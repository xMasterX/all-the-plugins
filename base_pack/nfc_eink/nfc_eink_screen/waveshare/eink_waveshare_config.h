#pragma once
#include "eink_waveshare_i.h"

typedef enum {
    EinkScreenTypeWaveshare2n13inch = 4,
    EinkScreenTypeWaveshare2n7inch = 16,
    EinkScreenTypeWaveshare2n9inch = 7,
    EinkScreenTypeWaveshare4n2inch = 10,
    EinkScreenTypeWaveshare7n5inchHD = 12,
    EinkScreenTypeWaveshare7n5inch = 14,
} EinkScreenTypeWaveshare;

EinkScreenTypeWaveshare
    eink_waveshare_config_translate_screen_type_to_protocol(NfcEinkScreenType common_screen_type);
NfcEinkScreenType eink_waveshare_config_translate_protocol_to_screen_type(
    EinkScreenTypeWaveshare protocol_screen_type);
