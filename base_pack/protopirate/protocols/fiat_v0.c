#include "fiat_v0.h"
#include "../protopirate_app_i.h"
#include <lib/toolbox/manchester_decoder.h>

#define TAG "FiatProtocolV0"

static const SubGhzBlockConst subghz_protocol_fiat_v0_const = {
    .te_short = 200,
    .te_long = 400,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

#define FIAT_V0_PREAMBLE_PAIRS  150
#define FIAT_V0_GAP_US          800
#define FIAT_V0_TOTAL_BURSTS    3
#define FIAT_V0_INTER_BURST_GAP 25000

struct SubGhzProtocolDecoderFiatV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester_state;
    uint8_t decoder_state;
    uint16_t preamble_count;
    uint32_t data_low;
    uint32_t data_high;
    uint8_t bit_count;
    uint32_t cnt;
    uint32_t serial;
    uint8_t btn;
    uint32_t te_last;
};

struct SubGhzProtocolEncoderFiatV0 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t cnt;
    uint32_t serial;
    uint8_t btn;
};

typedef enum {
    FiatV0DecoderStepReset = 0,
    FiatV0DecoderStepPreamble = 1,
    FiatV0DecoderStepData = 2,
} FiatV0DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_fiat_v0_decoder = {
    .alloc = subghz_protocol_decoder_fiat_v0_alloc,
    .free = subghz_protocol_decoder_fiat_v0_free,
    .feed = subghz_protocol_decoder_fiat_v0_feed,
    .reset = subghz_protocol_decoder_fiat_v0_reset,
    .get_hash_data = subghz_protocol_decoder_fiat_v0_get_hash_data,
    .serialize = subghz_protocol_decoder_fiat_v0_serialize,
    .deserialize = subghz_protocol_decoder_fiat_v0_deserialize,
    .get_string = subghz_protocol_decoder_fiat_v0_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_fiat_v0_encoder = {
    .alloc = subghz_protocol_encoder_fiat_v0_alloc,
    .free = subghz_protocol_encoder_fiat_v0_free,
    .deserialize = subghz_protocol_encoder_fiat_v0_deserialize,
    .stop = subghz_protocol_encoder_fiat_v0_stop,
    .yield = subghz_protocol_encoder_fiat_v0_yield,
};

const SubGhzProtocol fiat_protocol_v0 = {
    .name = FIAT_PROTOCOL_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_fiat_v0_decoder,
    .encoder = &subghz_protocol_fiat_v0_encoder,
};

// ============================================================================
// ENCODER IMPLEMENTATION
// ============================================================================

void* subghz_protocol_encoder_fiat_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderFiatV0* instance = malloc(sizeof(SubGhzProtocolEncoderFiatV0));

    instance->base.protocol = &fiat_protocol_v0;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 1024;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    instance->cnt = 0;
    instance->serial = 0;
    instance->btn = 0;

    return instance;
}

void subghz_protocol_encoder_fiat_v0_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderFiatV0* instance = context;
    if(instance->encoder.upload) {
        free(instance->encoder.upload);
    }
    free(instance);
}

static void subghz_protocol_encoder_fiat_v0_get_upload(SubGhzProtocolEncoderFiatV0* instance) {
    furi_check(instance);
    size_t index = 0;

    uint32_t te_short = subghz_protocol_fiat_v0_const.te_short;
    uint32_t te_long = subghz_protocol_fiat_v0_const.te_long;

    FURI_LOG_I(
        TAG,
        "Building upload: cnt=0x%08lX, serial=0x%08lX, btn=0x%02X",
        instance->cnt,
        instance->serial,
        instance->btn);

    uint64_t data = ((uint64_t)instance->cnt << 32) | instance->serial;

    // Reverse the decoder's btn fix: decoder does (x << 1) | 1
    uint8_t btn_to_send = instance->btn >> 1;

    for(uint8_t burst = 0; burst < FIAT_V0_TOTAL_BURSTS; burst++) {
        if(burst > 0) {
            instance->encoder.upload[index++] =
                level_duration_make(false, FIAT_V0_INTER_BURST_GAP);
        }

        // Preamble: HIGH-LOW pairs
        for(int i = 0; i < FIAT_V0_PREAMBLE_PAIRS; i++) {
            instance->encoder.upload[index++] = level_duration_make(true, te_short);
            instance->encoder.upload[index++] = level_duration_make(false, te_short);
        }

        // Replace last preamble LOW with gap
        instance->encoder.upload[index - 1] = level_duration_make(false, FIAT_V0_GAP_US);

        // First bit (bit 63) - special handling after gap
        bool first_bit = (data >> 63) & 1;
        if(first_bit) {
            // First bit is 1: LONG HIGH
            instance->encoder.upload[index++] = level_duration_make(true, te_long);
        } else {
            // First bit is 0: SHORT HIGH + LONG LOW
            instance->encoder.upload[index++] = level_duration_make(true, te_short);
            instance->encoder.upload[index++] = level_duration_make(false, te_long);
        }

        bool prev_bit = first_bit;

        // Encode remaining 63 data bits (bits 62:0) using differential Manchester
        for(int bit = 62; bit >= 0; bit--) {
            bool curr_bit = (data >> bit) & 1;

            if(!prev_bit && !curr_bit) {
                // 0->0: SHORT HIGH + SHORT LOW
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
                instance->encoder.upload[index++] = level_duration_make(false, te_short);
            } else if(!prev_bit && curr_bit) {
                // 0->1: LONG HIGH
                instance->encoder.upload[index++] = level_duration_make(true, te_long);
            } else if(prev_bit && !curr_bit) {
                // 1->0: LONG LOW
                instance->encoder.upload[index++] = level_duration_make(false, te_long);
            } else {
                // 1->1: SHORT LOW + SHORT HIGH
                instance->encoder.upload[index++] = level_duration_make(false, te_short);
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
            }

            prev_bit = curr_bit;
        }

        // Encode 6 btn bits using same differential pattern
        for(int bit = 5; bit >= 0; bit--) {
            bool curr_bit = (btn_to_send >> bit) & 1;

            if(!prev_bit && !curr_bit) {
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
                instance->encoder.upload[index++] = level_duration_make(false, te_short);
            } else if(!prev_bit && curr_bit) {
                instance->encoder.upload[index++] = level_duration_make(true, te_long);
            } else if(prev_bit && !curr_bit) {
                instance->encoder.upload[index++] = level_duration_make(false, te_long);
            } else {
                instance->encoder.upload[index++] = level_duration_make(false, te_short);
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
            }

            prev_bit = curr_bit;
        }

        // End marker - ensure we end with LOW
        if(prev_bit) {
            instance->encoder.upload[index++] = level_duration_make(false, te_short);
        }
        instance->encoder.upload[index++] = level_duration_make(false, te_short * 8);
    }

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;

    FURI_LOG_I(
        TAG,
        "Upload built: %zu elements, btn_to_send=0x%02X",
        instance->encoder.size_upload,
        btn_to_send);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_fiat_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderFiatV0* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    instance->encoder.repeat = 10;

    flipper_format_rewind(flipper_format);

    do {
        FuriString* temp_str = furi_string_alloc();
        if(!flipper_format_read_string(flipper_format, "Protocol", temp_str)) {
            FURI_LOG_E(TAG, "Missing Protocol");
            furi_string_free(temp_str);
            break;
        }

        if(!furi_string_equal(temp_str, instance->base.protocol->name)) {
            FURI_LOG_E(TAG, "Wrong protocol: %s", furi_string_get_cstr(temp_str));
            furi_string_free(temp_str);
            break;
        }
        furi_string_free(temp_str);

        uint32_t bit_count_temp;
        if(!flipper_format_read_uint32(flipper_format, "Bit", &bit_count_temp, 1)) {
            FURI_LOG_E(TAG, "Missing Bit");
            break;
        }

        temp_str = furi_string_alloc();
        if(!flipper_format_read_string(flipper_format, "Key", temp_str)) {
            FURI_LOG_E(TAG, "Missing Key");
            furi_string_free(temp_str);
            break;
        }

        const char* key_str = furi_string_get_cstr(temp_str);
        uint64_t key = 0;
        size_t str_len = strlen(key_str);
        size_t hex_pos = 0;

        for(size_t i = 0; i < str_len && hex_pos < 16; i++) {
            char c = key_str[i];
            if(c == ' ') continue;

            uint8_t nibble;
            if(c >= '0' && c <= '9')
                nibble = c - '0';
            else if(c >= 'A' && c <= 'F')
                nibble = c - 'A' + 10;
            else if(c >= 'a' && c <= 'f')
                nibble = c - 'a' + 10;
            else
                break;

            key = (key << 4) | nibble;
            hex_pos++;
        }
        furi_string_free(temp_str);

        instance->generic.data = key;
        instance->cnt = (uint32_t)(key >> 32);
        instance->serial = (uint32_t)(key & 0xFFFFFFFF);

        uint32_t btn_temp = 0;
        if(flipper_format_read_uint32(flipper_format, "Btn", &btn_temp, 1)) {
            instance->btn = (uint8_t)btn_temp;
        } else {
            instance->btn = 0;
        }

        if(!flipper_format_read_uint32(
               flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1)) {
            instance->encoder.repeat = 10;
        }

        subghz_protocol_encoder_fiat_v0_get_upload(instance);
        instance->encoder.is_running = true;

        FURI_LOG_I(
            TAG,
            "Encoder ready: cnt=0x%08lX, serial=0x%08lX, btn=0x%02X",
            instance->cnt,
            instance->serial,
            instance->btn);

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_fiat_v0_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderFiatV0* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_fiat_v0_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderFiatV0* instance = context;

    if(!instance->encoder.is_running || instance->encoder.repeat == 0) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        instance->encoder.repeat--;
        instance->encoder.front = 0;
    }

    return ret;
}

// ============================================================================
// DECODER IMPLEMENTATION
// ============================================================================

void* subghz_protocol_decoder_fiat_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderFiatV0* instance = malloc(sizeof(SubGhzProtocolDecoderFiatV0));
    instance->base.protocol = &fiat_protocol_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_fiat_v0_free(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV0* instance = context;
    free(instance);
}

void subghz_protocol_decoder_fiat_v0_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV0* instance = context;
    instance->decoder.parser_step = FiatV0DecoderStepReset;
    instance->decoder_state = 0;
    instance->preamble_count = 0;
    instance->data_low = 0;
    instance->data_high = 0;
    instance->bit_count = 0;
    instance->cnt = 0;
    instance->serial = 0;
    instance->btn = 0;
    instance->te_last = 0;
    instance->manchester_state = ManchesterStateMid1;
}

void subghz_protocol_decoder_fiat_v0_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV0* instance = context;
    uint32_t te_short = (uint32_t)subghz_protocol_fiat_v0_const.te_short;
    uint32_t te_long = (uint32_t)subghz_protocol_fiat_v0_const.te_long;
    uint32_t te_delta = (uint32_t)subghz_protocol_fiat_v0_const.te_delta;
    uint32_t gap_threshold = 800;
    uint32_t diff;

    switch(instance->decoder_state) {
    case FiatV0DecoderStepReset:
        if(!level) {
            return;
        }
        if(duration < te_short) {
            diff = te_short - duration;
        } else {
            diff = duration - te_short;
        }
        if(diff < te_delta) {
            instance->data_low = 0;
            instance->data_high = 0;
            instance->decoder_state = FiatV0DecoderStepPreamble;
            instance->te_last = duration;
            instance->preamble_count = 0;
            instance->bit_count = 0;
            manchester_advance(
                instance->manchester_state,
                ManchesterEventReset,
                &instance->manchester_state,
                NULL);
        }
        break;

    case FiatV0DecoderStepPreamble:
        if(level) {
            return;
        }
        if(duration < te_short) {
            diff = te_short - duration;
            if(diff < te_delta) {
                instance->preamble_count++;
                instance->te_last = duration;
                if(instance->preamble_count >= 0x96) {
                    if(duration < gap_threshold) {
                        diff = gap_threshold - duration;
                    } else {
                        diff = duration - gap_threshold;
                    }
                    if(diff < te_delta) {
                        instance->decoder_state = FiatV0DecoderStepData;
                        instance->preamble_count = 0;
                        instance->data_low = 0;
                        instance->data_high = 0;
                        instance->bit_count = 0;
                        instance->te_last = duration;
                        return;
                    }
                }
            } else {
                instance->decoder_state = FiatV0DecoderStepReset;
                if(instance->preamble_count >= 0x96) {
                    if(duration < gap_threshold) {
                        diff = gap_threshold - duration;
                    } else {
                        diff = duration - gap_threshold;
                    }
                    if(diff < te_delta) {
                        instance->decoder_state = FiatV0DecoderStepData;
                        instance->preamble_count = 0;
                        instance->data_low = 0;
                        instance->data_high = 0;
                        instance->bit_count = 0;
                        instance->te_last = duration;
                        return;
                    }
                }
            }
        } else {
            diff = duration - te_short;
            if(diff < te_delta) {
                instance->preamble_count++;
                instance->te_last = duration;
            } else {
                instance->decoder_state = FiatV0DecoderStepReset;
            }
            if(instance->preamble_count >= 0x96) {
                if(duration >= 799) {
                    diff = duration - gap_threshold;
                } else {
                    diff = gap_threshold - duration;
                }
                if(diff < te_delta) {
                    instance->decoder_state = FiatV0DecoderStepData;
                    instance->preamble_count = 0;
                    instance->data_low = 0;
                    instance->data_high = 0;
                    instance->bit_count = 0;
                    instance->te_last = duration;
                    return;
                }
            }
        }
        break;

    case FiatV0DecoderStepData: {
        ManchesterEvent event = ManchesterEventReset;
        if(duration < te_short) {
            diff = te_short - duration;
            if(diff < te_delta) {
                event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
            }
        } else {
            diff = duration - te_short;
            if(diff < te_delta) {
                event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
            } else {
                if(duration < te_long) {
                    diff = te_long - duration;
                } else {
                    diff = duration - te_long;
                }
                if(diff < te_delta) {
                    event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
                }
            }
        }

        if(event != ManchesterEventReset) {
            bool data_bit_bool;
            if(manchester_advance(
                   instance->manchester_state,
                   event,
                   &instance->manchester_state,
                   &data_bit_bool)) {
                uint32_t new_bit = data_bit_bool ? 1 : 0;

                uint32_t carry = (instance->data_low >> 31) & 1;
                instance->data_low = (instance->data_low << 1) | new_bit;
                instance->data_high = (instance->data_high << 1) | carry;

                instance->bit_count++;

                if(instance->bit_count == 0x40) {
                    instance->serial = instance->data_low;
                    instance->cnt = instance->data_high;
                    instance->data_low = 0;
                    instance->data_high = 0;
                }

                if(instance->bit_count > 0x46) {
                    instance->btn = (uint8_t)((instance->data_low << 1) | 1);

                    instance->generic.data = ((uint64_t)instance->cnt << 32) | instance->serial;
                    instance->generic.data_count_bit = 71;
                    instance->generic.serial = instance->serial;
                    instance->generic.btn = instance->btn;
                    instance->generic.cnt = instance->cnt;

                    if(instance->base.callback) {
                        instance->base.callback(&instance->base, instance->base.context);
                    }

                    instance->data_low = 0;
                    instance->data_high = 0;
                    instance->bit_count = 0;
                    instance->decoder_state = FiatV0DecoderStepReset;
                }
            }
        }
        instance->te_last = duration;
        break;
    }
    }
}

uint8_t subghz_protocol_decoder_fiat_v0_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV0* instance = context;
    SubGhzBlockDecoder decoder = {
        .decode_data = instance->generic.data,
        .decode_count_bit = instance->generic.data_count_bit};
    return subghz_protocol_blocks_get_hash_data(&decoder, (decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_fiat_v0_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV0* instance = context;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        if(!flipper_format_write_uint32(flipper_format, "Frequency", &preset->frequency, 1)) break;
        if(!flipper_format_write_string_cstr(
               flipper_format, "Preset", furi_string_get_cstr(preset->name)))
            break;
        if(!flipper_format_write_string_cstr(
               flipper_format, "Protocol", instance->generic.protocol_name))
            break;

        uint32_t bits = 71;
        if(!flipper_format_write_uint32(flipper_format, "Bit", &bits, 1)) break;

        char key_str[20];
        snprintf(key_str, sizeof(key_str), "%08lX%08lX", instance->cnt, instance->serial);
        if(!flipper_format_write_string_cstr(flipper_format, "Key", key_str)) break;

        if(!flipper_format_write_uint32(flipper_format, "Cnt", &instance->cnt, 1)) break;
        if(!flipper_format_write_uint32(flipper_format, "Serial", &instance->serial, 1)) break;

        uint32_t temp = instance->btn;
        if(!flipper_format_write_uint32(flipper_format, "Btn", &temp, 1)) break;

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_fiat_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV0* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_fiat_v0_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_fiat_v0_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV0* instance = context;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Sn:%08lX Btn:%02X\r\n"
        "Cnt:%08lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->cnt,
        instance->serial,
        instance->serial,
        instance->btn,
        instance->cnt);
}
