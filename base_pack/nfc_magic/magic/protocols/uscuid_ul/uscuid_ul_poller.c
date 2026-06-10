#include "uscuid_ul_poller_i.h"

#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <furi/furi.h>

#define TAG "USCUID_UL_POLLER"
#define USCUID_UL_POLLER_THREAD_FLAG_DONE (1U << 0)

typedef NfcCommand (*UscuidUlPollerStateHandler)(UscuidUlPoller* instance);

// Backdoor wakeups, probed in order; whichever ACKs is recorded.
static const UscuidUlWakeup uscuid_ul_wakeup_variants[] = {UscuidUlWakeupA, UscuidUlWakeupB};

static void uscuid_ul_poller_config_nfc(Nfc* nfc) {
    nfc_config(nfc, NfcModePoller, NfcTechIso14443a);
    nfc_set_guard_time_us(nfc, ISO14443_3A_GUARD_TIME_US);
    nfc_set_fdt_poll_fc(nfc, ISO14443_3A_FDT_POLL_FC);
    nfc_set_fdt_poll_poll_us(nfc, ISO14443_3A_POLL_POLL_MIN_US);
}

UscuidUlPoller* uscuid_ul_poller_alloc(Nfc* nfc) {
    furi_assert(nfc);

    UscuidUlPoller* instance = malloc(sizeof(UscuidUlPoller));
    instance->nfc = nfc;
    uscuid_ul_poller_config_nfc(nfc);

    instance->tx_buffer = bit_buffer_alloc(USCUID_UL_POLLER_MAX_BUFFER_SIZE);
    instance->rx_buffer = bit_buffer_alloc(USCUID_UL_POLLER_MAX_BUFFER_SIZE);

    instance->event.data = &instance->event_data;
    instance->state = UscuidUlPollerStateIdle;
    instance->session_state = UscuidUlPollerSessionStateIdle;
    instance->wakeup = UscuidUlWakeupNone;

    return instance;
}

void uscuid_ul_poller_free(UscuidUlPoller* instance) {
    furi_assert(instance);

    bit_buffer_free(instance->tx_buffer);
    bit_buffer_free(instance->rx_buffer);
    free(instance);
}

// --- Detection (standalone, own raw session; reuses the poller primitives) ---

typedef struct {
    UscuidUlPoller* poller;
    FuriThreadId thread_id;
    UscuidUlData* result;
} UscuidUlDetectContext;

static NfcCommand uscuid_ul_poller_detect_callback(NfcEvent event, void* context) {
    furi_assert(context);
    UscuidUlDetectContext* ctx = context;

    if(event.type == NfcEventTypePollerReady) {
        for(size_t i = 0; i < COUNT_OF(uscuid_ul_wakeup_variants); i++) {
            const UscuidUlWakeup variant = uscuid_ul_wakeup_variants[i];
            if(uscuid_ul_poller_wakeup(ctx->poller, variant) != UscuidUlPollerErrorNone) {
                continue;
            }
            uint8_t config[USCUID_UL_CONFIG_SIZE];
            if(uscuid_ul_poller_read_config(ctx->poller, config) != UscuidUlPollerErrorNone) {
                continue;
            }
            if(config[0] != USCUID_UL_CONFIG_MAGIC) {
                continue;
            }
            ctx->result->is_uscuid_ul = true;
            ctx->result->wakeup = variant;
            memcpy(ctx->result->config, config, USCUID_UL_CONFIG_SIZE);
            uscuid_ul_classify(config, ctx->result);
            break;
        }
    }

    furi_thread_flags_set(ctx->thread_id, USCUID_UL_POLLER_THREAD_FLAG_DONE);
    return NfcCommandStop;
}

UscuidUlPollerError uscuid_ul_poller_detect(Nfc* nfc, UscuidUlData* data) {
    furi_assert(nfc);
    furi_assert(data);

    memset(data, 0, sizeof(UscuidUlData));
    data->wakeup = UscuidUlWakeupNone;
    data->type = MfUltralightTypeOrigin;
    // TODO(increment 2): set maybe_ul5 from the normal-activation UID prefix AA 55
    // in the family-first scanner (UL-5 config is locked, so it cannot be read here).

    UscuidUlDetectContext ctx = {
        .poller = uscuid_ul_poller_alloc(nfc),
        .thread_id = furi_thread_get_current_id(),
        .result = data,
    };

    nfc_start(nfc, uscuid_ul_poller_detect_callback, &ctx);
    furi_thread_flags_wait(USCUID_UL_POLLER_THREAD_FLAG_DONE, FuriFlagWaitAny, FuriWaitForever);
    furi_thread_flags_clear(USCUID_UL_POLLER_THREAD_FLAG_DONE);
    nfc_stop(nfc);

    uscuid_ul_poller_free(ctx.poller);

    return data->is_uscuid_ul ? UscuidUlPollerErrorNone : UscuidUlPollerErrorNotPresent;
}

const char* uscuid_ul_get_variant_name(const UscuidUlData* data) {
    furi_assert(data);

    if(!data->is_uscuid_ul || !data->type_known) {
        return "Unknown";
    }

    switch(data->type) {
    case MfUltralightTypeUL11:
        return "UL11";
    case MfUltralightTypeUL21:
        return data->is_ultra ? "UL21 (Ultra)" : "UL21";
    case MfUltralightTypeNTAG213:
        return "NTAG213";
    case MfUltralightTypeNTAG215:
        return "NTAG215";
    case MfUltralightTypeNTAG216:
        return "NTAG216";
    case MfUltralightTypeMfulC:
        return "UL-C (write N/A)";
    default:
        return "Unknown";
    }
}

// --- Write state machine (raw model, like gen1a_poller) ---

static NfcCommand uscuid_ul_poller_idle_handler(UscuidUlPoller* instance) {
    NfcCommand command = NfcCommandContinue;

    for(size_t i = 0; i < COUNT_OF(uscuid_ul_wakeup_variants); i++) {
        const UscuidUlWakeup variant = uscuid_ul_wakeup_variants[i];
        if(uscuid_ul_poller_wakeup(instance, variant) == UscuidUlPollerErrorNone) {
            instance->wakeup = variant;
            instance->event.type = UscuidUlPollerEventTypeDetected;
            command = instance->callback(instance->event, instance->context);
            instance->state = UscuidUlPollerStateRequestMode;
            break;
        }
    }
    // If neither wakeup answered, stay idle and retry on the next poll.

    return command;
}

static NfcCommand uscuid_ul_poller_request_mode_handler(UscuidUlPoller* instance) {
    instance->event.type = UscuidUlPollerEventTypeRequestMode;
    NfcCommand command = instance->callback(instance->event, instance->context);
    // Only Write is supported for now.
    instance->state = UscuidUlPollerStateRequestDataToWrite;
    return command;
}

static NfcCommand uscuid_ul_poller_request_data_handler(UscuidUlPoller* instance) {
    // Zero the out-parameter so a scene that forgets to set it can't leave a stale value.
    instance->event_data.data_to_write.data = NULL;
    instance->event.type = UscuidUlPollerEventTypeRequestDataToWrite;
    NfcCommand command = instance->callback(instance->event, instance->context);

    instance->data = instance->event_data.data_to_write.data;
    instance->write_index = 0;
    instance->written = 0;
    instance->failed_page = 0xFFFF;
    instance->pages_total = (instance->data != NULL) ? instance->data->pages_read : 0;

    if(instance->data == NULL || instance->pages_total == 0) {
        // Nothing to write is a failure, not a no-op "success".
        FURI_LOG_E(TAG, "No source data to write");
        instance->state = UscuidUlPollerStateFail;
    } else {
        instance->state = UscuidUlPollerStateWrite;
    }
    return command;
}

static NfcCommand uscuid_ul_poller_write_handler(UscuidUlPoller* instance) {
    NfcCommand command = NfcCommandContinue;

    if(instance->write_index >= instance->pages_total) {
        instance->state = UscuidUlPollerStateSuccess;
        return command;
    }

    const uint8_t page =
        uscuid_ul_poller_page_for_index(instance->write_index, instance->pages_total);
    const uint8_t* src = instance->data->page[page].data;

    do {
        if(uscuid_ul_poller_write_page(instance, page, src) != UscuidUlPollerErrorNone) {
            FURI_LOG_E(TAG, "Write failed at page %u", page);
            instance->failed_page = page;
            instance->state = UscuidUlPollerStateFail;
            break;
        }

        // Read-back verify: compare the 4 bytes we just wrote.
        uint8_t readback[MF_ULTRALIGHT_PAGE_SIZE * 4];
        if(uscuid_ul_poller_read_page(instance, page, readback) != UscuidUlPollerErrorNone ||
           memcmp(readback, src, MF_ULTRALIGHT_PAGE_SIZE) != 0) {
            FURI_LOG_E(TAG, "Read-back mismatch at page %u", page);
            instance->failed_page = page;
            instance->state = UscuidUlPollerStateFail;
            break;
        }

        instance->written++;
        instance->write_index++;

        instance->event.type = UscuidUlPollerEventTypeWriteProgress;
        instance->event_data.write_progress.pages_written = instance->written;
        instance->event_data.write_progress.pages_total = instance->pages_total;
        command = instance->callback(instance->event, instance->context);
    } while(false);

    return command;
}

static NfcCommand uscuid_ul_poller_success_handler(UscuidUlPoller* instance) {
    instance->event.type = UscuidUlPollerEventTypeSuccess;
    NfcCommand command = instance->callback(instance->event, instance->context);
    instance->state = UscuidUlPollerStateIdle;
    return command;
}

static NfcCommand uscuid_ul_poller_fail_handler(UscuidUlPoller* instance) {
    instance->event.type = UscuidUlPollerEventTypeFail;
    instance->event_data.fail.pages_written = instance->written;
    instance->event_data.fail.failed_page = instance->failed_page;
    NfcCommand command = instance->callback(instance->event, instance->context);
    instance->state = UscuidUlPollerStateIdle;
    return command;
}

static const UscuidUlPollerStateHandler uscuid_ul_poller_state_handlers[UscuidUlPollerStateNum] = {
    [UscuidUlPollerStateIdle] = uscuid_ul_poller_idle_handler,
    [UscuidUlPollerStateRequestMode] = uscuid_ul_poller_request_mode_handler,
    [UscuidUlPollerStateRequestDataToWrite] = uscuid_ul_poller_request_data_handler,
    [UscuidUlPollerStateWrite] = uscuid_ul_poller_write_handler,
    [UscuidUlPollerStateSuccess] = uscuid_ul_poller_success_handler,
    [UscuidUlPollerStateFail] = uscuid_ul_poller_fail_handler,
};

static NfcCommand uscuid_ul_poller_run(NfcEvent event, void* context) {
    furi_assert(context);
    UscuidUlPoller* instance = context;
    NfcCommand command = NfcCommandContinue;

    if(event.type == NfcEventTypePollerReady) {
        command = uscuid_ul_poller_state_handlers[instance->state](instance);
    }

    if(instance->session_state == UscuidUlPollerSessionStateStopRequest) {
        command = NfcCommandStop;
    }

    return command;
}

void uscuid_ul_poller_start(
    UscuidUlPoller* instance,
    UscuidUlPollerCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(callback);

    instance->callback = callback;
    instance->context = context;
    instance->state = UscuidUlPollerStateIdle;
    instance->session_state = UscuidUlPollerSessionStateStarted;
    nfc_start(instance->nfc, uscuid_ul_poller_run, instance);
}

void uscuid_ul_poller_stop(UscuidUlPoller* instance) {
    furi_assert(instance);

    instance->session_state = UscuidUlPollerSessionStateStopRequest;
    nfc_stop(instance->nfc);
    instance->session_state = UscuidUlPollerSessionStateIdle;
}
