#pragma once

#include "uscuid_ul_poller.h"
#include <nfc/nfc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USCUID_UL_POLLER_MAX_BUFFER_SIZE (64U)
#define USCUID_UL_POLLER_MAX_FWT (60000U)

// Magic wakeup, raw frames like a real Gen1A chip (see gen1a_poller).
#define USCUID_UL_WUPA_A (0x40) // 7-bit
#define USCUID_UL_DATA_A (0x43)
#define USCUID_UL_WUPA_B (0x20) // 7-bit
#define USCUID_UL_DATA_B (0x23)
#define USCUID_UL_ACK (0x0A) // 4-bit

// Commands available once in backdoor mode (same opcodes as plain UL).
#define USCUID_UL_CMD_READ (0x30) // 30 <page>          -> 16 bytes
#define USCUID_UL_CMD_WRITE (0xA2) // A2 <page> <4 bytes> -> ACK
// Read configuration. Doc lists E0 50; the identify example shows E1 00 -- which is
// canonical needs confirming on hardware (TMD-5S). Kept here so it is a one-line change.
#define USCUID_UL_CMD_READ_CFG_0 (0xE0)
#define USCUID_UL_CMD_READ_CFG_1 (0x50)

#define USCUID_UL_CONFIG_MAGIC (0x85) // config[0]
#define USCUID_UL_CFG_PRESET (7) // C3=UL11 3C=UL21 A5=NTAG213 5A=NTAG215 AA=NTAG216 00=UL-C
#define USCUID_UL_CFG_VENDOR (9) // version vendor byte: 04=NXP, 34=Mikron (Ultra)
#define USCUID_UL_VENDOR_MIKRON (0x34)

#define USCUID_UL_PRESET_UL11 (0xC3)
#define USCUID_UL_PRESET_UL21 (0x3C)
#define USCUID_UL_PRESET_ULC (0x00)
#define USCUID_UL_PRESET_NTAG213 (0xA5)
#define USCUID_UL_PRESET_NTAG215 (0x5A)
#define USCUID_UL_PRESET_NTAG216 (0xAA)

typedef enum {
    UscuidUlPollerStateIdle,
    UscuidUlPollerStateRequestMode,
    UscuidUlPollerStateRequestDataToWrite,
    UscuidUlPollerStateWrite,
    UscuidUlPollerStateSuccess,
    UscuidUlPollerStateFail,

    UscuidUlPollerStateNum,
} UscuidUlPollerState;

typedef enum {
    UscuidUlPollerSessionStateIdle,
    UscuidUlPollerSessionStateStarted,
    UscuidUlPollerSessionStateStopRequest,
} UscuidUlPollerSessionState;

struct UscuidUlPoller {
    Nfc* nfc;
    UscuidUlPollerState state;
    UscuidUlPollerSessionState session_state;

    UscuidUlWakeup wakeup; // backdoor entry resolved at session start
    const MfUltralightData* data; // source dump, not owned (stable for the session)
    uint16_t pages_total; // pages to write (from the dump)
    uint16_t write_index; // 0-based write position (order: see uscuid_ul_poller_page_for_index)
    uint16_t written; // pages successfully written (and verified)
    uint16_t failed_page; // page that failed, for the Fail event

    BitBuffer* tx_buffer;
    BitBuffer* rx_buffer;

    UscuidUlPollerEvent event;
    UscuidUlPollerEventData event_data;

    UscuidUlPollerCallback callback;
    void* context;
};

// Low-level primitives (raw nfc_poller_trx + manual CRC, like gen1a_poller).
UscuidUlPollerError uscuid_ul_poller_wakeup(UscuidUlPoller* instance, UscuidUlWakeup variant);
UscuidUlPollerError uscuid_ul_poller_read_config(UscuidUlPoller* instance, uint8_t* config);
UscuidUlPollerError
    uscuid_ul_poller_write_page(UscuidUlPoller* instance, uint8_t page, const uint8_t* data);
UscuidUlPollerError
    uscuid_ul_poller_read_page(UscuidUlPoller* instance, uint8_t page, uint8_t* data);

// Maps the 0-based write position to the actual page, encoding the order
// "pages 4..total-1 ascending, then 3,2,1,0" (block 0 dead last).
uint8_t uscuid_ul_poller_page_for_index(uint16_t index, uint16_t pages_total);

// Fills type/flags from a 16-byte config page (config[0] must be 0x85).
void uscuid_ul_classify(const uint8_t* config, UscuidUlData* data);

#ifdef __cplusplus
}
#endif
