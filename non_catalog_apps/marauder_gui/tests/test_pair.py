"""Host regression tests: python tests/test_pair.py [optional local capture.pcap].

Requires GCC on PATH. Only synthetic packets are stored in the repository.
The wrapper calls the production detector; it does not reimplement its logic.
"""
import ctypes
import atexit
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TMP = tempfile.TemporaryDirectory()
WORK = Path(TMP.name)
(WORK / "storage").mkdir()
(WORK / "furi.h").write_text("#include <stdbool.h>\n#include <stdint.h>\n#include <stddef.h>\n")
(WORK / "storage" / "storage.h").write_text('''
typedef void Storage;
typedef void File;
#define FSAM_READ 0
#define FSOM_OPEN_EXISTING 0
#define storage_file_alloc(s) ((File*)(s))
#define storage_file_open(f,p,a,m) ((void)(f), (void)(p), (void)(a), (void)(m), false)
#define storage_file_free(f) ((void)(f))
#define storage_file_close(f) ((void)(f))
#define storage_file_read(f,b,n) ((void)(f), (void)(b), (void)(n), (size_t)0)
#define storage_file_size(f) ((void)(f), (uint64_t)0)
#define storage_file_seek(f,n,a) ((void)(f), (void)(n), (void)(a))
''')
(WORK / "wrapper.c").write_text('''
#include "marauder_gui_pcap_pair.h"
#include "marauder_gui_pcap_index.h"
#include <string.h>
static MarauderPcapPairTracker tracker;
static MarauderPcapLiveParser parser;
static int matches;
void reset(void) {
    memset(&tracker, 0, sizeof(tracker));
    marauder_pcap_live_parser_reset(&parser);
    matches = 0;
}
int feed(const uint8_t* p, size_t n, size_t full, uint64_t t) {
    uint8_t ap[6];
    return marauder_pcap_pair_feed(&tracker, p, n, full, t, ap);
}
static void callback(void* ctx, const uint8_t* p, size_t n, size_t full, uint64_t t) {
    (void)ctx;
    matches += feed(p, n, full, t);
}
int stream(const uint8_t* p, size_t n) {
    marauder_pcap_live_parser_feed(&parser, p, n, callback, NULL);
    return matches;
}
''')
LIB = WORK / ("pair.dll" if sys.platform == "win32" else "pair.so")
subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared",
                "-fPIC", "-I", str(ROOT), "-I", str(WORK), str(WORK / "wrapper.c"),
                str(ROOT / "marauder_gui_pcap_index.c"),
                str(ROOT / "marauder_gui_pcap_pair.c"), "-o", str(LIB)], check=True)
DLL = ctypes.CDLL(str(LIB))
DLL.feed.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_uint64]
DLL.feed.restype = ctypes.c_int
DLL.stream.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
DLL.stream.restype = ctypes.c_int


def unload():
    # Windows keeps a loaded DLL locked; release it before TemporaryDirectory.
    import _ctypes
    if sys.platform == "win32":
        _ctypes.FreeLibrary(DLL._handle)
    else:
        _ctypes.dlclose(DLL._handle)


atexit.register(unload)


def packet(m=1, client=2, replay=1, qos=False, ht=False):
    ap = bytes.fromhex("020000000001")
    sta = bytes([2, 0, 0, 0, 1, client])
    header = bytearray(24 + (2 if qos else 0) + (4 if ht else 0))
    header[0] = 0x88 if qos else 0x08
    header[1] = (2 if m in (1, 3) else 1) | (0x80 if ht else 0)
    header[4:10], header[10:16] = (sta, ap) if m in (1, 3) else (ap, sta)
    header[16:22] = ap
    key = bytearray(95)
    key[0] = 2
    struct.pack_into(">H", key, 1, {1: 0x8a, 2: 0x10a, 3: 0x13ca, 4: 0x30a}[m])
    struct.pack_into(">Q", key, 5, replay)
    key[13:45] = bytes([1 if m in (1, 3) else 2]) * 32
    if m != 1:
        key[77:93] = b"x" * 16
    return bytes(header) + bytes.fromhex("aaaa03000000888e") + b"\x02\x03\x00\x5f" + key


def feed(p, time=1, full=None):
    return bool(DLL.feed(p, min(len(p), 160), len(p) if full is None else full, time))


class PairTests(unittest.TestCase):
    def setUp(self):
        DLL.reset()

    def test_matching_pair_and_retransmission(self):
        self.assertFalse(feed(packet()))
        self.assertFalse(feed(packet(), 2))
        self.assertTrue(feed(packet(2), 3))

    def test_qos_and_ht(self):
        for qos, ht in [(True, False), (True, True)]:
            DLL.reset()
            self.assertFalse(feed(packet(qos=qos, ht=ht)))
            self.assertTrue(feed(packet(2, qos=qos, ht=ht), 2))

    def test_stream_chunks_and_incomplete_record(self):
        raw = struct.pack("<IHHIIII", 0xa1b2c3d4, 2, 4, 0, 0, 65535, 105)
        for m in (1, 2):
            p = packet(m)
            raw += struct.pack("<IIII", 106, m * 5000, len(p), len(p)) + p
        for chunk in (1, 7, 16, 24, 128, 160, 512):
            DLL.reset()
            for offset in range(0, len(raw) - 1, chunk):
                part = raw[offset:min(offset + chunk, len(raw) - 1)]
                self.assertEqual(DLL.stream(part, len(part)), 0)
            self.assertEqual(DLL.stream(raw[-1:], 1), 1)

    def test_m2_alone_m3_m4(self):
        for m in (2, 3, 4):
            self.assertFalse(feed(packet(m)))

    def test_client_replay_time_and_reset(self):
        feed(packet(), 100)
        self.assertFalse(feed(packet(2, client=3), 101))
        self.assertFalse(feed(packet(2, replay=2), 101))
        self.assertFalse(feed(packet(2), 99))
        self.assertFalse(feed(packet(2), 10000101))
        self.assertTrue(feed(packet(2), 10000100))
        DLL.reset()
        self.assertFalse(feed(packet(2), 101))

    def test_every_truncated_prefix(self):
        p = packet(2)
        feed(packet())
        for n in range(len(p)):
            self.assertFalse(feed(p[:n], 2, len(p)), n)
        self.assertFalse(feed(p, 2, len(p) - 1))

    def test_malformed_m2(self):
        feed(packet())
        original = packet(2)
        # direction, protected/fragmented/null frames, EAPOL type, descriptor,
        # group key, request/error/secure, wrong AP, replay and nonce/MIC.
        mutations = [(1, 2), (1, 0x41), (1, 5), (22, 1), (0, 0x48),
                     (33, 0), (36, 254), (38, 2), (37, 9), (37, 5),
                     (37, 3), (4, 4), (48, 9)]
        for offset, value in mutations:
            p = bytearray(original)
            p[offset] = value
            self.assertFalse(feed(bytes(p), 2), (offset, value))
        for start, end, value in [(49, 81, 0), (49, 81, 1), (113, 129, 0)]:
            p = bytearray(original)
            p[start:end] = bytes([value]) * (end - start)
            self.assertFalse(feed(bytes(p), 2))

    def test_bounded_eviction_and_duplicate_m1(self):
        for client in range(2, 10):
            feed(packet(client=client))
        feed(packet(client=2), 2)
        self.assertTrue(feed(packet(2, client=3), 3))
        feed(packet(client=10), 4)
        self.assertFalse(feed(packet(2, client=2), 5))
        self.assertTrue(feed(packet(2, client=10), 5))


if __name__ == "__main__":
    capture = Path(sys.argv.pop()) if len(sys.argv) == 2 and sys.argv[1].endswith(".pcap") else None
    result = unittest.main(exit=False)
    if not result.result.wasSuccessful():
        sys.exit(1)
    if capture:
        DLL.reset()
        raw = capture.read_bytes()
        assert struct.unpack_from("<I", raw)[0] == 0xa1b2c3d4
        assert struct.unpack_from("<I", raw, 20)[0] == 105
        offset, index, matches = 24, 0, []
        while offset < len(raw):
            seconds, micros, size, original = struct.unpack_from("<IIII", raw, offset)
            offset += 16
            frame = raw[offset:offset + size]
            assert len(frame) == size
            offset += size
            index += 1
            if feed(frame, seconds * 1000000 + micros):
                matches.append(index)
        print(f"Local replay: {index} frames; M1/M2 candidates at frames {matches}")
