/*
 * Telecrane Control - forge frames for Telecrane-style 2FSK crane remotes
 *
 * Serial format (4 bytes, shown as 8 chars): C + channel-nibble + 6-digit ID.
 *   The 6 digits are 3 decimal pairs that become the 3 ID bytes, e.g.
 *   C8123456 -> channel 8, ID bytes 12 34 56 (0x0C 0x22 0x38).
 *   16 channels (C0-CF). Channel plan measured on ch8 (848.934 MHz) and ch9
 *   (850.918 MHz), step 1.9843 MHz; other channels extrapolated.
 *
 * Frame: [0x55 preamble ~77b][sync 45BA85BA8][const][24-bit ID @ bits 131-154]
 *        [button mask][trailer], ~197 bits, 2FSK 18.9 kbaud, no checksum.
 *
 * Sniff: hops the 16 channels in async RX (CC1101 GDO0 = sliced data),
 * decodes level/duration captures, extracts the ID from valid frames.
 *
 * Build: ufbt (MNTM SDK).  See README.md for install; PROTOCOL.md for the
 * on-air protocol.
 */
#include <furi.h>
#include <furi_hal_subghz.h>
#include <furi_hal_region.h>
#include "radio_device_loader.h"
#include <toolbox/level_duration.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/dialog_ex.h>
#include <gui/canvas.h>
#include <gui/view.h>
#include <input/input.h>
#include <flipper_format/flipper_format.h>
#include <storage/storage.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#define TAG "TelecraneControl"

// Radio backend: the internal CC1101 or an external CC1101 board, selected on the main menu
// and shown on the sniff screen. Same loader the stock Sub-GHz apps use.
static const SubGhzDevice* crane_radio = NULL;
static SubGhzRadioDeviceType crane_radio_type = SubGhzRadioDeviceTypeInternal;

static const char* crane_radio_label(void) {
    return (crane_radio != NULL && radio_device_loader_is_external(crane_radio)) ? "External" :
                                                                                   "Internal";
}

// Selecting External falls back to the internal radio when no board answers, so the label is
// always re-derived from the device we actually ended up with.
static void crane_radio_apply(SubGhzRadioDeviceType type) {
    crane_radio = radio_device_loader_set(crane_radio, type);
    crane_radio_type = radio_device_loader_is_external(crane_radio) ?
                           SubGhzRadioDeviceTypeExternalCC1101 :
                           SubGhzRadioDeviceTypeInternal;
}

// ----------------------------------------------------------------------------
// protocol constants
// ----------------------------------------------------------------------------

#define CRANE_NIBBLES            50
#define CRANE_STRIP_BITS         3 // decoder-phase bits at frame start
#define CRANE_BIT_US             53 // 18.9 kbaud
#define CRANE_PRESET_DEVIATN_IDX 19
#define CRANE_IDLE_TAIL_FRAMES \
    50 // idle frames sent on release, like a real remote (STOP sends none)

// measured channel plan: ch8/ch9 centers, fixed step to the other 14 channels
#define CRANE_CH8_FREQ      848934115.5
#define CRANE_CH9_FREQ      850918412.9
#define CRANE_CH_STEP       1984297.4
#define CRANE_CHANNEL_COUNT 16

// CC1101 registers for async 2-FSK (modeled on the stock 2FSKDev238Async preset)
static uint8_t crane_preset[] = {
    0x02, 0x0d, // IOCFG0: GDO0 async serial data output
    0x0b, 0x06, // FSCTRL1
    0x08, 0x32, // PKTCTRL0: async, continuous, no whitening
    0x07, 0x04, // PKTCTRL1
    0x14, 0x00, // MDMCFG0
    0x13, 0x02, // MDMCFG1
    0x12, 0x04, // MDMCFG2: 2-FSK, no preamble/sync
    0x11, 0x7d, // MDMCFG3: DRATE_M=0x7d -> ~18.9 kbaud demod filter (E=9 in MDMCFG4)
    0x10, 0xc9, // MDMCFG4: CHANBW_E=3 CHANBW_M=0 -> RX BW ~101 kHz, DRATE_E=9
    0x15, 0x23, // DEVIATN: patched per channel
    0x18, 0x18, // MCSM0
    0x19, 0x16, // FOCCFG
    0x1d, 0x91, // AGCCTRL0
    0x1c, 0x00, // AGCCTRL1
    0x1b, 0x07, // AGCCTRL2
    0x20, 0xfb, // WORCTRL
    0x22, 0x10, // FREND0
    0x21, 0x56, // FREND1
    0x00, 0x00, // end of register load
    0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // PA table: 10 dBm
};

// button commands: masks verified identical on both models
typedef struct {
    const char* name;
    uint16_t mask;
} CraneCmd;

static const CraneCmd crane_cmds[] = {
    {"* EDIT", 0x0000}, // index 0: edit entry, never transmitted
    {"* DELETE", 0x0000}, // index 1: delete entry, never transmitted
    {"START", 0x0010},
    {"STOP", 0x0008},
    {"UP", 0x0004},
    {"DOWN", 0x0002},
    {"NORTH", 0x2000},
    {"SOUTH", 0x4000},
    {"EAST", 0x0001},
    {"WEST", 0x8000},
    {"1", 0x1010},
    {"2", 0x0808},
    {"3", 0x0404},
    {"4", 0x0202},
};
#define CRANE_CMD_COUNT  (int)(sizeof(crane_cmds) / sizeof(crane_cmds[0]))
#define CRANE_CMD_EDIT   0
#define CRANE_CMD_DELETE 1

// idle frame templates, 50 nibbles = 200 bits (from signal-demod-file output)
static const uint8_t crane_tpl_ch8[] = {
    0x1, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, // 15555555
    0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, // 55555555
    0x5, 0x5, 0x5, 0x5, 0x4, 0x5, 0xb, 0xa, // 555545ba
    0x8, 0x5, 0xb, 0xa, 0x8, 0x0, 0x0, 0x1, // 85ba8001
    0x0, 0x1, 0x6, 0x4, 0x4, 0x0, 0x6, 0x0, // 01644060
    0x0, 0x0, 0x0, 0x7, 0x0, 0xa, 0x8, 0x0, // 00070a80
    0x0, 0x0, // 00
};
static const uint8_t crane_tpl_ch9[] = {
    0x1, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, // 15555555
    0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, // 55555555
    0x5, 0x5, 0x5, 0x5, 0x4, 0x5, 0xb, 0xa, // 555545ba
    0x8, 0x5, 0xb, 0xa, 0x8, 0x0, 0x0, 0x1, // 85ba8001
    0x2, 0x0, 0xa, 0x2, 0xe, 0x1, 0xa, 0x0, // 20a2e1a0
    0x0, 0x0, 0x0, 0x6, 0x4, 0x0, 0x0, 0x0, // 00064000
    0x0, 0x1, // 01
};

// 36-bit sync word 45BA85BA8 (anchors every frame)
static const bool crane_sync_bits[] = {
    0, 1, 0, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0,
    0, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 0, 0,
};

// DEVIATN model verified against the flipper presets:
// f_dev = (8 + M) * 2^E * 26e6/2^17
static double crane_deviatn_to_hz(uint8_t deviatn) {
    const int m = deviatn & 0xf;
    const int e = (deviatn >> 4) & 0x7;
    return (8 + m) * (1 << e) * (26000000.0 / 131072.0);
}

// ----------------------------------------------------------------------------
// channel profile: everything the radio needs, derived from the serial
// ----------------------------------------------------------------------------

typedef struct {
    double freq;
    double dev_hz;
    uint8_t deviatn;
    int period; // samples between frame starts
    const uint8_t* tpl;
    uint16_t base_b;
    int id_bytes[3];
} CraneProfile;

// serial: C + channel nibble (hex) + 6 decimal digits (3 ID bytes)
static bool crane_parse_serial(const char* serial, int* channel, int* id_bytes) {
    if(strlen(serial) != 8 || (serial[0] != 'C' && serial[0] != 'c')) {
        return false;
    }
    char nib = serial[1];
    int ch;
    if(nib >= '0' && nib <= '9') {
        ch = nib - '0';
    } else if(nib >= 'a' && nib <= 'f') {
        ch = nib - 'a' + 10;
    } else if(nib >= 'A' && nib <= 'F') {
        ch = nib - 'A' + 10;
    } else {
        return false;
    }
    for(int i = 0; i < 6; i++) {
        if(serial[2 + i] < '0' || serial[2 + i] > '9') {
            return false;
        }
    }
    for(int i = 0; i < 3; i++) {
        id_bytes[i] = (serial[2 + 2 * i] - '0') * 10 + (serial[3 + 2 * i] - '0');
    }
    *channel = ch;
    return true;
}

// nearest measured channel provides the template/base/period/deviatn
static bool crane_profile(const char* serial, CraneProfile* profile) {
    int channel;
    int id_bytes[3];
    if(!crane_parse_serial(serial, &channel, id_bytes)) {
        // caller must not transmit: channel/id_bytes would be uninitialised stack, which
        // put a garbage frequency on the air
        return false;
    }
    const bool near9 = channel >= 9;
    profile->freq = CRANE_CH8_FREQ + (channel - 8) * CRANE_CH_STEP;
    profile->deviatn = near9 ? 0x24 : 0x23;
    profile->dev_hz = crane_deviatn_to_hz(profile->deviatn);
    profile->period = near9 ? 30023 : 19940;
    profile->tpl = near9 ? crane_tpl_ch9 : crane_tpl_ch8;
    profile->base_b = near9 ? 0x0006 : 0x0007;
    memcpy(profile->id_bytes, id_bytes, sizeof(id_bytes));
    return true;
}

// Rejects a bad serial on the keyboard itself, with the format spelled out. Without this the
// app silently bounced back to the main menu and the typing was lost.
static bool crane_serial_validator(const char* text, FuriString* error, void* context) {
    UNUSED(context);
    int channel;
    int id_bytes[3];
    if(crane_parse_serial(text, &channel, id_bytes)) {
        return true;
    }
    furi_string_printf(error, "Need C, channel\n0-F, then 6\ndigits\n(e.g. C8123456)");
    return false;
}

// ----------------------------------------------------------------------------
// saved RCs: /ext/apps_data/crane_remote/crane_remote.conf
// ----------------------------------------------------------------------------

#define CRANE_MAX_RCS    16
#define CRANE_NAME_LEN   24
#define CRANE_SERIAL_LEN 12
#define CRANE_DATA_DIR   "/ext/apps_data/crane_remote"
#define CRANE_CONF_PATH  CRANE_DATA_DIR "/crane_remote.conf"
#define CRANE_DEBUG_PATH CRANE_DATA_DIR "/debug.log"

typedef struct {
    char name[CRANE_NAME_LEN];
    char serial[CRANE_SERIAL_LEN];
} CraneRc;

static CraneRc crane_rcs[CRANE_MAX_RCS];
static size_t crane_rc_count;

// Debug tracing to CRANE_DEBUG_PATH. Off in the pack build: every scene change, sniff keypress
// and TX frame opened the storage record and appended a line to the SD card. Set to 1 to get
// the trace back - the call sites are untouched, only the bodies go away.
#define CRANE_DEBUG_LOG 0

#if CRANE_DEBUG_LOG
static void crane_dbg(const char* msg) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, CRANE_DEBUG_PATH, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        storage_file_write(file, msg, strlen(msg));
        storage_file_write(file, "\n", 1);
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

// printf-style wrapper over crane_dbg
static void crane_dbgf(const char* fmt, ...) {
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    crane_dbg(buf);
}

// truncate the log at app start so each session's trace reads cleanly
// (crane_dbg appends; pull with: storage.py read /ext/apps_data/crane_remote/debug.log)
static void crane_dbg_init(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, CRANE_DATA_DIR);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, CRANE_DEBUG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        const char* hdr = "=== crane_remote session start ===\n";
        storage_file_write(file, hdr, strlen(hdr));
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}
#else
// No-op bodies rather than macros, so every call site still compiles and still evaluates its
// arguments (no unused-variable warnings) - the empty calls then inline away.
static void crane_dbg(const char* msg) {
    UNUSED(msg);
}

static void crane_dbgf(const char* fmt, ...) {
    UNUSED(fmt);
}

static void crane_dbg_init(void) {
}
#endif

static void crane_save_rcs(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, CRANE_DATA_DIR);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    if(!flipper_format_file_open_new(ff, CRANE_CONF_PATH)) {
        flipper_format_free(ff);
        furi_record_close(RECORD_STORAGE);
        return;
    }
    const uint32_t count = crane_rc_count;
    flipper_format_write_uint32(ff, "count", &count, 1);
    char key[24];
    for(size_t i = 0; i < crane_rc_count; i++) {
        snprintf(key, sizeof(key), "name_%lu", (unsigned long)i);
        flipper_format_write_string_cstr(ff, key, crane_rcs[i].name);
        snprintf(key, sizeof(key), "serial_%lu", (unsigned long)i);
        flipper_format_write_string_cstr(ff, key, crane_rcs[i].serial);
    }
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

static void crane_load_rcs(void) {
    crane_rc_count = 0;
    memset(crane_rcs, 0, sizeof(crane_rcs));

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    const bool opened = flipper_format_file_open_existing(ff, CRANE_CONF_PATH);
    uint32_t count = 0;
    if(opened) {
        if(!flipper_format_read_uint32(ff, "count", &count, 1)) {
            count = 0;
        }
        if(count > CRANE_MAX_RCS) {
            count = CRANE_MAX_RCS;
        }
    }
    if(opened && count > 0) {
        char key[24];
        FuriString* s = furi_string_alloc();
        for(uint32_t i = 0; i < count; i++) {
            // written at crane_rc_count, not i, so a rejected entry closes its own gap
            CraneRc* rc = &crane_rcs[crane_rc_count];
            snprintf(key, sizeof(key), "name_%lu", (unsigned long)i);
            if(!flipper_format_read_string(ff, key, s)) {
                break;
            }
            strncpy(rc->name, furi_string_get_cstr(s), CRANE_NAME_LEN - 1);
            snprintf(key, sizeof(key), "serial_%lu", (unsigned long)i);
            if(!flipper_format_read_string(ff, key, s)) {
                break;
            }
            strncpy(rc->serial, furi_string_get_cstr(s), CRANE_SERIAL_LEN - 1);
            int channel;
            int id_bytes[3];
            if(!crane_parse_serial(rc->serial, &channel, id_bytes)) {
                // a hand-edited or truncated conf must not reach the transmitter
                memset(rc, 0, sizeof(*rc));
                continue;
            }
            crane_rc_count++;
        }
        furi_string_free(s);
    }
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    // no seeded defaults: a fresh install starts with an empty RC list; the user
    // adds remotes via "New RC from Serial" or "Sniff RC"
}

// ----------------------------------------------------------------------------
// transmitter
// ----------------------------------------------------------------------------

static volatile bool crane_holding = false;
static volatile bool crane_exit = false;
static volatile bool crane_radio_ready = false;
static CraneProfile crane_tx_profile;
static uint16_t crane_tx_mask;

typedef struct {
    const bool* bits;
    size_t count;
    size_t index;
} CraneTx;

static LevelDuration crane_tx_callback(void* context) {
    CraneTx* tx = context;
    if(tx->index >= tx->count) {
        return level_duration_reset();
    }
    const bool bit = tx->bits[tx->index];
    tx->index++;
    return level_duration_make(bit, CRANE_BIT_US);
}

static void crane_ensure_region(void) {
    // Session region covering 0-1 GHz so the region gate allows our channel. The HAL keeps
    // the pointer and never frees the previous one, so this must happen exactly once -
    // it used to run on every TX setup and every sniff sweep, leaking each time.
    static bool done = false;
    if(done) {
        return;
    }
    done = true;
    FuriHalRegion* region = malloc(sizeof(FuriHalRegion) + sizeof(FuriHalRegionBand));
    region->country_code[0] = '0';
    region->country_code[1] = '0';
    region->country_code[2] = 0;
    region->country_code[3] = 0;
    region->bands_count = 1;
    region->bands[0].start = 0;
    region->bands[0].end = 1000000000;
    region->bands[0].power_limit = 12;
    region->bands[0].duty_cycle = 50;
    furi_hal_region_set(region);
}

// sends one frame; the radio must already be set up (preset + frequency loaded)
// by the caller, which happens once per hold rather than once per frame
static bool crane_tx_stream(const bool* bits, size_t count) {
    CraneTx tx = {.bits = bits, .count = count, .index = 0};
    if(!subghz_devices_start_async_tx(crane_radio, crane_tx_callback, &tx)) {
        FURI_LOG_E(TAG, "tx blocked by region");
        return false;
    }
    while(!subghz_devices_is_async_complete_tx(crane_radio)) {
        furi_delay_ms(2);
    }
    subghz_devices_stop_async_tx(crane_radio);
    return true;
}

// builds one frame's bit stream for the current mask
static size_t crane_build_frame(bool* out) {
    bool bits[200];
    const uint8_t* tpl = crane_tx_profile.tpl;
    for(size_t i = 0; i < 50; i++) {
        for(int k = 3; k >= 0; k--) {
            bits[4 * i + (3 - k)] = ((tpl[i] >> k) & 1) != 0;
        }
    }
    for(int k = 0; k < 24; k++) {
        const int byte_idx = k / 8;
        const int bit_idx = 7 - (k % 8);
        bits[131 + k] = ((crane_tx_profile.id_bytes[byte_idx] >> bit_idx) & 1) != 0;
    }
    for(int k = 0; k < 5; k++) {
        bits[155 + k] = ((crane_tx_mask >> (4 - k)) & 1) != 0;
    }
    const uint16_t field_b = crane_tx_profile.base_b + crane_tx_mask;
    for(int k = 0; k < 16; k++) {
        bits[160 + k] = ((field_b >> (15 - k)) & 1) != 0;
    }
    memcpy(out, bits + CRANE_STRIP_BITS, 200 - CRANE_STRIP_BITS);
    return 200 - CRANE_STRIP_BITS;
}

// ----------------------------------------------------------------------------
// sniffer: channel hop + capture + decode
// ----------------------------------------------------------------------------

// Sized to hold ~20 frames, not the whole dwell. A frame is 200 bits at CRANE_BIT_US
// (10.6ms) and the decoder only needs sync + 75 bits from wherever it finds it, so 4096
// edges / 4096 bits is ample - and 20K allocates where the original 80K did not.
#define CRANE_CAP_MAX        4096
#define CRANE_CAP_BITS       4096
#define CRANE_SNIFF_DWELL_MS 600
#define CRANE_SNIFF_DEBUG    0 // 1: log per-channel cap/nbits/alt/same + a frame hex dump

// crane_cap (64K) and the sniff bit buffer (16K) are allocated only for the duration of a
// sniff sweep. As statics they put ~82K in .bss, which the FAP loader has to find in one
// piece at load time - the app then fails to start at all with "Not enough memory",
// even though nothing but the sniffer ever touches them.
static uint32_t* crane_cap = NULL;
static volatile size_t crane_cap_count = 0;

static void crane_capture_callback(bool level, uint32_t duration, void* context) {
    UNUSED(context);
    if(crane_cap != NULL && crane_cap_count < CRANE_CAP_MAX) {
        crane_cap[crane_cap_count++] = (level ? 0x80000000u : 0) | (duration & 0x7fffff);
    }
}

// durations -> NRZ bits (53 us per bit, run-length decoded)
static size_t crane_capture_to_bits(bool* bits, size_t max_bits) {
    size_t n = 0;
    for(size_t i = 0; i < crane_cap_count && n < max_bits; i++) {
        const uint32_t entry = crane_cap[i];
        const bool level = (entry & 0x80000000u) != 0;
        const uint32_t dur = entry & 0x7fffffu;
        if(dur == 0 || dur > 100000) {
            continue; // silence gap or junk
        }
        int k = (int)((dur + CRANE_BIT_US / 2) / CRANE_BIT_US);
        if(k < 1) {
            k = 1;
        }
        if(k > 500) {
            k = 500;
        }
        for(int j = 0; j < k && n < max_bits; j++) {
            bits[n++] = level;
        }
    }
    return n;
}

// scan the bit stream for sync 45BA85BA8 + the 12-bit constant, extract the ID
static bool crane_decode_bits(const bool* bits, size_t nbits, int channel, char* serial_out) {
    const size_t need = 36 + 15 + 24;
    if(nbits < need) {
        return false;
    }
    for(size_t i = 0; i + need <= nbits; i++) {
        bool match = true;
        for(int k = 0; k < 36; k++) {
            if(bits[i + k] != crane_sync_bits[k]) {
                match = false;
                break;
            }
        }
        if(!match) {
            continue;
        }
        // 12-bit constant anchors the frame after the sync: 11 zeros then a one.
        // The next 3 bits (frame bits 128-130) vary by channel (ch8 = 000,
        // ch9 = 001), so they are skipped, not verified, before the ID at i+51.
        for(int k = 0; k < 12; k++) {
            if(bits[i + 36 + k] != (k == 11)) {
                match = false;
                break;
            }
        }
        if(!match) {
            continue;
        }
        int bytes[3] = {0, 0, 0};
        for(int k = 0; k < 24; k++) {
            if(bits[i + 51 + k]) {
                bytes[k / 8] |= (1 << (7 - (k % 8)));
            }
        }
        // the ID encodes as decimal digit pairs: every byte must be <= 99
        if(bytes[0] > 99 || bytes[1] > 99 || bytes[2] > 99) {
            continue;
        }
        // manual assembly: C + hex channel nibble + 3 decimal digit pairs
        serial_out[0] = 'C';
        serial_out[1] = channel <= 9 ? ('0' + channel) : ('A' + channel - 10);
        serial_out[2] = '0' + bytes[0] / 10;
        serial_out[3] = '0' + bytes[0] % 10;
        serial_out[4] = '0' + bytes[1] / 10;
        serial_out[5] = '0' + bytes[1] % 10;
        serial_out[6] = '0' + bytes[2] / 10;
        serial_out[7] = '0' + bytes[2] % 10;
        serial_out[8] = 0;
        return true;
    }
    return false;
}

// ----------------------------------------------------------------------------
// app state
// ----------------------------------------------------------------------------

// Logical scene, used for Back navigation only.
typedef enum {
    CraneSceneMain,
    CraneSceneRcList,
    CraneSceneTx,
    CraneSceneEdit,
    CraneSceneConfirmDelete,
    CraneSceneTextInput,
    CraneSceneSniff,
} CraneScene;

// Views registered with the dispatcher. Main, RC list and Edit are all the same Submenu
// repopulated, so they share one view id rather than registering one View under three.
typedef enum {
    CraneViewMenu,
    CraneViewTx,
    CraneViewConfirmDelete,
    CraneViewTextInput,
    CraneViewSniff,
} CraneViewId;

typedef struct {
    char name[CRANE_NAME_LEN];
    char serial[CRANE_SERIAL_LEN];
    uint8_t selection;
    bool holding;
    bool valid;
} CraneTxModel;

typedef struct {
    int channel; // currently scanned channel
    bool done; // a serial was captured
    char serial[CRANE_SERIAL_LEN];
    char radio[12]; // which radio this sweep is running on
} CraneSniffModel;

typedef enum {
    CraneModeIdle,
    CraneModeTx,
    CraneModeSniff,
} CraneMode;

typedef enum {
    CraneTiEditName,
    CraneTiEditSerial,
    CraneTiNewSerial,
    CraneTiNewName,
} CraneTiMode;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    TextInput* text_input;
    DialogEx* dialog;
    View* tx_view;
    View* sniff_view;
    uint8_t current_scene;
    uint8_t current_rc;
    CraneTiMode ti_mode;
    char edit_buffer[24];
    char confirm_text[64];
    FuriThread* worker;
} CraneApp;

static CraneApp* crane_app;

// worker mode + shared state
static volatile CraneMode crane_worker_mode = CraneModeIdle;
static volatile bool crane_sniff_done = false;
static char crane_sniff_serial[CRANE_SERIAL_LEN];
static int crane_sniff_channel = 0;
static char crane_new_serial[CRANE_SERIAL_LEN];

// ----------------------------------------------------------------------------
// worker: TX hold loop + sniff sweep
// ----------------------------------------------------------------------------

static int32_t crane_worker_fn(void* context) {
    UNUSED(context);
    static bool frame[2000];

    while(!crane_exit) {
        if(crane_worker_mode == CraneModeTx) {
            if(crane_holding) {
                if(!crane_radio_ready) {
                    crane_ensure_region();
                    crane_preset[CRANE_PRESET_DEVIATN_IDX] = crane_tx_profile.deviatn;
                    subghz_devices_reset(crane_radio);
                    subghz_devices_load_preset(
                        crane_radio, FuriHalSubGhzPresetCustom, crane_preset);
                    subghz_devices_set_frequency(crane_radio, (uint32_t)crane_tx_profile.freq);
                    crane_radio_ready = true;
                    crane_dbg("radio: setup complete");
                }
                const uint32_t start = furi_get_tick();
                const size_t n = crane_build_frame(frame);
                const bool ok = crane_tx_stream(frame, n);
                crane_dbg(ok ? "radio: frame sent" : "radio: frame BLOCKED");
                if(!ok) {
                    crane_holding = false;
                }
                const uint32_t elapsed = furi_get_tick() - start;
                const uint32_t period_ms = (uint32_t)(crane_tx_profile.period / 1000);
                if(elapsed < period_ms) {
                    furi_delay_ms(period_ms - elapsed);
                }
            } else {
                if(crane_radio_ready) {
                    // button released: emit an idle tail like a real remote, then drop
                    // the radio. STOP (0x0008) sends no tail, matching the hardware.
                    if(crane_tx_mask != 0x0008) {
                        const uint16_t held = crane_tx_mask;
                        crane_tx_mask = 0; // idle frame: keep the ID, clear the button
                        const uint32_t period_ms = (uint32_t)(crane_tx_profile.period / 1000);
                        for(int t = 0; t < CRANE_IDLE_TAIL_FRAMES && !crane_exit &&
                                       !crane_holding && crane_worker_mode == CraneModeTx;
                            t++) {
                            const uint32_t start = furi_get_tick();
                            const size_t n = crane_build_frame(frame);
                            if(!crane_tx_stream(frame, n)) {
                                break;
                            }
                            const uint32_t elapsed = furi_get_tick() - start;
                            if(elapsed < period_ms) {
                                furi_delay_ms(period_ms - elapsed);
                            }
                        }
                        crane_tx_mask = held;
                        crane_dbg("radio: idle tail sent");
                    }
                    subghz_devices_idle(crane_radio);
                    crane_radio_ready = false;
                    crane_dbg("radio: idle");
                }
                furi_delay_ms(10);
            }
        } else if(crane_worker_mode == CraneModeSniff) {
            if(crane_sniff_done) {
                // a serial is on screen: wait for the UI thread to accept it,
                // don't re-arm the radio or busy-spin
                furi_delay_ms(50);
                continue;
            }
            // furi's malloc crashes the app on OOM rather than returning NULL, so ask
            // first and back out quietly if the sweep would not fit.
            const size_t sniff_bytes = CRANE_CAP_MAX * sizeof(uint32_t) + CRANE_CAP_BITS;
            if(memmgr_get_free_heap() < sniff_bytes + 8192) {
                crane_worker_mode = CraneModeIdle;
                continue;
            }
            bool* bits = malloc(CRANE_CAP_BITS);
            crane_cap = malloc(CRANE_CAP_MAX * sizeof(uint32_t));
            crane_ensure_region();
            crane_dbg("sniff: sweep start");
            for(int ch = 0; ch < CRANE_CHANNEL_COUNT && crane_worker_mode == CraneModeSniff &&
                            !crane_sniff_done && !crane_exit;
                ch++) {
                const double freq = CRANE_CH8_FREQ + (ch - 8) * CRANE_CH_STEP;
                const uint8_t deviatn = (ch >= 9) ? 0x24 : 0x23;
                crane_sniff_channel = ch;
                // push the live channel into the view model and force a redraw
                with_view_model(
                    crane_app->sniff_view, CraneSniffModel * model, { model->channel = ch; }, true);

                crane_preset[CRANE_PRESET_DEVIATN_IDX] = deviatn;
                subghz_devices_reset(crane_radio);
                subghz_devices_load_preset(crane_radio, FuriHalSubGhzPresetCustom, crane_preset);
                subghz_devices_set_frequency(crane_radio, (uint32_t)freq);
                crane_cap_count = 0;
                subghz_devices_start_async_rx(crane_radio, crane_capture_callback, NULL);
                // dwell: ~10 frames at the 20-30 ms cadence
                furi_delay_ms(CRANE_SNIFF_DWELL_MS);
                subghz_devices_stop_async_rx(crane_radio);

                const size_t nbits = crane_capture_to_bits(bits, CRANE_CAP_BITS);
#if CRANE_SNIFF_DEBUG
                // diagnostics: longest alternating-bit run (the preamble is ~77 bits
                // of 0101..) and longest constant run (a dominant one = no clean signal)
                size_t best_alt = nbits ? 1 : 0;
                size_t best_same = nbits ? 1 : 0;
                size_t cur_alt = 1;
                size_t cur_same = 1;
                for(size_t bi = 1; bi < nbits; bi++) {
                    if(bits[bi] != bits[bi - 1]) {
                        cur_alt++;
                        cur_same = 1;
                    } else {
                        cur_same++;
                        cur_alt = 1;
                    }
                    if(cur_alt > best_alt) best_alt = cur_alt;
                    if(cur_same > best_same) best_same = cur_same;
                }
                crane_dbgf(
                    "sniff: ch%d cap=%u nbits=%u alt=%u same=%u",
                    ch,
                    (unsigned)crane_cap_count,
                    (unsigned)nbits,
                    (unsigned)best_alt,
                    (unsigned)best_same);
                // when a real preamble is present, dump the bits right after it so we
                // can read the sync/const/ID framing against the known template
                if(best_alt >= 40) {
                    size_t ae = 0;
                    size_t ca = 1;
                    for(size_t bi = 1; bi < nbits; bi++) {
                        ca = (bits[bi] != bits[bi - 1]) ? ca + 1 : 1;
                        if(ca >= best_alt) {
                            ae = bi;
                            break;
                        }
                    }
                    const size_t start = ae > 6 ? ae - 6 : 0;
                    char hx[72];
                    size_t p = 0;
                    for(int nib = 0;
                        nib < 64 && p < sizeof(hx) - 1 && start + (size_t)nib * 4 + 3 < nbits;
                        nib++) {
                        int v = 0;
                        for(int k = 0; k < 4; k++) {
                            v = (v << 1) | (bits[start + (size_t)nib * 4 + k] ? 1 : 0);
                        }
                        hx[p++] = "0123456789abcdef"[v];
                    }
                    hx[p] = 0;
                    crane_dbgf("sniff: ch%d hex@%u %s", ch, (unsigned)start, hx);
                }
#endif
                char serial[CRANE_SERIAL_LEN];
                if(crane_decode_bits(bits, nbits, ch, serial)) {
                    strncpy(crane_sniff_serial, serial, CRANE_SERIAL_LEN - 1);
                    crane_sniff_serial[CRANE_SERIAL_LEN - 1] = 0;
                    crane_sniff_done = true;
                    with_view_model(
                        crane_app->sniff_view,
                        CraneSniffModel * model,
                        {
                            strncpy(model->serial, serial, CRANE_SERIAL_LEN - 1);
                            model->serial[CRANE_SERIAL_LEN - 1] = 0;
                            model->done = true;
                        },
                        true);
                    char dbg[64];
                    snprintf(dbg, sizeof(dbg), "sniff: found %s", serial);
                    crane_dbg(dbg);
                }
            }
            // leaving the sweep (found, stopped via Back, or exiting): release the
            // radio so the next RX session starts from a clean idle state
            subghz_devices_idle(crane_radio);
            // the ISR is stopped by now, so dropping the capture buffer is safe
            free(crane_cap);
            crane_cap = NULL;
            free(bits);
            crane_dbgf(
                "sniff: sweep end done=%d mode=%d", (int)crane_sniff_done, (int)crane_worker_mode);
            if(!crane_sniff_done && !crane_exit && crane_worker_mode == CraneModeSniff) {
                // full sweep with nothing: pause so the UI can breathe, then repeat
                furi_delay_ms(100);
            }
        } else {
            furi_delay_ms(20);
        }
    }
    return 0;
}

// ----------------------------------------------------------------------------
// UI
// ----------------------------------------------------------------------------

static void crane_scene_main(CraneApp* app);
static void crane_scene_rc_list(CraneApp* app);
static void crane_scene_tx(CraneApp* app);
static void crane_scene_edit(CraneApp* app);
static void crane_scene_confirm_delete(CraneApp* app);
static void crane_scene_sniff(CraneApp* app);
static void crane_text_input_callback(void* context);
static void crane_submenu_edit_callback(void* context, uint32_t index);

// ---- scene: main menu

static void crane_submenu_main_callback(void* context, uint32_t index) {
    CraneApp* app = context;
    if(index == 1) {
        crane_scene_rc_list(app);
    } else if(index == 2) {
        // New RC from Serial: fresh serial entry
        crane_app->ti_mode = CraneTiNewSerial;
        // prefilled so the only thing left to type is digits - the leading "C" and the
        // channel nibble are the fiddly part on the default keyboard
        strcpy(app->edit_buffer, "C8");
        text_input_reset(app->text_input);
        text_input_set_header_text(app->text_input, "Serial: C + chan + 6 digits");
        text_input_set_minimum_length(app->text_input, 8);
        text_input_set_validator(app->text_input, crane_serial_validator, NULL);
        text_input_set_result_callback(
            app->text_input,
            crane_text_input_callback,
            app,
            app->edit_buffer,
            9, // 8 chars + terminator
            false);
        app->current_scene = CraneSceneTextInput;
        view_dispatcher_switch_to_view(app->view_dispatcher, CraneViewTextInput);
    } else {
        // Sniff RC
        crane_dbg("menu: sniff selected");
        crane_sniff_done = false;
        crane_sniff_channel = 0;
        // Re-probe before arming the worker: swapping crane_radio under a running sweep
        // would be a race. Only while on internal, so a board already in use is never
        // power-cycled; the label below then reports whatever we settled on.
        if(crane_radio_type == SubGhzRadioDeviceTypeInternal) {
            crane_radio_apply(SubGhzRadioDeviceTypeExternalCC1101);
        }
        crane_worker_mode = CraneModeSniff;
        with_view_model(
            app->sniff_view,
            CraneSniffModel * model,
            {
                model->channel = 0;
                model->done = false;
                model->serial[0] = 0;
                strncpy(model->radio, crane_radio_label(), sizeof(model->radio) - 1);
                model->radio[sizeof(model->radio) - 1] = 0;
            },
            false);
        crane_scene_sniff(app);
    }
}

static void crane_scene_main(CraneApp* app) {
    crane_worker_mode = CraneModeIdle;
    crane_dbg("scene: main");
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Telecrane Control");
    submenu_add_item(app->submenu, "My RCs", 1, crane_submenu_main_callback, app);
    submenu_add_item(app->submenu, "New RC from Serial", 2, crane_submenu_main_callback, app);
    submenu_add_item(app->submenu, "Sniff RC", 3, crane_submenu_main_callback, app);
    app->current_scene = CraneSceneMain;
    view_dispatcher_switch_to_view(app->view_dispatcher, CraneViewMenu);
}

// ---- scene: rc list

static void crane_submenu_rclist_callback(void* context, uint32_t index) {
    CraneApp* app = context;
    if(index >= 1 && index <= crane_rc_count) {
        app->current_rc = (uint8_t)(index - 1);
        crane_scene_tx(app);
    }
}

static void crane_scene_rc_list(CraneApp* app) {
    crane_worker_mode = CraneModeIdle;
    crane_dbg("scene: rc_list");
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "My RCs");
    if(crane_rc_count == 0) {
        // index 0 is not handled by the callback, so this reads as a label, not an entry
        submenu_add_item(
            app->submenu, "(no remotes saved)", 0, crane_submenu_rclist_callback, app);
    }
    for(size_t i = 0; i < crane_rc_count; i++) {
        submenu_add_item(
            app->submenu, crane_rcs[i].name, (uint32_t)i + 1, crane_submenu_rclist_callback, app);
    }
    app->current_scene = CraneSceneRcList;
    view_dispatcher_switch_to_view(app->view_dispatcher, CraneViewMenu);
}

// ---- scene: tx (custom view, hold-to-talk)

static void crane_tx_view_draw_callback(Canvas* canvas, void* context) {
    CraneTxModel* model = context; // view_draw passes the locked model here
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    char header[48];
    snprintf(header, sizeof(header), "%.10s [%s]", model->name, model->serial);
    canvas_draw_str(canvas, 0, 10, header);
    if(model->holding) {
        canvas_draw_str(canvas, 112, 10, "TX");
    }
    canvas_set_font(canvas, FontSecondary);
    const int visible = 4;
    int first = model->selection - visible / 2;
    if(first < 0) {
        first = 0;
    }
    if(first + visible > CRANE_CMD_COUNT) {
        first = CRANE_CMD_COUNT - visible;
    }
    for(int i = 0; i < visible; i++) {
        const int idx = first + i;
        const int y = 20 + i * 12;
        if(idx == model->selection) {
            canvas_draw_box(canvas, 0, y - 8, 122, 11);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 2, y, crane_cmds[idx].name);
        canvas_set_color(canvas, ColorBlack);
    }
}

static bool crane_tx_view_input_callback(InputEvent* event, void* context) {
    CraneApp* app = context;
    bool consumed = false;

    if(event->key == InputKeyBack) {
        if(event->type == InputTypeShort || event->type == InputTypeLong) {
            crane_worker_mode = CraneModeIdle;
            crane_holding = false;
            crane_scene_rc_list(app);
            consumed = true;
        }
        return consumed;
    }

    with_view_model(
        app->tx_view,
        CraneTxModel * model,
        {
            if(event->type == InputTypePress) {
                if(event->key == InputKeyOk) {
                    if(model->selection == CRANE_CMD_EDIT) {
                        crane_scene_edit(app);
                    } else if(model->selection == CRANE_CMD_DELETE) {
                        crane_scene_confirm_delete(app);
                    } else if(crane_profile(crane_rcs[app->current_rc].serial, &crane_tx_profile)) {
                        crane_tx_mask = crane_cmds[model->selection].mask;
                        model->holding = true;
                        crane_holding = true;
                        crane_worker_mode = CraneModeTx;
                    } else {
                        model->valid = false; // unparsable serial: refuse to transmit
                    }
                    consumed = true;
                }
            } else if(event->type == InputTypeRelease) {
                if(event->key == InputKeyOk) {
                    // stay in TX mode so the worker can send the idle tail; it drops the
                    // radio itself afterward, and leaving the scene sets the mode to idle
                    model->holding = false;
                    crane_holding = false;
                    consumed = true;
                }
            } else if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
                if(event->key == InputKeyUp && model->selection > 0) {
                    model->selection--;
                    consumed = true;
                } else if(event->key == InputKeyDown && model->selection < CRANE_CMD_COUNT - 1) {
                    model->selection++;
                    consumed = true;
                }
            }
        },
        false);
    return consumed;
}

static void crane_scene_tx(CraneApp* app) {
    crane_worker_mode = CraneModeIdle;
    crane_holding = false;
    const CraneRc* rc = &crane_rcs[app->current_rc];
    crane_dbgf("scene: tx rc=%s", rc->serial);
    with_view_model(
        app->tx_view,
        CraneTxModel * model,
        {
            strncpy(model->name, rc->name, CRANE_NAME_LEN - 1);
            model->name[CRANE_NAME_LEN - 1] = 0;
            strncpy(model->serial, rc->serial, CRANE_SERIAL_LEN - 1);
            model->serial[CRANE_SERIAL_LEN - 1] = 0;
            model->selection = 0;
            model->holding = false;
            model->valid = true;
        },
        false);
    app->current_scene = CraneSceneTx;
    view_dispatcher_switch_to_view(app->view_dispatcher, CraneViewTx);
}

// ---- scene: edit

static void crane_scene_edit(CraneApp* app) {
    const CraneRc* rc = &crane_rcs[app->current_rc];
    crane_dbg("scene: edit");
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Edit RC");
    char item[64];
    snprintf(item, sizeof(item), "Name: %s", rc->name);
    submenu_add_item(app->submenu, item, 1, crane_submenu_edit_callback, app);
    snprintf(item, sizeof(item), "Serial: %s", rc->serial);
    submenu_add_item(app->submenu, item, 2, crane_submenu_edit_callback, app);
    app->current_scene = CraneSceneEdit;
    view_dispatcher_switch_to_view(app->view_dispatcher, CraneViewMenu);
}

static void crane_submenu_edit_callback(void* context, uint32_t index) {
    CraneApp* app = context;
    if(index == 1) {
        crane_app->ti_mode = CraneTiEditName;
        strcpy(app->edit_buffer, crane_rcs[app->current_rc].name);
        text_input_reset(app->text_input);
        text_input_set_header_text(app->text_input, "RC name");
        text_input_set_result_callback(
            app->text_input,
            crane_text_input_callback,
            app,
            app->edit_buffer,
            CRANE_NAME_LEN,
            false);
    } else {
        crane_app->ti_mode = CraneTiEditSerial;
        strcpy(app->edit_buffer, crane_rcs[app->current_rc].serial);
        text_input_reset(app->text_input);
        text_input_set_header_text(app->text_input, "Serial: C + chan + 6 digits");
        text_input_set_minimum_length(app->text_input, 8);
        text_input_set_validator(app->text_input, crane_serial_validator, NULL);
        text_input_set_result_callback(
            app->text_input, crane_text_input_callback, app, app->edit_buffer, 9, false);
    }
    app->current_scene = CraneSceneTextInput;
    view_dispatcher_switch_to_view(app->view_dispatcher, CraneViewTextInput);
}

// ---- scene: confirm delete (yes/no)

static void crane_rc_delete(size_t idx) {
    if(idx >= crane_rc_count) {
        return;
    }
    for(size_t i = idx; i + 1 < crane_rc_count; i++) {
        crane_rcs[i] = crane_rcs[i + 1];
    }
    crane_rc_count--;
    crane_save_rcs();
}

static void crane_dialog_delete_callback(DialogExResult result, void* context) {
    CraneApp* app = context;
    if(result == DialogExResultRight) {
        // yes: delete and return to the RC list
        crane_dbgf("delete rc=%s", crane_rcs[app->current_rc].serial);
        crane_rc_delete(app->current_rc);
        crane_scene_rc_list(app);
    } else {
        // no (or center/back): return to the RC transmit screen
        crane_scene_tx(app);
    }
}

static void crane_scene_confirm_delete(CraneApp* app) {
    const CraneRc* rc = &crane_rcs[app->current_rc];
    crane_dbg("scene: confirm delete");
    snprintf(app->confirm_text, sizeof(app->confirm_text), "%.16s\n%s", rc->name, rc->serial);
    dialog_ex_reset(app->dialog);
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, crane_dialog_delete_callback);
    dialog_ex_set_header(app->dialog, "Delete RC?", 64, 2, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog, app->confirm_text, 64, 28, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(app->dialog, "No");
    dialog_ex_set_right_button_text(app->dialog, "Yes");
    app->current_scene = CraneSceneConfirmDelete;
    view_dispatcher_switch_to_view(app->view_dispatcher, CraneViewConfirmDelete);
}

// ---- scene: text input (shared: edit name/serial, new rc serial/name)

static void crane_text_input_callback(void* context) {
    CraneApp* app = context;
    if(crane_app->ti_mode == CraneTiEditName) {
        CraneRc* rc = &crane_rcs[app->current_rc];
        strncpy(rc->name, app->edit_buffer, CRANE_NAME_LEN - 1);
        rc->name[CRANE_NAME_LEN - 1] = 0;
        crane_save_rcs();
        crane_scene_edit(app);
    } else if(crane_app->ti_mode == CraneTiEditSerial) {
        CraneRc* rc = &crane_rcs[app->current_rc];
        int channel;
        int id_bytes[3];
        if(crane_parse_serial(app->edit_buffer, &channel, id_bytes)) {
            strncpy(rc->serial, app->edit_buffer, CRANE_SERIAL_LEN - 1);
            rc->serial[CRANE_SERIAL_LEN - 1] = 0;
            crane_save_rcs();
        }
        crane_scene_edit(app);
    } else if(crane_app->ti_mode == CraneTiNewSerial) {
        int channel;
        int id_bytes[3];
        if(!crane_parse_serial(app->edit_buffer, &channel, id_bytes)) {
            // invalid: back to the main menu, user retries
            crane_scene_main(app);
            return;
        }
        strcpy(crane_new_serial, app->edit_buffer);
        crane_app->ti_mode = CraneTiNewName;
        char def_name[CRANE_NAME_LEN];
        snprintf(def_name, sizeof(def_name), "rc_%d", (int)crane_rc_count + 1);
        strcpy(app->edit_buffer, def_name);
        // reset first: the serial step left a validator and a minimum length behind
        text_input_reset(app->text_input);
        text_input_set_header_text(app->text_input, "Name for the new RC");
        text_input_set_result_callback(
            app->text_input,
            crane_text_input_callback,
            app,
            app->edit_buffer,
            CRANE_NAME_LEN,
            false);
        view_dispatcher_switch_to_view(app->view_dispatcher, CraneViewTextInput);
    } else { // CraneTiNewName
        if(crane_rc_count >= CRANE_MAX_RCS) {
            // the list is full: adding another would run off the end of crane_rcs
            crane_scene_rc_list(app);
            return;
        }
        CraneRc* rc = &crane_rcs[crane_rc_count];
        strncpy(rc->name, app->edit_buffer, CRANE_NAME_LEN - 1);
        rc->name[CRANE_NAME_LEN - 1] = 0;
        strncpy(rc->serial, crane_new_serial, CRANE_SERIAL_LEN - 1);
        rc->serial[CRANE_SERIAL_LEN - 1] = 0;
        crane_rc_count++;
        crane_save_rcs();
        crane_scene_rc_list(app);
    }
}

// ---- scene: sniff (custom view)

static void crane_sniff_view_draw_callback(Canvas* canvas, void* context) {
    CraneSniffModel* model = context; // view framework passes the locked model
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    if(model->done) {
        canvas_draw_str(canvas, 0, 14, "RC found:");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 28, model->serial);
        char found[32];
        snprintf(found, sizeof(found), "radio: %s", model->radio);
        canvas_draw_str(canvas, 0, 40, found);
        canvas_draw_str(canvas, 0, 52, "OK: continue  Back: exit");
        return;
    }
    canvas_draw_str(canvas, 0, 14, "Sniffing channels...");
    canvas_set_font(canvas, FontSecondary);
    char line[48];
    snprintf(line, sizeof(line), "channel: C%1X", model->channel);
    canvas_draw_str(canvas, 0, 28, line);
    snprintf(line, sizeof(line), "radio: %s", model->radio);
    canvas_draw_str(canvas, 0, 40, line);
    canvas_draw_str(canvas, 0, 52, "Back: stop");
}

static bool crane_sniff_view_input_callback(InputEvent* event, void* context) {
    CraneApp* app = context;
    crane_dbgf("sniff input: key=%d type=%d", (int)event->key, (int)event->type);
    if(event->key == InputKeyBack &&
       (event->type == InputTypeShort || event->type == InputTypeLong)) {
        crane_worker_mode = CraneModeIdle;
        crane_sniff_done = false;
        crane_scene_main(app);
        return true;
    }
    if(event->key == InputKeyOk && event->type == InputTypeShort) {
        if(crane_sniff_done) {
            // hand the captured serial to the new-RC name flow
            crane_worker_mode = CraneModeIdle;
            strcpy(crane_new_serial, crane_sniff_serial);
            crane_app->ti_mode = CraneTiNewName;
            char def_name[CRANE_NAME_LEN];
            snprintf(def_name, sizeof(def_name), "rc_%d", (int)crane_rc_count + 1);
            strcpy(app->edit_buffer, def_name);
            text_input_reset(app->text_input);
            text_input_set_header_text(app->text_input, "Name for the new RC");
            text_input_set_result_callback(
                app->text_input,
                crane_text_input_callback,
                app,
                app->edit_buffer,
                CRANE_NAME_LEN,
                false);
            app->current_scene = CraneSceneTextInput;
            view_dispatcher_switch_to_view(app->view_dispatcher, CraneViewTextInput);
            return true;
        }
    }
    return false;
}

static void crane_scene_sniff(CraneApp* app) {
    crane_dbg("scene: sniff");
    app->current_scene = CraneSceneSniff;
    view_dispatcher_switch_to_view(app->view_dispatcher, CraneViewSniff);
}

// ---- navigation: back pops one scene; on main it exits

static bool crane_navigation_callback(void* context) {
    CraneApp* app = context;
    crane_worker_mode = CraneModeIdle;
    crane_holding = false;
    switch(app->current_scene) {
    case CraneSceneRcList:
        crane_scene_main(app);
        return true;
    case CraneSceneTx:
        crane_scene_rc_list(app);
        return true;
    case CraneSceneEdit:
        crane_scene_tx(app);
        return true;
    case CraneSceneConfirmDelete:
        crane_scene_tx(app);
        return true;
    case CraneSceneTextInput:
        // the keyboard is shared: name/serial edits came from Edit, the rest from Main
        if(app->ti_mode == CraneTiEditName || app->ti_mode == CraneTiEditSerial) {
            crane_scene_edit(app);
        } else {
            crane_scene_main(app);
        }
        return true;
    case CraneSceneSniff:
        crane_scene_main(app);
        return true;
    default:
        return false; // main menu: exit the app
    }
}

// ----------------------------------------------------------------------------
// app lifecycle
// ----------------------------------------------------------------------------

int32_t crane_remote_app(void* p) {
    UNUSED(p);
    CraneApp* app = malloc(sizeof(CraneApp));
    memset(app, 0, sizeof(CraneApp));
    crane_app = app;

    crane_dbg_init();
    crane_load_rcs();
    subghz_devices_init();
    // prefer an external CC1101 board; the loader falls back to the internal radio when
    // nothing answers, so this is just "use whatever is plugged in"
    crane_radio_apply(SubGhzRadioDeviceTypeExternalCC1101);
    crane_dbgf("app: start rcs=%d", (int)crane_rc_count);
    FURI_LOG_I(TAG, "app: starting with %d rcs", (int)crane_rc_count);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->text_input = text_input_alloc();
    app->dialog = dialog_ex_alloc();

    app->tx_view = view_alloc();
    view_allocate_model(app->tx_view, ViewModelTypeLocking, sizeof(CraneTxModel));
    view_set_draw_callback(app->tx_view, crane_tx_view_draw_callback);
    view_set_input_callback(app->tx_view, crane_tx_view_input_callback);
    view_set_context(app->tx_view, app);

    app->sniff_view = view_alloc();
    view_allocate_model(app->sniff_view, ViewModelTypeLocking, sizeof(CraneSniffModel));
    view_set_draw_callback(app->sniff_view, crane_sniff_view_draw_callback);
    view_set_input_callback(app->sniff_view, crane_sniff_view_input_callback);
    view_set_context(app->sniff_view, app);

    view_dispatcher_add_view(app->view_dispatcher, CraneViewMenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(app->view_dispatcher, CraneViewTx, app->tx_view);
    view_dispatcher_add_view(
        app->view_dispatcher, CraneViewConfirmDelete, dialog_ex_get_view(app->dialog));
    view_dispatcher_add_view(
        app->view_dispatcher, CraneViewTextInput, text_input_get_view(app->text_input));
    view_dispatcher_add_view(app->view_dispatcher, CraneViewSniff, app->sniff_view);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, crane_navigation_callback);

    app->worker = furi_thread_alloc_ex("CraneWorker", 4096, crane_worker_fn, NULL);
    furi_thread_start(app->worker);

    crane_scene_main(app);
    view_dispatcher_run(app->view_dispatcher);

    crane_exit = true;
    furi_thread_join(app->worker);
    furi_thread_free(app->worker);

    view_dispatcher_remove_view(app->view_dispatcher, CraneViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, CraneViewTx);
    view_dispatcher_remove_view(app->view_dispatcher, CraneViewConfirmDelete);
    view_dispatcher_remove_view(app->view_dispatcher, CraneViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, CraneViewSniff);
    view_dispatcher_free(app->view_dispatcher);
    submenu_free(app->submenu);
    text_input_free(app->text_input);
    dialog_ex_free(app->dialog);
    radio_device_loader_end(crane_radio);
    subghz_devices_deinit();
    crane_radio = NULL;
    furi_record_close(RECORD_GUI);
    free(app);
    crane_app = NULL;
    return 0;
}
