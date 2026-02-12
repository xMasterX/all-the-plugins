#include "subaru.h"
#include "../protopirate_app_i.h"

#define TAG "SubaruProtocol"

static const SubGhzBlockConst subghz_protocol_subaru_const = {
    .te_short = 800,
    .te_long = 1600,
    .te_delta = 200,
    .min_count_bit_for_found = 64,
};

#define SUBARU_PREAMBLE_PAIRS  80
#define SUBARU_GAP_US          2800
#define SUBARU_SYNC_US         2800
#define SUBARU_TOTAL_BURSTS    3
#define SUBARU_INTER_BURST_GAP 25000

typedef struct SubGhzProtocolDecoderSubaru {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;
    uint16_t bit_count;
    uint8_t data[8];

    uint64_t key;
    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
} SubGhzProtocolDecoderSubaru;

typedef struct SubGhzProtocolEncoderSubaru {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint64_t key;
    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
} SubGhzProtocolEncoderSubaru;

typedef enum {
    SubaruDecoderStepReset = 0,
    SubaruDecoderStepCheckPreamble,
    SubaruDecoderStepFoundGap,
    SubaruDecoderStepFoundSync,
    SubaruDecoderStepSaveDuration,
    SubaruDecoderStepCheckDuration,
} SubaruDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_subaru_decoder = {
    .alloc = subghz_protocol_decoder_subaru_alloc,
    .free = subghz_protocol_decoder_subaru_free,
    .feed = subghz_protocol_decoder_subaru_feed,
    .reset = subghz_protocol_decoder_subaru_reset,
    .get_hash_data = subghz_protocol_decoder_subaru_get_hash_data,
    .serialize = subghz_protocol_decoder_subaru_serialize,
    .deserialize = subghz_protocol_decoder_subaru_deserialize,
    .get_string = subghz_protocol_decoder_subaru_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_subaru_encoder = {
    .alloc = subghz_protocol_encoder_subaru_alloc,
    .free = subghz_protocol_encoder_subaru_free,
    .deserialize = subghz_protocol_encoder_subaru_deserialize,
    .stop = subghz_protocol_encoder_subaru_stop,
    .yield = subghz_protocol_encoder_subaru_yield,
};

const SubGhzProtocol subaru_protocol = {
    .name = SUBARU_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_subaru_decoder,
    .encoder = &subghz_protocol_subaru_encoder,
};

// ============================================================================
// DECODER HELPER FUNCTIONS
// ============================================================================

static void subaru_decode_count(const uint8_t* KB, uint16_t* cnt) {
    uint8_t lo = 0;
    if((KB[4] & 0x40) == 0) lo |= 0x01;
    if((KB[4] & 0x80) == 0) lo |= 0x02;
    if((KB[5] & 0x01) == 0) lo |= 0x04;
    if((KB[5] & 0x02) == 0) lo |= 0x08;
    if((KB[6] & 0x01) == 0) lo |= 0x10;
    if((KB[6] & 0x02) == 0) lo |= 0x20;
    if((KB[5] & 0x40) == 0) lo |= 0x40;
    if((KB[5] & 0x80) == 0) lo |= 0x80;

    uint8_t REG_SH1 = (KB[7] << 4) & 0xF0;
    if(KB[5] & 0x04) REG_SH1 |= 0x04;
    if(KB[5] & 0x08) REG_SH1 |= 0x08;
    if(KB[6] & 0x80) REG_SH1 |= 0x02;
    if(KB[6] & 0x40) REG_SH1 |= 0x01;

    uint8_t REG_SH2 = ((KB[6] << 2) & 0xF0) | ((KB[7] >> 4) & 0x0F);

    uint8_t SER0 = KB[3];
    uint8_t SER1 = KB[1];
    uint8_t SER2 = KB[2];

    uint8_t total_rot = 4 + lo;
    for(uint8_t i = 0; i < total_rot; ++i) {
        uint8_t t_bit = (SER0 >> 7) & 1;
        SER0 = ((SER0 << 1) & 0xFE) | ((SER1 >> 7) & 1);
        SER1 = ((SER1 << 1) & 0xFE) | ((SER2 >> 7) & 1);
        SER2 = ((SER2 << 1) & 0xFE) | t_bit;
    }

    uint8_t T1 = SER1 ^ REG_SH1;
    uint8_t T2 = SER2 ^ REG_SH2;

    uint8_t hi = 0;
    if((T1 & 0x10) == 0) hi |= 0x04;
    if((T1 & 0x20) == 0) hi |= 0x08;
    if((T2 & 0x80) == 0) hi |= 0x02;
    if((T2 & 0x40) == 0) hi |= 0x01;
    if((T1 & 0x01) == 0) hi |= 0x40;
    if((T1 & 0x02) == 0) hi |= 0x80;
    if((T2 & 0x08) == 0) hi |= 0x20;
    if((T2 & 0x04) == 0) hi |= 0x10;

    *cnt = ((hi << 8) | lo) & 0xFFFF;
}

static void subaru_add_bit(SubGhzProtocolDecoderSubaru* instance, bool bit) {
    if(instance->bit_count < 64) {
        uint8_t byte_idx = instance->bit_count / 8;
        uint8_t bit_idx = 7 - (instance->bit_count % 8);
        if(bit) {
            instance->data[byte_idx] |= (1 << bit_idx);
        } else {
            instance->data[byte_idx] &= ~(1 << bit_idx);
        }
        instance->bit_count++;
    }
}

static bool subaru_process_data(SubGhzProtocolDecoderSubaru* instance) {
    if(instance->bit_count < 64) {
        return false;
    }

    uint8_t* b = instance->data;

    instance->key = ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) | ((uint64_t)b[2] << 40) |
                    ((uint64_t)b[3] << 32) | ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
                    ((uint64_t)b[6] << 8) | ((uint64_t)b[7]);

    instance->serial = ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    instance->btn = b[0] & 0x0F;
    subaru_decode_count(b, &instance->cnt);

    return true;
}

// ============================================================================
// ENCODER IMPLEMENTATION
// ============================================================================

void* subghz_protocol_encoder_subaru_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderSubaru* instance = malloc(sizeof(SubGhzProtocolEncoderSubaru));

    instance->base.protocol = &subaru_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 1024;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    instance->key = 0;
    instance->serial = 0;
    instance->btn = 0;
    instance->cnt = 0;

    return instance;
}

void subghz_protocol_encoder_subaru_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderSubaru* instance = context;
    if(instance->encoder.upload) {
        free(instance->encoder.upload);
    }
    free(instance);
}

static void subghz_protocol_encoder_subaru_get_upload(SubGhzProtocolEncoderSubaru* instance) {
    furi_check(instance);
    size_t index = 0;

    uint32_t te_short = subghz_protocol_subaru_const.te_short;
    uint32_t te_long = subghz_protocol_subaru_const.te_long;

    FURI_LOG_I(
        TAG,
        "Building upload: key=0x%016llX, serial=0x%06lX, btn=0x%X, cnt=0x%04X",
        instance->key,
        instance->serial,
        instance->btn,
        instance->cnt);

    for(uint8_t burst = 0; burst < SUBARU_TOTAL_BURSTS; burst++) {
        if(burst > 0) {
            instance->encoder.upload[index++] = level_duration_make(false, SUBARU_INTER_BURST_GAP);
        }

        // Preamble: Long HIGH/LOW pairs
        for(int i = 0; i < SUBARU_PREAMBLE_PAIRS; i++) {
            instance->encoder.upload[index++] = level_duration_make(true, te_long);
            instance->encoder.upload[index++] = level_duration_make(false, te_long);
        }

        // Replace last preamble LOW with gap (to avoid consecutive LOWs combining)
        instance->encoder.upload[index - 1] = level_duration_make(false, SUBARU_GAP_US);

        // Sync: Long HIGH
        instance->encoder.upload[index++] = level_duration_make(true, SUBARU_SYNC_US);

        // Sync end: Long LOW
        instance->encoder.upload[index++] = level_duration_make(false, te_long);

        // Data: 64 bits, PWM encoding
        // Short HIGH = 1, Long HIGH = 0
        for(int bit = 63; bit >= 0; bit--) {
            if((instance->key >> bit) & 1) {
                // Bit 1: Short HIGH
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
            } else {
                // Bit 0: Long HIGH
                instance->encoder.upload[index++] = level_duration_make(true, te_long);
            }
            // LOW separator
            instance->encoder.upload[index++] = level_duration_make(false, te_short);
        }

        // End marker: extended LOW
        instance->encoder.upload[index++] = level_duration_make(false, te_long * 2);
    }

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;

    FURI_LOG_I(TAG, "Upload built: %zu elements", instance->encoder.size_upload);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_subaru_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderSubaru* instance = context;
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

        // Try to read raw data first (most accurate)
        uint32_t raw_high = 0, raw_low = 0;
        if(flipper_format_read_uint32(flipper_format, "DataHi", &raw_high, 1) &&
           flipper_format_read_uint32(flipper_format, "DataLo", &raw_low, 1)) {
            instance->key = ((uint64_t)raw_high << 32) | raw_low;
            FURI_LOG_I(TAG, "Read raw data: 0x%016llX", instance->key);
        } else {
            // Fall back to Key field
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

            instance->key = key;
            FURI_LOG_I(TAG, "Parsed key: 0x%016llX", instance->key);
        }

        // Read optional fields
        if(!flipper_format_read_uint32(flipper_format, "Serial", &instance->serial, 1)) {
            instance->serial = (uint32_t)((instance->key >> 32) & 0xFFFFFF);
        }

        uint32_t btn_temp = 0;
        if(flipper_format_read_uint32(flipper_format, "Btn", &btn_temp, 1)) {
            instance->btn = (uint8_t)btn_temp;
        } else {
            instance->btn = (uint8_t)((instance->key >> 56) & 0x0F);
        }

        uint32_t cnt_temp = 0;
        if(flipper_format_read_uint32(flipper_format, "Cnt", &cnt_temp, 1)) {
            instance->cnt = (uint16_t)cnt_temp;
        } else {
            instance->cnt = 0;
        }

        if(!flipper_format_read_uint32(
               flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1)) {
            instance->encoder.repeat = 10;
        }

        subghz_protocol_encoder_subaru_get_upload(instance);
        instance->encoder.is_running = true;

        FURI_LOG_I(
            TAG,
            "Encoder ready: key=0x%016llX, serial=0x%06lX, btn=0x%X",
            instance->key,
            instance->serial,
            instance->btn);

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_subaru_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderSubaru* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_subaru_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderSubaru* instance = context;

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

void* subghz_protocol_decoder_subaru_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderSubaru* instance = malloc(sizeof(SubGhzProtocolDecoderSubaru));
    instance->base.protocol = &subaru_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_subaru_free(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderSubaru* instance = context;
    free(instance);
}

void subghz_protocol_decoder_subaru_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderSubaru* instance = context;
    instance->decoder.parser_step = SubaruDecoderStepReset;
    instance->decoder.te_last = 0;
    instance->header_count = 0;
    instance->bit_count = 0;
    memset(instance->data, 0, sizeof(instance->data));
}

void subghz_protocol_decoder_subaru_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderSubaru* instance = context;

    uint32_t te_short = subghz_protocol_subaru_const.te_short;
    uint32_t te_long = subghz_protocol_subaru_const.te_long;
    uint32_t te_delta = subghz_protocol_subaru_const.te_delta;

    switch(instance->decoder.parser_step) {
    case SubaruDecoderStepReset:
        if(level && (DURATION_DIFF(duration, te_long) < te_delta)) {
            instance->decoder.parser_step = SubaruDecoderStepCheckPreamble;
            instance->decoder.te_last = duration;
            instance->header_count = 1;
        }
        break;

    case SubaruDecoderStepCheckPreamble:
        if(!level) {
            if(DURATION_DIFF(duration, te_long) < te_delta) {
                instance->header_count++;
            } else if(duration > 2000 && duration < 3500) {
                if(instance->header_count > 20) {
                    instance->decoder.parser_step = SubaruDecoderStepFoundGap;
                } else {
                    instance->decoder.parser_step = SubaruDecoderStepReset;
                }
            } else {
                instance->decoder.parser_step = SubaruDecoderStepReset;
            }
        } else {
            if(DURATION_DIFF(duration, te_long) < te_delta) {
                instance->decoder.te_last = duration;
                instance->header_count++;
            } else {
                instance->decoder.parser_step = SubaruDecoderStepReset;
            }
        }
        break;

    case SubaruDecoderStepFoundGap:
        if(level && duration > 2000 && duration < 3500) {
            instance->decoder.parser_step = SubaruDecoderStepFoundSync;
        } else {
            instance->decoder.parser_step = SubaruDecoderStepReset;
        }
        break;

    case SubaruDecoderStepFoundSync:
        if(!level && (DURATION_DIFF(duration, te_long) < te_delta)) {
            instance->decoder.parser_step = SubaruDecoderStepSaveDuration;
            instance->bit_count = 0;
            memset(instance->data, 0, sizeof(instance->data));
        } else {
            instance->decoder.parser_step = SubaruDecoderStepReset;
        }
        break;

    case SubaruDecoderStepSaveDuration:
        if(level) {
            if(DURATION_DIFF(duration, te_short) < te_delta) {
                // Short HIGH = bit 1
                subaru_add_bit(instance, true);
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = SubaruDecoderStepCheckDuration;
            } else if(DURATION_DIFF(duration, te_long) < te_delta) {
                // Long HIGH = bit 0
                subaru_add_bit(instance, false);
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = SubaruDecoderStepCheckDuration;
            } else if(duration > 3000) {
                // End of transmission
                if(instance->bit_count >= 64) {
                    if(subaru_process_data(instance)) {
                        instance->generic.data = instance->key;
                        instance->generic.data_count_bit = 64;
                        instance->generic.serial = instance->serial;
                        instance->generic.btn = instance->btn;
                        instance->generic.cnt = instance->cnt;

                        if(instance->base.callback) {
                            instance->base.callback(&instance->base, instance->base.context);
                        }
                    }
                }
                instance->decoder.parser_step = SubaruDecoderStepReset;
            } else {
                instance->decoder.parser_step = SubaruDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = SubaruDecoderStepReset;
        }
        break;

    case SubaruDecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(duration, te_short) < te_delta) ||
               (DURATION_DIFF(duration, te_long) < te_delta)) {
                instance->decoder.parser_step = SubaruDecoderStepSaveDuration;
            } else if(duration > 3000) {
                // Gap - end of packet
                if(instance->bit_count >= 64) {
                    if(subaru_process_data(instance)) {
                        instance->generic.data = instance->key;
                        instance->generic.data_count_bit = 64;
                        instance->generic.serial = instance->serial;
                        instance->generic.btn = instance->btn;
                        instance->generic.cnt = instance->cnt;

                        if(instance->base.callback) {
                            instance->base.callback(&instance->base, instance->base.context);
                        }
                    }
                }
                instance->decoder.parser_step = SubaruDecoderStepReset;
            } else {
                instance->decoder.parser_step = SubaruDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = SubaruDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_subaru_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderSubaru* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_subaru_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderSubaru* instance = context;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        if(!flipper_format_write_uint32(flipper_format, "Frequency", &preset->frequency, 1)) break;
        if(!flipper_format_write_string_cstr(
               flipper_format, "Preset", furi_string_get_cstr(preset->name)))
            break;
        if(!flipper_format_write_string_cstr(
               flipper_format, "Protocol", instance->generic.protocol_name))
            break;

        uint32_t bits = 64;
        if(!flipper_format_write_uint32(flipper_format, "Bit", &bits, 1)) break;

        char key_str[20];
        uint32_t key_hi = (uint32_t)(instance->key >> 32);
        uint32_t key_lo = (uint32_t)(instance->key & 0xFFFFFFFF);
        snprintf(key_str, sizeof(key_str), "%08lX%08lX", key_hi, key_lo);
        if(!flipper_format_write_string_cstr(flipper_format, "Key", key_str)) break;

        if(!flipper_format_write_uint32(flipper_format, "Serial", &instance->serial, 1)) break;

        uint32_t temp = instance->btn;
        if(!flipper_format_write_uint32(flipper_format, "Btn", &temp, 1)) break;

        temp = instance->cnt;
        if(!flipper_format_write_uint32(flipper_format, "Cnt", &temp, 1)) break;

        // Save raw data for exact reproduction
        uint32_t raw_high = (uint32_t)(instance->key >> 32);
        uint32_t raw_low = (uint32_t)(instance->key & 0xFFFFFFFF);
        if(!flipper_format_write_uint32(flipper_format, "DataHi", &raw_high, 1)) break;
        if(!flipper_format_write_uint32(flipper_format, "DataLo", &raw_low, 1)) break;

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_subaru_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderSubaru* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_subaru_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_subaru_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderSubaru* instance = context;

    uint32_t key_hi = (uint32_t)(instance->key >> 32);
    uint32_t key_lo = (uint32_t)(instance->key & 0xFFFFFFFF);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Sn:%06lX Btn:%X Cnt:%04X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        key_hi,
        key_lo,
        instance->serial,
        instance->btn,
        instance->cnt);
}
