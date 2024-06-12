#include "nfc_types.h"

const char* nfc_get_dev_type(FurryHalNfcType type) {
    if(type == FurryHalNfcTypeA) {
        return "NFC-A";
    } else if(type == FurryHalNfcTypeB) {
        return "NFC-B";
    } else if(type == FurryHalNfcTypeF) {
        return "NFC-F";
    } else if(type == FurryHalNfcTypeV) {
        return "NFC-V";
    } else {
        return "Unknown";
    }
}

const char* nfc_guess_protocol(NfcProtocol protocol) {
    if(protocol == NfcDeviceProtocolMifareClassic) {
        return "Mifare Classic";
    } else {
        return "Unrecognized";
    }
}

const char* nfc_mf_classic_type(MfClassicType type) {
    if(type == MfClassicTypeMini) {
        return "Mifare Mini 0.3K";
    } else if(type == MfClassicType1k) {
        return "Mifare Classic 1K";
    } else if(type == MfClassicType4k) {
        return "Mifare Classic 4K";
    } else {
        return "Mifare Classic";
    }
}
