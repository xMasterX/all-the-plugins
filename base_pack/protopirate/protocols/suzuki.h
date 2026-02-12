#pragma once

#include <furi.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/types.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <flipper_format/flipper_format.h>

#include "../defines.h"

#define SUZUKI_PROTOCOL_NAME "Suzuki"

extern const SubGhzProtocol suzuki_protocol;

// Decoder functions
void* subghz_protocol_decoder_suzuki_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_suzuki_free(void* context);
void subghz_protocol_decoder_suzuki_reset(void* context);
void subghz_protocol_decoder_suzuki_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_suzuki_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_suzuki_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_suzuki_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_suzuki_get_string(void* context, FuriString* output);

// Encoder functions
void* subghz_protocol_encoder_suzuki_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_suzuki_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_suzuki_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_suzuki_stop(void* context);
LevelDuration subghz_protocol_encoder_suzuki_yield(void* context);
