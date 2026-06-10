#pragma once

#include <nfc/nfc.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USCUID_UL_CONFIG_SIZE (16)

typedef enum {
    UscuidUlPollerErrorNone,
    UscuidUlPollerErrorTimeout,
    UscuidUlPollerErrorNotPresent,
    UscuidUlPollerErrorProtocol,
} UscuidUlPollerError;

// Which magic backdoor wakeup the card answered. The poller probes A (40/43) then B
// (20/23) and records whichever ACKs; it does NOT read a wakeup-selector byte from config
// (detection happens before config is read). None == no backdoor answered.
typedef enum {
    UscuidUlWakeupNone,
    UscuidUlWakeupA,
    UscuidUlWakeupB,
} UscuidUlWakeup;

// Result of non-destructive detection / variation check.
typedef struct {
    bool is_uscuid_ul; // confirmed: config page read and starts with 0x85
    uint8_t config[USCUID_UL_CONFIG_SIZE];
    UscuidUlWakeup wakeup; // backdoor entry that answered (or None)
    MfUltralightType type; // emulated type from preset byte cfg[7] (vendor cfg[9] only refines UL21 -> Ultra)
    bool type_known; // preset byte recognised
    bool is_ultra; // UL21 + Mikron vendor (cfg[9]==0x34) -> display "UL21 (Ultra)"
    bool maybe_ul5; // unpersonalized UL-5 hint (UID prefix AA 55); detect-only, never writable
} UscuidUlData;

typedef enum {
    UscuidUlPollerModeWrite,
    // Wipe / Dump to be added later (for certain cases)
} UscuidUlPollerMode;

typedef enum {
    UscuidUlPollerEventTypeDetected,
    UscuidUlPollerEventTypeRequestMode,
    UscuidUlPollerEventTypeRequestDataToWrite,
    UscuidUlPollerEventTypeWriteProgress,
    UscuidUlPollerEventTypeSuccess,
    UscuidUlPollerEventTypeFail,
} UscuidUlPollerEventType;

typedef struct {
    UscuidUlPollerMode mode;
} UscuidUlPollerEventDataRequestMode;

typedef struct {
    const MfUltralightData* data; // source dump (not owned; lives in the app device)
} UscuidUlPollerEventDataRequestDataToWrite;

typedef struct {
    uint16_t pages_written;
    uint16_t pages_total;
} UscuidUlPollerEventDataWriteProgress;

typedef struct {
    uint16_t pages_written; // pages successfully written before the failure
    uint16_t pages_total; // pages that were to be written
    uint16_t failed_page; // page whose write or read-back failed (0xFFFF if not page-specific)
} UscuidUlPollerEventDataFail;

typedef union {
    UscuidUlPollerEventDataRequestMode poller_mode;
    UscuidUlPollerEventDataRequestDataToWrite data_to_write;
    UscuidUlPollerEventDataWriteProgress write_progress;
    UscuidUlPollerEventDataFail fail;
} UscuidUlPollerEventData;

typedef struct {
    UscuidUlPollerEventType type;
    UscuidUlPollerEventData* data;
} UscuidUlPollerEvent;

typedef NfcCommand (*UscuidUlPollerCallback)(UscuidUlPollerEvent event, void* context);

typedef struct UscuidUlPoller UscuidUlPoller;

// Non-destructive: tries magic wakeup (A then B) + config read, classifies the variant,
// and sets the AA-55 UL-5 hint. Returns None when a USCUID-UL is confirmed.
UscuidUlPollerError uscuid_ul_poller_detect(Nfc* nfc, UscuidUlData* data);

// UI label for the variant: "UL11", "UL21", "UL21 (Ultra)", "NTAG213/215/216",
// "UL-C (write N/A)", or "Unknown". Returns a static string.
const char* uscuid_ul_get_variant_name(const UscuidUlData* data);

// True when this detected tag can be written: a confirmed USCUID-UL with a recognised,
// supported type. UL-C is display-only ("write N/A"); an unrecognised preset is not writable.
bool uscuid_ul_data_is_writable(const UscuidUlData* data);

UscuidUlPoller* uscuid_ul_poller_alloc(Nfc* nfc);
void uscuid_ul_poller_free(UscuidUlPoller* instance);

// Selects the write transport before start: pass the wakeup recorded during detection.
// None -> direct (normal activation + A2, for CUID/ATS tags); A/B -> backdoor (40/43-20/23).
void uscuid_ul_poller_set_wakeup(UscuidUlPoller* instance, UscuidUlWakeup wakeup);
void uscuid_ul_poller_start(
    UscuidUlPoller* instance,
    UscuidUlPollerCallback callback,
    void* context);
void uscuid_ul_poller_stop(UscuidUlPoller* instance);

#ifdef __cplusplus
}
#endif
