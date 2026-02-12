#include "kia_v2.h"
#include "../protopirate_app_i.h"
#include <lib/toolbox/manchester_encoder.h>
#include <furi.h>

#define TAG "KiaV2"

#define KIA_V2_HEADER_PAIRS 252
#define KIA_V2_TOTAL_BURSTS 2

static const SubGhzBlockConst kia_protocol_v2_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 150,
    .min_count_bit_for_found = 53,
};

struct SubGhzProtocolDecoderKiaV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;

    ManchesterState manchester_state;
};

struct SubGhzProtocolEncoderKiaV2 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    KiaV2DecoderStepReset = 0,
    KiaV2DecoderStepCheckPreamble,
    KiaV2DecoderStepCollectRawBits,
} KiaV2DecoderStep;

const SubGhzProtocolDecoder kia_protocol_v2_decoder = {
    .alloc = kia_protocol_decoder_v2_alloc,
    .free = kia_protocol_decoder_v2_free,
    .feed = kia_protocol_decoder_v2_feed,
    .reset = kia_protocol_decoder_v2_reset,
    .get_hash_data = kia_protocol_decoder_v2_get_hash_data,
    .serialize = kia_protocol_decoder_v2_serialize,
    .deserialize = kia_protocol_decoder_v2_deserialize,
    .get_string = kia_protocol_decoder_v2_get_string,
};

const SubGhzProtocolEncoder kia_protocol_v2_encoder = {
    .alloc = kia_protocol_encoder_v2_alloc,
    .free = kia_protocol_encoder_v2_free,
    .deserialize = kia_protocol_encoder_v2_deserialize,
    .stop = kia_protocol_encoder_v2_stop,
    .yield = kia_protocol_encoder_v2_yield,
};

const SubGhzProtocol kia_protocol_v2 = {
    .name = KIA_PROTOCOL_V2_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &kia_protocol_v2_decoder,
    .encoder = &kia_protocol_v2_encoder,
};

static uint8_t kia_v2_calculate_crc(uint64_t data) {
    // Remove the CRC nibble (last 4 bits) to get the actual data
    uint64_t data_without_crc = data >> 4;

    // Extract 6 bytes from the data
    uint8_t bytes[6];
    bytes[0] = (uint8_t)(data_without_crc);
    bytes[1] = (uint8_t)(data_without_crc >> 8);
    bytes[2] = (uint8_t)(data_without_crc >> 16);
    bytes[3] = (uint8_t)(data_without_crc >> 24);
    bytes[4] = (uint8_t)(data_without_crc >> 32);
    bytes[5] = (uint8_t)(data_without_crc >> 40);

    uint8_t crc = 0;
    for(int i = 0; i < 6; i++) {
        crc ^= (bytes[i] & 0x0F) ^ (bytes[i] >> 4);
    }

    return (crc + 1) & 0x0F;
}

static void kia_protocol_encoder_v2_get_upload(SubGhzProtocolEncoderKiaV2* instance) {
    furi_check(instance);
    size_t index = 0;

    uint8_t crc = kia_v2_calculate_crc(instance->generic.data);
    instance->generic.data = (instance->generic.data & ~0x0FULL) | crc;

    for(uint8_t burst = 0; burst < KIA_V2_TOTAL_BURSTS; burst++) {
        for(int i = 0; i < KIA_V2_HEADER_PAIRS; i++) {
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)kia_protocol_v2_const.te_long);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)kia_protocol_v2_const.te_long);
        }

        instance->encoder.upload[index++] =
            level_duration_make(false, (uint32_t)kia_protocol_v2_const.te_short);

        for(uint8_t i = instance->generic.data_count_bit; i > 1; i--) {
            bool bit = bit_read(instance->generic.data, i - 2);

            if(bit) {
                instance->encoder.upload[index++] =
                    level_duration_make(true, (uint32_t)kia_protocol_v2_const.te_short);
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)kia_protocol_v2_const.te_short);
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)kia_protocol_v2_const.te_short);
                instance->encoder.upload[index++] =
                    level_duration_make(true, (uint32_t)kia_protocol_v2_const.te_short);
            }
        }
    }

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;

    FURI_LOG_I(
        TAG,
        "Upload built: %d bursts, size_upload=%zu, data_count_bit=%u, data=0x%016llX",
        KIA_V2_TOTAL_BURSTS,
        instance->encoder.size_upload,
        instance->generic.data_count_bit,
        instance->generic.data);
}

void* kia_protocol_encoder_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderKiaV2* instance = malloc(sizeof(SubGhzProtocolEncoderKiaV2));

    instance->base.protocol = &kia_protocol_v2;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 1300;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    return instance;
}

void kia_protocol_encoder_v2_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderKiaV2* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

SubGhzProtocolStatus
    kia_protocol_encoder_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderKiaV2* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    flipper_format_rewind(flipper_format);

    do {
        FuriString* temp_str = furi_string_alloc();
        if(!flipper_format_read_string(flipper_format, "Protocol", temp_str)) {
            FURI_LOG_E(TAG, "Missing Protocol");
            furi_string_free(temp_str);
            break;
        }

        if(!furi_string_equal(temp_str, instance->base.protocol->name)) {
            FURI_LOG_E(
                TAG,
                "Wrong protocol %s != %s",
                furi_string_get_cstr(temp_str),
                instance->base.protocol->name);
            furi_string_free(temp_str);
            break;
        }
        furi_string_free(temp_str);

        uint32_t bit_count_temp;
        if(!flipper_format_read_uint32(flipper_format, "Bit", &bit_count_temp, 1)) {
            FURI_LOG_E(TAG, "Missing Bit");
            break;
        }

        instance->generic.data_count_bit = kia_protocol_v2_const.min_count_bit_for_found;

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
            if(c >= '0' && c <= '9') {
                nibble = c - '0';
            } else if(c >= 'A' && c <= 'F') {
                nibble = c - 'A' + 10;
            } else if(c >= 'a' && c <= 'f') {
                nibble = c - 'a' + 10;
            } else {
                FURI_LOG_E(TAG, "Invalid hex character: %c", c);
                furi_string_free(temp_str);
                break;
            }

            key = (key << 4) | nibble;
            hex_pos++;
        }

        furi_string_free(temp_str);

        if(hex_pos != 16) {
            FURI_LOG_E(TAG, "Invalid key length: %zu nibbles (expected 16)", hex_pos);
            break;
        }

        instance->generic.data = key;
        FURI_LOG_I(TAG, "Parsed key: 0x%016llX", instance->generic.data);

        if(instance->generic.data == 0) {
            FURI_LOG_E(TAG, "Key is zero after parsing!");
            break;
        }

        if(!flipper_format_read_uint32(flipper_format, "Serial", &instance->generic.serial, 1)) {
            instance->generic.serial = (uint32_t)((instance->generic.data >> 20) & 0xFFFFFFFF);
            FURI_LOG_I(TAG, "Extracted serial: 0x%08lX", instance->generic.serial);
        } else {
            FURI_LOG_I(TAG, "Read serial: 0x%08lX", instance->generic.serial);
        }

        uint32_t btn_temp;
        if(flipper_format_read_uint32(flipper_format, "Btn", &btn_temp, 1)) {
            instance->generic.btn = (uint8_t)btn_temp;
            FURI_LOG_I(TAG, "Read button: 0x%02X", instance->generic.btn);
        } else {
            instance->generic.btn = (uint8_t)((instance->generic.data >> 16) & 0x0F);
            FURI_LOG_I(TAG, "Extracted button: 0x%02X", instance->generic.btn);
        }

        uint32_t cnt_temp;
        if(flipper_format_read_uint32(flipper_format, "Cnt", &cnt_temp, 1)) {
            instance->generic.cnt = (uint16_t)cnt_temp;
            FURI_LOG_I(TAG, "Read counter: 0x%03lX", (unsigned long)instance->generic.cnt);
        } else {
            uint16_t raw_count = (uint16_t)((instance->generic.data >> 4) & 0xFFF);
            instance->generic.cnt = ((raw_count >> 4) | (raw_count << 8)) & 0xFFF;
            FURI_LOG_I(TAG, "Extracted counter: 0x%03lX", (unsigned long)instance->generic.cnt);
        }

        uint64_t new_data = 0;

        new_data |= 1ULL << 52;

        new_data |= ((uint64_t)instance->generic.serial << 20) & 0xFFFFFFFFF00000ULL;

        uint32_t uVar6 = ((uint32_t)(instance->generic.cnt & 0xFF) << 8) |
                         ((uint32_t)(instance->generic.btn & 0x0F) << 16) |
                         ((uint32_t)(instance->generic.cnt >> 4) & 0xF0);

        new_data |= (uint64_t)uVar6;

        instance->generic.data = new_data;
        instance->generic.data_count_bit = 53;

        FURI_LOG_I(
            TAG,
            "Encoder reconstruct: serial=0x%08lX, btn=0x%X, cnt=0x%03lX, uVar6=0x%05lX, data=0x%016llX",
            (unsigned long)instance->generic.serial,
            (unsigned int)instance->generic.btn,
            (unsigned long)instance->generic.cnt,
            (unsigned long)uVar6,
            (unsigned long long)instance->generic.data);

        if(!flipper_format_read_uint32(
               flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1)) {
            instance->encoder.repeat = 10;
            FURI_LOG_D(TAG, "Repeat not found in file, using default 10");
        }

        kia_protocol_encoder_v2_get_upload(instance);

        instance->encoder.is_running = true;

        FURI_LOG_I(
            TAG,
            "Encoder deserialized: repeat=%u, size_upload=%zu, is_running=%d, front=%zu",
            instance->encoder.repeat,
            instance->encoder.size_upload,
            instance->encoder.is_running,
            instance->encoder.front);

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void kia_protocol_encoder_v2_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderKiaV2* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration kia_protocol_encoder_v2_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderKiaV2* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        FURI_LOG_D(
            TAG,
            "Encoder yield stopped: repeat=%u, is_running=%d",
            instance->encoder.repeat,
            instance->encoder.is_running);
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(instance->encoder.front < 5 || instance->encoder.front == 0) {
        FURI_LOG_D(
            TAG,
            "Encoder yield[%zu]: repeat=%u, size=%zu, level=%d, duration=%lu",
            instance->encoder.front,
            instance->encoder.repeat,
            instance->encoder.size_upload,
            level_duration_get_level(ret),
            level_duration_get_duration(ret));
    }

    if(++instance->encoder.front == instance->encoder.size_upload) {
        instance->encoder.repeat--;
        instance->encoder.front = 0;
        FURI_LOG_I(
            TAG, "Encoder completed one cycle, remaining repeat=%u", instance->encoder.repeat);
    }

    return ret;
}

void* kia_protocol_decoder_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderKiaV2* instance = malloc(sizeof(SubGhzProtocolDecoderKiaV2));
    instance->base.protocol = &kia_protocol_v2;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void kia_protocol_decoder_v2_free(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderKiaV2* instance = context;
    free(instance);
}

void kia_protocol_decoder_v2_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderKiaV2* instance = context;
    instance->decoder.parser_step = KiaV2DecoderStepReset;
    instance->header_count = 0;
    instance->manchester_state = ManchesterStateMid1;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void kia_protocol_decoder_v2_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderKiaV2* instance = context;

    switch(instance->decoder.parser_step) {
    case KiaV2DecoderStepReset:
        if((level) && (DURATION_DIFF(duration, kia_protocol_v2_const.te_long) <
                       kia_protocol_v2_const.te_delta)) {
            instance->decoder.parser_step = KiaV2DecoderStepCheckPreamble;
            instance->decoder.te_last = duration;
            instance->header_count = 0;
            manchester_advance(
                instance->manchester_state,
                ManchesterEventReset,
                &instance->manchester_state,
                NULL);
        }
        break;

    case KiaV2DecoderStepCheckPreamble:
        if(level) // HIGH pulse
        {
            if(DURATION_DIFF(duration, kia_protocol_v2_const.te_long) <
               kia_protocol_v2_const.te_delta) {
                instance->decoder.te_last = duration;
                instance->header_count++;
            } else if(
                DURATION_DIFF(duration, kia_protocol_v2_const.te_short) <
                kia_protocol_v2_const.te_delta) {
                if(instance->header_count >= 100) {
                    instance->header_count = 0;
                    instance->decoder.decode_data = 0;
                    instance->decoder.decode_count_bit = 1;
                    instance->decoder.parser_step = KiaV2DecoderStepCollectRawBits;
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                } else {
                    instance->decoder.te_last = duration;
                }
            } else {
                instance->decoder.parser_step = KiaV2DecoderStepReset;
            }
        } else {
            if(DURATION_DIFF(duration, kia_protocol_v2_const.te_long) <
               kia_protocol_v2_const.te_delta) {
                instance->header_count++;
                instance->decoder.te_last = duration;
            } else if(
                DURATION_DIFF(duration, kia_protocol_v2_const.te_short) <
                kia_protocol_v2_const.te_delta) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = KiaV2DecoderStepReset;
            }
        }
        break;

    case KiaV2DecoderStepCollectRawBits: {
        ManchesterEvent event;

        if(DURATION_DIFF(duration, kia_protocol_v2_const.te_short) <
           kia_protocol_v2_const.te_delta) {
            event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
        } else if(
            DURATION_DIFF(duration, kia_protocol_v2_const.te_long) <
            kia_protocol_v2_const.te_delta) {
            event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
        } else {
            instance->decoder.parser_step = KiaV2DecoderStepReset;
            break;
        }

        bool data_bit;
        if(manchester_advance(
               instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
            instance->decoder.decode_data = (instance->decoder.decode_data << 1) | data_bit;
            instance->decoder.decode_count_bit++;

            if(instance->decoder.decode_count_bit == 53) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                instance->generic.serial = (uint32_t)((instance->generic.data >> 20) & 0xFFFFFFFF);
                instance->generic.btn = (uint8_t)((instance->generic.data >> 16) & 0x0F);

                uint16_t raw_count = (uint16_t)((instance->generic.data >> 4) & 0xFFF);
                instance->generic.cnt = ((raw_count >> 4) | (raw_count << 8)) & 0xFFF;

                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);

                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->header_count = 0;
                instance->decoder.parser_step = KiaV2DecoderStepReset;
            }
        }
        break;
    }
    }
}

uint8_t kia_protocol_decoder_v2_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderKiaV2* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus kia_protocol_decoder_v2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderKiaV2* instance = context;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        if(!flipper_format_write_uint32(flipper_format, "Frequency", &preset->frequency, 1)) {
            break;
        }

        const char* preset_name = furi_string_get_cstr(preset->name);
        const char* short_preset = protopirate_get_short_preset_name(preset_name);
        if(!flipper_format_write_string_cstr(flipper_format, "Preset", short_preset)) {
            break;
        }

        if(!flipper_format_write_string_cstr(
               flipper_format, "Protocol", instance->generic.protocol_name)) {
            break;
        }

        uint32_t bits = instance->generic.data_count_bit;
        if(!flipper_format_write_uint32(flipper_format, "Bit", &bits, 1)) {
            break;
        }

        char key_str[20];
        snprintf(key_str, sizeof(key_str), "%016llX", instance->generic.data);
        if(!flipper_format_write_string_cstr(flipper_format, "Key", key_str)) {
            break;
        }

        uint32_t crc = instance->generic.data & 0x0F;
        if(!flipper_format_write_uint32(flipper_format, "CRC", &crc, 1)) {
            break;
        }

        if(!flipper_format_write_uint32(flipper_format, "Serial", &instance->generic.serial, 1)) {
            break;
        }

        uint32_t temp = instance->generic.btn;
        if(!flipper_format_write_uint32(flipper_format, "Btn", &temp, 1)) {
            break;
        }

        if(!flipper_format_write_uint32(flipper_format, "Cnt", &instance->generic.cnt, 1)) {
            break;
        }

        uint32_t raw_count = (uint16_t)((instance->generic.data >> 4) & 0xFFF);
        if(!flipper_format_write_uint32(flipper_format, "RawCnt", &raw_count, 1)) {
            break;
        }

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

SubGhzProtocolStatus
    kia_protocol_decoder_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderKiaV2* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, kia_protocol_v2_const.min_count_bit_for_found);
}

void kia_protocol_decoder_v2_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderKiaV2* instance = context;

    uint8_t crc = instance->generic.data & 0x0F;

    bool crc_valid = crc == kia_v2_calculate_crc(instance->generic.data);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%013llX\r\n"
        "Sn:%08lX Btn:%X\r\n"
        "Cnt:%03lX CRC:%X - %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->generic.data,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt,
        crc,
        crc_valid ? "OK" : "BAD");
}
