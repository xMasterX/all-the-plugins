#include "eink_waveshare_config.h"

#define TAG "WSH_Config"

EinkScreenTypeWaveshare
    eink_waveshare_config_translate_screen_type_to_protocol(NfcEinkScreenType common_screen_type) {
    switch(common_screen_type) {
    case NfcEinkScreenTypeWaveshare2Color2n13inch:
        return EinkScreenTypeWaveshare2n13inch;
    case NfcEinkScreenTypeWaveshare2Color2n7inch:
        return EinkScreenTypeWaveshare2n7inch;
    case NfcEinkScreenTypeWaveshare2Color2n9inch:
        return EinkScreenTypeWaveshare2n9inch;
    case NfcEinkScreenTypeWaveshare2Color4n2inch:
        return EinkScreenTypeWaveshare4n2inch;
    case NfcEinkScreenTypeWaveshare2Color7n5inch:
        return EinkScreenTypeWaveshare7n5inch;
    default:
        FURI_LOG_E(TAG, "Unknown waveshare screen type 0x%02X", common_screen_type);
        furi_crash();
    }
}

NfcEinkScreenType eink_waveshare_config_translate_protocol_to_screen_type(
    EinkScreenTypeWaveshare protocol_screen_type) {
    NfcEinkScreenType common_type = NfcEinkScreenTypeUnknown;

    if(protocol_screen_type == EinkScreenTypeWaveshare2n13inch) {
        common_type = NfcEinkScreenTypeWaveshare2Color2n13inch;
    } else if(protocol_screen_type == EinkScreenTypeWaveshare2n9inch) {
        common_type = NfcEinkScreenTypeWaveshare2Color2n9inch;
    } else if(protocol_screen_type == EinkScreenTypeWaveshare2n7inch) {
        common_type = NfcEinkScreenTypeWaveshare2Color2n7inch;
    } else if(protocol_screen_type == EinkScreenTypeWaveshare4n2inch) {
        common_type = NfcEinkScreenTypeWaveshare2Color4n2inch;
    } else if(protocol_screen_type == EinkScreenTypeWaveshare7n5inch) {
        common_type = NfcEinkScreenTypeWaveshare2Color7n5inch;
    }

    return common_type;
}
