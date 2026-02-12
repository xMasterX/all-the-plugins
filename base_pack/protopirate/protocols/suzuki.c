#include "suzuki.h"

#define TAG "SuzukiProtocol"

// ============================================================================
// PROTOCOL CONSTANTS
// ============================================================================

static const SubGhzBlockConst subghz_protocol_suzuki_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 99,
    .min_count_bit_for_found = 64,
};

#define SUZUKI_PREAMBLE_COUNT 350
#define SUZUKI_GAP_TIME       2000
#define SUZUKI_GAP_DELTA      399

// ============================================================================
// DECODER STRUCT
// ============================================================================

typedef struct SubGhzProtocolDecoderSuzuki {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
} SubGhzProtocolDecoderSuzuki;

// ============================================================================
// ENCODER STRUCT
// ============================================================================

typedef struct SubGhzProtocolEncoderSuzuki {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
} SubGhzProtocolEncoderSuzuki;

// ============================================================================
// DECODER STATE MACHINE
// ============================================================================

typedef enum {
    SuzukiDecoderStepReset = 0,
    SuzukiDecoderStepCountPreamble = 1,
    SuzukiDecoderStepDecodeData = 2,
} SuzukiDecoderStep;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static void suzuki_add_bit(SubGhzProtocolDecoderSuzuki* instance, uint8_t bit) {
    instance->decoder.decode_data = (instance->decoder.decode_data << 1) | bit;
    instance->decoder.decode_count_bit++;
}

static const char* suzuki_get_button_name(uint8_t btn) {
    switch(btn) {
    case 1:
        return "Panic";
    case 2:
        return "Boot";
    case 3:
        return "Lock";
    case 4:
        return "Unlock";
    default:
        return "Unknown";
    }
}

// ============================================================================
// PROTOCOL DEFINITION
// ============================================================================

const SubGhzProtocolDecoder subghz_protocol_suzuki_decoder = {
    .alloc = subghz_protocol_decoder_suzuki_alloc,
    .free = subghz_protocol_decoder_suzuki_free,
    .feed = subghz_protocol_decoder_suzuki_feed,
    .reset = subghz_protocol_decoder_suzuki_reset,
    .get_hash_data = subghz_protocol_decoder_suzuki_get_hash_data,
    .serialize = subghz_protocol_decoder_suzuki_serialize,
    .deserialize = subghz_protocol_decoder_suzuki_deserialize,
    .get_string = subghz_protocol_decoder_suzuki_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_suzuki_encoder = {
    .alloc = subghz_protocol_encoder_suzuki_alloc,
    .free = subghz_protocol_encoder_suzuki_free,
    .deserialize = subghz_protocol_encoder_suzuki_deserialize,
    .stop = subghz_protocol_encoder_suzuki_stop,
    .yield = subghz_protocol_encoder_suzuki_yield,
};

const SubGhzProtocol suzuki_protocol = {
    .name = SUZUKI_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_suzuki_decoder,
    .encoder = &subghz_protocol_suzuki_encoder,
};

// ============================================================================
// DECODER IMPLEMENTATION
// ============================================================================

void* subghz_protocol_decoder_suzuki_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderSuzuki* instance = malloc(sizeof(SubGhzProtocolDecoderSuzuki));
    instance->base.protocol = &suzuki_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_suzuki_free(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderSuzuki* instance = context;
    free(instance);
}

void subghz_protocol_decoder_suzuki_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderSuzuki* instance = context;
    instance->decoder.parser_step = SuzukiDecoderStepReset;
}

void subghz_protocol_decoder_suzuki_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderSuzuki* instance = context;

    switch(instance->decoder.parser_step) {
    case SuzukiDecoderStepReset:
        // Wait for HIGH pulse (~250µs) to start preamble
        if(!level) {
            return;
        }

        if(DURATION_DIFF(duration, subghz_protocol_suzuki_const.te_short) >
           subghz_protocol_suzuki_const.te_delta) {
            return;
        }

        instance->decoder.decode_data = 0;
        instance->decoder.decode_count_bit = 0;
        instance->decoder.parser_step = SuzukiDecoderStepCountPreamble;
        instance->header_count = 0;
        break;

    case SuzukiDecoderStepCountPreamble:
        if(level) {
            // HIGH pulse
            if(instance->header_count >= 300) {
                if(DURATION_DIFF(duration, subghz_protocol_suzuki_const.te_long) <=
                   subghz_protocol_suzuki_const.te_delta) {
                    instance->decoder.parser_step = SuzukiDecoderStepDecodeData;
                    suzuki_add_bit(instance, 1);
                }
            }
        } else {
            if(DURATION_DIFF(duration, subghz_protocol_suzuki_const.te_short) <=
               subghz_protocol_suzuki_const.te_delta) {
                instance->decoder.te_last = duration;
                instance->header_count++;
            } else {
                instance->decoder.parser_step = SuzukiDecoderStepReset;
            }
        }
        break;

    case SuzukiDecoderStepDecodeData:
        if(level) {
            // HIGH pulse - determines bit value
            if(duration < subghz_protocol_suzuki_const.te_long) {
                uint32_t diff_long = 500 - duration;
                if(diff_long > 99) {
                    uint32_t diff_short;
                    if(duration < 250) {
                        diff_short = 250 - duration;
                    } else {
                        diff_short = duration - 250;
                    }

                    if(diff_short <= 99) {
                        suzuki_add_bit(instance, 0);
                    }
                } else {
                    suzuki_add_bit(instance, 1);
                }
            } else {
                uint32_t diff_long = duration - 500;
                if(diff_long <= 99) {
                    suzuki_add_bit(instance, 1);
                }
            }
        } else {
            // LOW pulse - check for gap (end of transmission)
            uint32_t diff_gap;
            if(duration < SUZUKI_GAP_TIME) {
                diff_gap = SUZUKI_GAP_TIME - duration;
            } else {
                diff_gap = duration - SUZUKI_GAP_TIME;
            }

            if(diff_gap <= SUZUKI_GAP_DELTA) {
                if(instance->decoder.decode_count_bit == 64) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = 64;

                    uint64_t data = instance->generic.data;
                    uint32_t data_high = (uint32_t)(data >> 32);
                    uint32_t data_low = (uint32_t)data;

                    instance->generic.serial = ((data_high & 0xFFF) << 16) | (data_low >> 16);
                    instance->generic.btn = (data_low >> 12) & 0xF;
                    instance->generic.cnt = (data_high << 4) >> 16;

                    if(instance->base.callback) {
                        instance->base.callback(&instance->base, instance->base.context);
                    }
                }

                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = SuzukiDecoderStepReset;
            }
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_suzuki_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderSuzuki* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->generic.data_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_suzuki_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderSuzuki* instance = context;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    ret = subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        // Save CRC
        uint32_t crc = (instance->generic.data >> 4) & 0xFF;
        flipper_format_write_uint32(flipper_format, "CRC", &crc, 1);

        // Save decoded fields
        flipper_format_write_uint32(flipper_format, "Serial", &instance->generic.serial, 1);

        uint32_t temp = instance->generic.btn;
        flipper_format_write_uint32(flipper_format, "Btn", &temp, 1);

        flipper_format_write_uint32(flipper_format, "Cnt", &instance->generic.cnt, 1);
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_suzuki_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderSuzuki* instance = context;
    return subghz_block_generic_deserialize(&instance->generic, flipper_format);
}

void subghz_protocol_decoder_suzuki_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderSuzuki* instance = context;

    uint64_t data = instance->generic.data;
    uint32_t key_high = (data >> 32) & 0xFFFFFFFF;
    uint32_t key_low = data & 0xFFFFFFFF;
    uint8_t crc = (data >> 4) & 0xFF;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Sn:%07lX Btn:%X %s\r\n"
        "Cnt:%04lX CRC:%02X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        key_high,
        key_low,
        instance->generic.serial,
        instance->generic.btn,
        suzuki_get_button_name(instance->generic.btn),
        instance->generic.cnt,
        crc);
}

// ============================================================================
// ENCODER IMPLEMENTATION
// ============================================================================

void* subghz_protocol_encoder_suzuki_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderSuzuki* instance = malloc(sizeof(SubGhzProtocolEncoderSuzuki));
    instance->base.protocol = &suzuki_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_suzuki_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderSuzuki* instance = context;
    if(instance->encoder.upload) {
        free(instance->encoder.upload);
    }
    free(instance);
}

/**
 * Build the upload buffer for transmission
 * Signal format: 350 preamble pairs (SHORT HIGH/SHORT LOW) + 64 data bits + gap
 * Data encoding: SHORT HIGH = 0, LONG HIGH = 1
 */
static void subghz_protocol_encoder_suzuki_get_upload(SubGhzProtocolEncoderSuzuki* instance) {
    furi_check(instance);

    size_t index = 0;

    // Free old upload if exists
    if(instance->encoder.upload) {
        free(instance->encoder.upload);
    }

    // Allocate: preamble pairs + data bits (each has HIGH + LOW) + end gap
    instance->encoder.size_upload = (SUZUKI_PREAMBLE_COUNT * 2) + (64 * 2) + 1;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));

    // Preamble: SHORT HIGH / SHORT LOW pairs
    for(size_t i = 0; i < SUZUKI_PREAMBLE_COUNT; i++) {
        instance->encoder.upload[index++] =
            level_duration_make(true, subghz_protocol_suzuki_const.te_short);
        instance->encoder.upload[index++] =
            level_duration_make(false, subghz_protocol_suzuki_const.te_short);
    }

    // Data: 64 bits, MSB first
    // SHORT HIGH (~250µs) = 0, LONG HIGH (~500µs) = 1
    for(int bit = 63; bit >= 0; bit--) {
        if((instance->generic.data >> bit) & 1) {
            instance->encoder.upload[index++] =
                level_duration_make(true, subghz_protocol_suzuki_const.te_long);
        } else {
            instance->encoder.upload[index++] =
                level_duration_make(true, subghz_protocol_suzuki_const.te_short);
        }
        instance->encoder.upload[index++] =
            level_duration_make(false, subghz_protocol_suzuki_const.te_short);
    }

    // End gap
    instance->encoder.upload[index++] = level_duration_make(false, SUZUKI_GAP_TIME);

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_suzuki_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderSuzuki* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        subghz_protocol_encoder_suzuki_get_upload(instance);

        instance->encoder.is_running = true;
        instance->encoder.repeat = 10;

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_suzuki_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderSuzuki* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_suzuki_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderSuzuki* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
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
