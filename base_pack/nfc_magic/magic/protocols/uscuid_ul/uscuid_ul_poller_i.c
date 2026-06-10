#include "uscuid_ul_poller_i.h"

#include <nfc/helpers/iso14443_crc.h>
#include <furi/furi.h>

#define TAG "USCUID_UL_POLLER"

#define USCUID_UL_READ_RESPONSE_SIZE (MF_ULTRALIGHT_PAGE_SIZE * 4) // READ returns 4 pages

static UscuidUlPollerError uscuid_ul_process_nfc_error(NfcError error) {
    if(error == NfcErrorNone) {
        return UscuidUlPollerErrorNone;
    } else if(error == NfcErrorTimeout) {
        return UscuidUlPollerErrorTimeout;
    }
    return UscuidUlPollerErrorNotPresent;
}

static UscuidUlPollerError uscuid_ul_process_iso3_error(Iso14443_3aError error) {
    if(error == Iso14443_3aErrorNone) {
        return UscuidUlPollerErrorNone;
    } else if(error == Iso14443_3aErrorTimeout) {
        return UscuidUlPollerErrorTimeout;
    }
    return UscuidUlPollerErrorProtocol;
}

static bool uscuid_ul_rx_is_ack(const BitBuffer* rx) {
    return (bit_buffer_get_size(rx) == 4) && (bit_buffer_get_byte(rx, 0) == USCUID_UL_ACK);
}

bool uscuid_ul_is_direct(const UscuidUlPoller* instance) {
    return instance->wakeup == UscuidUlWakeupNone;
}

// Direct (no-backdoor) transport: normal-activation A2 write via the iso3 poller. Mirrors
// the firmware mf_ultralight poller — a 4-bit ACK carries no CRC, so the layer reports
// WrongCrc on success (CRC is auto-appended to TX by send_standard_frame).
static UscuidUlPollerError
    uscuid_ul_write_page_iso3(UscuidUlPoller* instance, uint8_t page, const uint8_t* data) {
    bit_buffer_reset(instance->tx_buffer);
    bit_buffer_append_byte(instance->tx_buffer, USCUID_UL_CMD_WRITE);
    bit_buffer_append_byte(instance->tx_buffer, page);
    bit_buffer_append_bytes(instance->tx_buffer, data, MF_ULTRALIGHT_PAGE_SIZE);

    Iso14443_3aError error = iso14443_3a_poller_send_standard_frame(
        instance->iso3_poller, instance->tx_buffer, instance->rx_buffer, USCUID_UL_POLLER_MAX_FWT);
    if(error != Iso14443_3aErrorWrongCrc) {
        return uscuid_ul_process_iso3_error(error);
    }
    return uscuid_ul_rx_is_ack(instance->rx_buffer) ? UscuidUlPollerErrorNone :
                                                      UscuidUlPollerErrorProtocol;
}

static UscuidUlPollerError
    uscuid_ul_read_page_iso3(UscuidUlPoller* instance, uint8_t page, uint8_t* data) {
    bit_buffer_reset(instance->tx_buffer);
    bit_buffer_append_byte(instance->tx_buffer, USCUID_UL_CMD_READ);
    bit_buffer_append_byte(instance->tx_buffer, page);

    Iso14443_3aError error = iso14443_3a_poller_send_standard_frame(
        instance->iso3_poller, instance->tx_buffer, instance->rx_buffer, USCUID_UL_POLLER_MAX_FWT);
    if(error != Iso14443_3aErrorNone) {
        return uscuid_ul_process_iso3_error(error);
    }
    if(bit_buffer_get_size_bytes(instance->rx_buffer) < USCUID_UL_READ_RESPONSE_SIZE) {
        return UscuidUlPollerErrorProtocol;
    }
    memcpy(data, bit_buffer_get_data(instance->rx_buffer), USCUID_UL_READ_RESPONSE_SIZE);
    return UscuidUlPollerErrorNone;
}

UscuidUlPollerError uscuid_ul_poller_wakeup(UscuidUlPoller* instance, UscuidUlWakeup variant) {
    furi_assert(instance);
    furi_check(variant != UscuidUlWakeupNone);

    const uint8_t wupa = (variant == UscuidUlWakeupB) ? USCUID_UL_WUPA_B : USCUID_UL_WUPA_A;
    const uint8_t data_cmd = (variant == UscuidUlWakeupB) ? USCUID_UL_DATA_B : USCUID_UL_DATA_A;

    UscuidUlPollerError ret = UscuidUlPollerErrorNone;
    do {
        // 7-bit WUPA-style backdoor entry (no CRC), like a real Gen1A.
        bit_buffer_reset(instance->tx_buffer);
        bit_buffer_set_size(instance->tx_buffer, 7);
        bit_buffer_set_byte(instance->tx_buffer, 0, wupa);
        NfcError error = nfc_poller_trx(
            instance->nfc, instance->tx_buffer, instance->rx_buffer, USCUID_UL_POLLER_MAX_FWT);
        if(error != NfcErrorNone) {
            ret = uscuid_ul_process_nfc_error(error);
            break;
        }
        if(!uscuid_ul_rx_is_ack(instance->rx_buffer)) {
            ret = UscuidUlPollerErrorProtocol;
            break;
        }

        // Data-access command (single byte, no CRC).
        bit_buffer_reset(instance->tx_buffer);
        bit_buffer_append_byte(instance->tx_buffer, data_cmd);
        error = nfc_poller_trx(
            instance->nfc, instance->tx_buffer, instance->rx_buffer, USCUID_UL_POLLER_MAX_FWT);
        if(error != NfcErrorNone) {
            ret = uscuid_ul_process_nfc_error(error);
            break;
        }
        if(!uscuid_ul_rx_is_ack(instance->rx_buffer)) {
            ret = UscuidUlPollerErrorProtocol;
            break;
        }
    } while(false);

    return ret;
}

UscuidUlPollerError uscuid_ul_poller_read_config(UscuidUlPoller* instance, uint8_t* config) {
    furi_assert(instance);
    furi_assert(config);

    UscuidUlPollerError ret = UscuidUlPollerErrorNone;
    do {
        bit_buffer_reset(instance->tx_buffer);
        bit_buffer_append_byte(instance->tx_buffer, USCUID_UL_CMD_READ_CFG_0);
        bit_buffer_append_byte(instance->tx_buffer, USCUID_UL_CMD_READ_CFG_1);
        iso14443_crc_append(Iso14443CrcTypeA, instance->tx_buffer);

        NfcError error = nfc_poller_trx(
            instance->nfc, instance->tx_buffer, instance->rx_buffer, USCUID_UL_POLLER_MAX_FWT);
        if(error != NfcErrorNone) {
            ret = uscuid_ul_process_nfc_error(error);
            break;
        }
        // 16 config bytes (+2 CRC); accept >= 16 and take the first 16.
        if(bit_buffer_get_size_bytes(instance->rx_buffer) < USCUID_UL_CONFIG_SIZE) {
            ret = UscuidUlPollerErrorProtocol;
            break;
        }
        memcpy(config, bit_buffer_get_data(instance->rx_buffer), USCUID_UL_CONFIG_SIZE);
    } while(false);

    return ret;
}

UscuidUlPollerError
    uscuid_ul_poller_write_page(UscuidUlPoller* instance, uint8_t page, const uint8_t* data) {
    furi_assert(instance);
    furi_assert(data);

    if(uscuid_ul_is_direct(instance)) {
        return uscuid_ul_write_page_iso3(instance, page, data);
    }

    UscuidUlPollerError ret = UscuidUlPollerErrorNone;
    do {
        bit_buffer_reset(instance->tx_buffer);
        bit_buffer_append_byte(instance->tx_buffer, USCUID_UL_CMD_WRITE);
        bit_buffer_append_byte(instance->tx_buffer, page);
        bit_buffer_append_bytes(instance->tx_buffer, data, MF_ULTRALIGHT_PAGE_SIZE);
        iso14443_crc_append(Iso14443CrcTypeA, instance->tx_buffer);

        NfcError error = nfc_poller_trx(
            instance->nfc, instance->tx_buffer, instance->rx_buffer, USCUID_UL_POLLER_MAX_FWT);
        if(error != NfcErrorNone) {
            ret = uscuid_ul_process_nfc_error(error);
            break;
        }
        if(!uscuid_ul_rx_is_ack(instance->rx_buffer)) {
            ret = UscuidUlPollerErrorProtocol;
            break;
        }
    } while(false);

    return ret;
}

UscuidUlPollerError
    uscuid_ul_poller_read_page(UscuidUlPoller* instance, uint8_t page, uint8_t* data) {
    furi_assert(instance);
    furi_assert(data);

    if(uscuid_ul_is_direct(instance)) {
        return uscuid_ul_read_page_iso3(instance, page, data);
    }

    UscuidUlPollerError ret = UscuidUlPollerErrorNone;
    do {
        bit_buffer_reset(instance->tx_buffer);
        bit_buffer_append_byte(instance->tx_buffer, USCUID_UL_CMD_READ);
        bit_buffer_append_byte(instance->tx_buffer, page);
        iso14443_crc_append(Iso14443CrcTypeA, instance->tx_buffer);

        NfcError error = nfc_poller_trx(
            instance->nfc, instance->tx_buffer, instance->rx_buffer, USCUID_UL_POLLER_MAX_FWT);
        if(error != NfcErrorNone) {
            ret = uscuid_ul_process_nfc_error(error);
            break;
        }
        // READ returns 4 pages (+CRC).
        if(bit_buffer_get_size_bytes(instance->rx_buffer) < MF_ULTRALIGHT_PAGE_SIZE * 4) {
            ret = UscuidUlPollerErrorProtocol;
            break;
        }
        memcpy(data, bit_buffer_get_data(instance->rx_buffer), MF_ULTRALIGHT_PAGE_SIZE * 4);
    } while(false);

    return ret;
}

uint8_t uscuid_ul_poller_page_for_index(uint16_t index, uint16_t pages_total) {
    // Low pages 0..3 (or fewer for a tiny dump) are written last, descending.
    const uint16_t low_count = (pages_total < 4) ? pages_total : 4;
    const uint16_t ascending_count = pages_total - low_count; // pages 4..total-1

    if(index < ascending_count) {
        return (uint8_t)(4 + index);
    }
    const uint16_t tail = index - ascending_count; // 0..low_count-1
    return (uint8_t)((low_count - 1) - tail); // 3,2,1,0
}

void uscuid_ul_classify(const uint8_t* config, UscuidUlData* data) {
    furi_assert(config);
    furi_assert(data);

    data->type_known = true;
    data->is_ultra = false;

    switch(config[USCUID_UL_CFG_PRESET]) {
    case USCUID_UL_PRESET_UL11:
        data->type = MfUltralightTypeUL11;
        break;
    case USCUID_UL_PRESET_UL21:
        data->type = MfUltralightTypeUL21;
        data->is_ultra = (config[USCUID_UL_CFG_VENDOR] == USCUID_UL_VENDOR_MIKRON);
        break;
    case USCUID_UL_PRESET_NTAG213:
        data->type = MfUltralightTypeNTAG213;
        break;
    case USCUID_UL_PRESET_NTAG215:
        data->type = MfUltralightTypeNTAG215;
        break;
    case USCUID_UL_PRESET_NTAG216:
        data->type = MfUltralightTypeNTAG216;
        break;
    case USCUID_UL_PRESET_ULC:
        data->type = MfUltralightTypeMfulC;
        break;
    default:
        data->type_known = false;
        data->type = MfUltralightTypeOrigin;
        break;
    }
}
