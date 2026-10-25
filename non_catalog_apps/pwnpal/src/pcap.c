#include "../include/pcap.h"

#include <furi.h>
#include <furi_hal_rtc.h>
#include <string.h>

// 24-byte classic pcap global header, LE, linktype 105 — byte-for-byte Marauder's Buffer.cpp
static void pcap_global_header(uint8_t hdr[24]) {
    uint32_t magic = 0xa1b2c3d4;
    uint16_t vmaj = 2, vmin = 4;
    int32_t thiszone = 0;
    uint32_t sigfigs = 0;
    uint32_t snaplen = PCAP_SNAPLEN;
    uint32_t linktype = PCAP_LINKTYPE_IEEE802_11;
    memcpy(hdr + 0, &magic, 4);
    memcpy(hdr + 4, &vmaj, 2);
    memcpy(hdr + 6, &vmin, 2);
    memcpy(hdr + 8, &thiszone, 4);
    memcpy(hdr + 12, &sigfigs, 4);
    memcpy(hdr + 16, &snaplen, 4);
    memcpy(hdr + 20, &linktype, 4);
}

// 16-byte record header: ts_sec, ts_usec, incl_len, orig_len (LE)
static void pcap_record_header(uint8_t rh[16], uint32_t sec, uint32_t usec, uint32_t len) {
    memcpy(rh + 0, &sec, 4);
    memcpy(rh + 4, &usec, 4);
    memcpy(rh + 8, &len, 4);
    memcpy(rh + 12, &len, 4);
}

bool pcap_append_frame(Storage* storage, const char* name, const uint8_t* frame, uint16_t len) {
    if(len == 0 || len > PCAP_SNAPLEN) return false;
    if(!name || !name[0]) name = "capture";
    storage_common_mkdir(storage, PWNPAL_HS_DIR); // no-op if it exists

    char path[128];
    snprintf(path, sizeof(path), "%s/%s.pcap", PWNPAL_HS_DIR, name);

    File* f = storage_file_alloc(storage);
    bool ok = storage_file_open(f, path, FSAM_WRITE, FSOM_OPEN_APPEND);
    if(ok && storage_file_size(f) == 0) {
        uint8_t gh[24];
        pcap_global_header(gh);
        ok = storage_file_write(f, gh, sizeof(gh)) == sizeof(gh);
    }
    if(ok) {
        uint32_t sec = furi_hal_rtc_get_timestamp();
        uint32_t usec = 0; // RTC is 1 Hz; sub-second not needed for cracking
        uint8_t rh[16];
        pcap_record_header(rh, sec, usec, len);
        ok = storage_file_write(f, rh, sizeof(rh)) == sizeof(rh);
        if(ok) ok = storage_file_write(f, frame, len) == len;
    }
    storage_file_close(f);
    storage_file_free(f);
    return ok;
}
