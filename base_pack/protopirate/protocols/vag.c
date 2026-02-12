#include "vag.h"
#include "aut64.h"
#include <string.h>
#include <lib/subghz/subghz_keystore.h>

#define TAG "VAGProtocol"

static const SubGhzBlockConst subghz_protocol_vag_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 80,
    .min_count_bit_for_found = 80,
};

#define VAG_KEYS_COUNT 3

static int8_t protocol_vag_keys_loaded = -1;
static struct aut64_key protocol_vag_keys[VAG_KEYS_COUNT];

static void protocol_vag_load_keys(const char* file_name) {
    if(protocol_vag_keys_loaded >= 0) {
        FURI_LOG_I(
            TAG,
            "Already loaded %u keys from %s, skipping load",
            protocol_vag_keys_loaded,
            file_name);
        return;
    }

    FURI_LOG_I(TAG, "Loading keys from %s", file_name);

    protocol_vag_keys_loaded = 0;

    for(uint8_t i = 0; i < VAG_KEYS_COUNT; i++) {
        uint8_t key_packed[AUT64_KEY_STRUCT_PACKED_SIZE];

        if(subghz_keystore_raw_get_data(
               file_name,
               i * AUT64_KEY_STRUCT_PACKED_SIZE,
               key_packed,
               AUT64_KEY_STRUCT_PACKED_SIZE)) {
            aut64_unpack(&protocol_vag_keys[i], key_packed);
            protocol_vag_keys_loaded++;
        } else {
            FURI_LOG_E(TAG, "Unable to load key %u", i);
            break;
        }
    }

    FURI_LOG_I(TAG, "Loaded %u keys", protocol_vag_keys_loaded);
}

static struct aut64_key* protocol_vag_get_key(uint8_t index) {
    for(uint8_t i = 0; i < MIN(protocol_vag_keys_loaded, VAG_KEYS_COUNT); i++) {
        if(protocol_vag_keys[i].index == index) {
            return &protocol_vag_keys[i];
        }
    }

    return NULL;
}

#define VAG_TEA_DELTA  0x9E3779B9U
#define VAG_TEA_ROUNDS 32

static const uint32_t vag_tea_key_schedule[] = {0x0B46502D, 0x5E253718, 0x2BF93A19, 0x622C1206};

static const char* vag_button_name(uint8_t btn) {
    switch(btn) {
    case 1:
        return "Unlock";
    case 2:
        return "Lock";
    case 4:
        return "Trunk";
    default:
        return "Unknown";
    }
}

typedef struct SubGhzProtocolDecoderVAG {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t parser_step;
    uint32_t te_last;
    uint32_t data_low;
    uint32_t data_high;
    uint8_t bit_count;
    uint32_t key1_low;
    uint32_t key1_high;
    uint32_t key2_low;
    uint32_t key2_high;
    uint16_t data_count_bit;
    uint8_t vag_type;
    uint16_t header_count;
    uint8_t mid_count;
    ManchesterState manchester_state;

    uint32_t serial;
    uint32_t cnt;
    uint8_t btn;
    uint8_t check_byte;
    uint8_t key_idx;
    bool decrypted;
} SubGhzProtocolDecoderVAG;

typedef enum {
    VAGDecoderStepReset = 0,
    VAGDecoderStepPreamble1 = 1,
    VAGDecoderStepData1 = 2,
    VAGDecoderStepPreamble2 = 3,
    VAGDecoderStepSync2A = 4,
    VAGDecoderStepSync2B = 5,
    VAGDecoderStepSync2C = 6,
    VAGDecoderStepData2 = 7,
} VAGDecoderStep;

static void vag_tea_decrypt(uint32_t* v0, uint32_t* v1, const uint32_t* key_schedule) {
    uint32_t sum = VAG_TEA_DELTA * VAG_TEA_ROUNDS;
    for(int i = 0; i < VAG_TEA_ROUNDS; i++) {
        *v1 -= (((*v0 << 4) ^ (*v0 >> 5)) + *v0) ^ (sum + key_schedule[(sum >> 11) & 3]);
        sum -= VAG_TEA_DELTA;
        *v0 -= (((*v1 << 4) ^ (*v1 >> 5)) + *v1) ^ (sum + key_schedule[sum & 3]);
    }
}
#ifdef ENABLE_EMULATE_FEATURE
static void vag_tea_encrypt(uint32_t* v0, uint32_t* v1, const uint32_t* key_schedule) {
    uint32_t sum = 0;
    for(int i = 0; i < VAG_TEA_ROUNDS; i++) {
        *v0 += (((*v1 << 4) ^ (*v1 >> 5)) + *v1) ^ (sum + key_schedule[sum & 3]);
        sum += VAG_TEA_DELTA;
        *v1 += (((*v0 << 4) ^ (*v0 >> 5)) + *v0) ^ (sum + key_schedule[(sum >> 11) & 3]);
    }
}
#endif
static bool vag_dispatch_type_1_2(uint8_t dispatch) {
    return (dispatch == 0x2A || dispatch == 0x1C || dispatch == 0x46);
}

static bool vag_dispatch_type_3_4(uint8_t dispatch) {
    return (dispatch == 0x2B || dispatch == 0x1D || dispatch == 0x47);
}

static bool vag_button_valid(const uint8_t* dec) {
    uint8_t dec_byte = dec[7];
    uint8_t dec_btn = (dec_byte >> 4) & 0xF;

    if(dec_btn == 1 || dec_btn == 2 || dec_btn == 4) {
        return true;
    }
    if(dec_byte == 0) {
        return true;
    }
    return false;
}

static bool vag_button_matches(const uint8_t* dec, uint8_t dispatch_byte) {
    uint8_t expected_btn = (dispatch_byte >> 4) & 0xF;
    uint8_t dec_btn = (dec[7] >> 4) & 0xF;

    if(dec_btn == expected_btn) {
        return true;
    }
    if(dec[7] == 0 && expected_btn == 2) {
        return true;
    }
    return false;
}

static void vag_fill_from_decrypted(
    SubGhzProtocolDecoderVAG* instance,
    const uint8_t* dec,
    uint8_t dispatch_byte) {
    uint32_t serial_raw = (uint32_t)dec[0] | ((uint32_t)dec[1] << 8) | ((uint32_t)dec[2] << 16) |
                          ((uint32_t)dec[3] << 24);
    instance->serial = (serial_raw << 24) | ((serial_raw & 0xFF00) << 8) |
                       ((serial_raw >> 8) & 0xFF00) | (serial_raw >> 24);

    instance->cnt = (uint32_t)dec[4] | ((uint32_t)dec[5] << 8) | ((uint32_t)dec[6] << 16);

    instance->btn = (dec[7] >> 4) & 0xF;
    instance->check_byte = dispatch_byte;
    instance->decrypted = true;
}

static bool vag_aut64_decrypt(uint8_t* block, int key_index) {
    struct aut64_key* key = protocol_vag_get_key(key_index + 1);
    if(!key) {
        FURI_LOG_E(TAG, "Key not found: %d", key_index + 1);
        return false;
    }
    aut64_decrypt(*key, block);
    return true;
}

static void vag_parse_data(SubGhzProtocolDecoderVAG* instance) {
    furi_check(instance);

    instance->decrypted = false;
    instance->serial = 0;
    instance->cnt = 0;
    instance->btn = 0;

    uint8_t dispatch_byte = (uint8_t)(instance->key2_low & 0xFF);
    uint8_t key2_high = (uint8_t)((instance->key2_low >> 8) & 0xFF);

    FURI_LOG_I(
        TAG,
        "Parsing VAG type=%d dispatch=0x%02X expected_btn=%d",
        instance->vag_type,
        dispatch_byte,
        (dispatch_byte >> 4) & 0xF);

    uint8_t key1_bytes[8];
    uint32_t key1_low = instance->key1_low;
    uint32_t key1_high = instance->key1_high;

    key1_bytes[0] = (uint8_t)(key1_high >> 24);
    key1_bytes[1] = (uint8_t)(key1_high >> 16);
    key1_bytes[2] = (uint8_t)(key1_high >> 8);
    key1_bytes[3] = (uint8_t)(key1_high);
    key1_bytes[4] = (uint8_t)(key1_low >> 24);
    key1_bytes[5] = (uint8_t)(key1_low >> 16);
    key1_bytes[6] = (uint8_t)(key1_low >> 8);
    key1_bytes[7] = (uint8_t)(key1_low);

#ifndef REMOVE_LOGS
    uint8_t type_byte = key1_bytes[0];
#endif
    uint8_t block[8];
    block[0] = key1_bytes[1];
    block[1] = key1_bytes[2];
    block[2] = key1_bytes[3];
    block[3] = key1_bytes[4];
    block[4] = key1_bytes[5];
    block[5] = key1_bytes[6];
    block[6] = key1_bytes[7];
    block[7] = key2_high;

    FURI_LOG_D(
        TAG,
        "Type byte: 0x%02X, Encrypted block: %02X %02X %02X %02X %02X %02X %02X %02X",
        type_byte,
        block[0],
        block[1],
        block[2],
        block[3],
        block[4],
        block[5],
        block[6],
        block[7]);

    switch(instance->vag_type) {
    case 1:
        if(!vag_dispatch_type_1_2(dispatch_byte)) {
            FURI_LOG_W(TAG, "Type 1: dispatch mismatch 0x%02X", dispatch_byte);
            break;
        }
        {
            uint8_t block_copy[8];

            for(int key_idx = 0; key_idx < 3; key_idx++) {
                memcpy(block_copy, block, 8);
                if(!vag_aut64_decrypt(block_copy, key_idx)) {
                    continue;
                }

                if(vag_button_valid(block_copy)) {
                    instance->serial = ((uint32_t)block_copy[0] << 24) |
                                       ((uint32_t)block_copy[1] << 16) |
                                       ((uint32_t)block_copy[2] << 8) | (uint32_t)block_copy[3];
                    instance->cnt = (uint32_t)block_copy[4] | ((uint32_t)block_copy[5] << 8) |
                                    ((uint32_t)block_copy[6] << 16);
                    instance->btn = block_copy[7];
                    instance->check_byte = dispatch_byte;
                    instance->key_idx = key_idx;
                    instance->decrypted = true;
                    FURI_LOG_I(
                        TAG,
                        "Type 1 key%d decoded: Ser=%08lX Cnt=%06lX Btn=%02X",
                        key_idx,
                        (unsigned long)instance->serial,
                        (unsigned long)instance->cnt,
                        instance->btn);
                    return;
                }
            }
            FURI_LOG_W(
                TAG,
                "Type 1: all keys failed, dec[7]=0x%02X dispatch=0x%02X",
                block_copy[7],
                dispatch_byte);
        }
        break;

    case 2:
        if(!vag_dispatch_type_1_2(dispatch_byte)) {
            FURI_LOG_W(TAG, "Type 2: dispatch mismatch 0x%02X", dispatch_byte);
            break;
        }
        {
            uint32_t v0_orig = ((uint32_t)block[0] << 24) | ((uint32_t)block[1] << 16) |
                               ((uint32_t)block[2] << 8) | (uint32_t)block[3];
            uint32_t v1_orig = ((uint32_t)block[4] << 24) | ((uint32_t)block[5] << 16) |
                               ((uint32_t)block[6] << 8) | (uint32_t)block[7];

            {
                uint32_t v0 = v0_orig;
                uint32_t v1 = v1_orig;

                vag_tea_decrypt(&v0, &v1, vag_tea_key_schedule);

                uint8_t tea_dec[8];
                tea_dec[0] = (uint8_t)(v0 >> 24);
                tea_dec[1] = (uint8_t)(v0 >> 16);
                tea_dec[2] = (uint8_t)(v0 >> 8);
                tea_dec[3] = (uint8_t)(v0);
                tea_dec[4] = (uint8_t)(v1 >> 24);
                tea_dec[5] = (uint8_t)(v1 >> 16);
                tea_dec[6] = (uint8_t)(v1 >> 8);
                tea_dec[7] = (uint8_t)(v1);

                if(!vag_button_matches(tea_dec, dispatch_byte)) {
                    FURI_LOG_W(
                        TAG,
                        "Type 2: XTEA button mismatch dec[7]=0x%02X dispatch=0x%02X",
                        tea_dec[7],
                        dispatch_byte);
                    break;
                }

                vag_fill_from_decrypted(instance, tea_dec, dispatch_byte);
                instance->key_idx = 0xFF;

                FURI_LOG_I(
                    TAG,
                    "Type 2 XTEA decoded: Ser=%08lX Cnt=%06lX Btn=%d",
                    (unsigned long)instance->serial,
                    (unsigned long)instance->cnt,
                    instance->btn);
                return;
            }
        }
        break;

    case 3: {
        uint8_t block_copy[8];

        memcpy(block_copy, block, 8);
        if(vag_aut64_decrypt(block_copy, 2) && vag_button_valid(block_copy)) {
            instance->vag_type = 4;
            instance->key_idx = 2;
            vag_fill_from_decrypted(instance, block_copy, dispatch_byte);
            FURI_LOG_I(
                TAG,
                "Type 3->4 key2 decoded: Ser=%08lX Cnt=%06lX Btn=%d",
                (unsigned long)instance->serial,
                (unsigned long)instance->cnt,
                instance->btn);
            return;
        }

        memcpy(block_copy, block, 8);
        if(vag_aut64_decrypt(block_copy, 1) && vag_button_valid(block_copy)) {
            instance->key_idx = 1;
            vag_fill_from_decrypted(instance, block_copy, dispatch_byte);
            FURI_LOG_I(
                TAG,
                "Type 3 key1 decoded: Ser=%08lX Cnt=%06lX Btn=%d",
                (unsigned long)instance->serial,
                (unsigned long)instance->cnt,
                instance->btn);
            return;
        }

        memcpy(block_copy, block, 8);
        if(vag_aut64_decrypt(block_copy, 0) && vag_button_valid(block_copy)) {
            instance->key_idx = 0;
            vag_fill_from_decrypted(instance, block_copy, dispatch_byte);
            FURI_LOG_I(
                TAG,
                "Type 3 key0 decoded: Ser=%08lX Cnt=%06lX Btn=%d",
                (unsigned long)instance->serial,
                (unsigned long)instance->cnt,
                instance->btn);
            return;
        }

        FURI_LOG_W(
            TAG,
            "Type 3: all keys failed, dec[7]=0x%02X dispatch=0x%02X",
            block_copy[7],
            dispatch_byte);
    } break;

    case 4:
        if(!vag_dispatch_type_3_4(dispatch_byte)) {
            FURI_LOG_W(TAG, "Type 4: dispatch mismatch 0x%02X", dispatch_byte);
            break;
        }
        {
            uint8_t block_copy[8];
            memcpy(block_copy, block, 8);

            if(!vag_aut64_decrypt(block_copy, 2)) {
                FURI_LOG_E(TAG, "Type 4: key 2 not loaded");
                break;
            }
            if(!vag_button_matches(block_copy, dispatch_byte)) {
                FURI_LOG_W(
                    TAG,
                    "Type 4: button mismatch dec[7]=0x%02X dispatch=0x%02X",
                    block_copy[7],
                    dispatch_byte);
                break;
            }
            instance->key_idx = 2;
            vag_fill_from_decrypted(instance, block_copy, dispatch_byte);
            FURI_LOG_I(
                TAG,
                "Type 4 decoded: Ser=%08lX Cnt=%06lX Btn=%d",
                (unsigned long)instance->serial,
                (unsigned long)instance->cnt,
                instance->btn);
        }
        return;

    default:
        FURI_LOG_W(TAG, "Unknown VAG type %d", instance->vag_type);
        break;
    }

    instance->decrypted = false;
    instance->serial = 0;
    instance->cnt = 0;
    instance->btn = 0;
    instance->check_byte = 0;
    FURI_LOG_W(TAG, "VAG decryption failed for type %d", instance->vag_type);
}

const SubGhzProtocolDecoder subghz_protocol_vag_decoder = {
    .alloc = subghz_protocol_decoder_vag_alloc,
    .free = subghz_protocol_decoder_vag_free,
    .feed = subghz_protocol_decoder_vag_feed,
    .reset = subghz_protocol_decoder_vag_reset,
    .get_hash_data = subghz_protocol_decoder_vag_get_hash_data,
    .serialize = subghz_protocol_decoder_vag_serialize,
    .deserialize = subghz_protocol_decoder_vag_deserialize,
    .get_string = subghz_protocol_decoder_vag_get_string,
};
#ifdef ENABLE_EMULATE_FEATURE
const SubGhzProtocolEncoder subghz_protocol_vag_encoder = {
    .alloc = subghz_protocol_encoder_vag_alloc,
    .free = subghz_protocol_encoder_vag_free,
    .deserialize = subghz_protocol_encoder_vag_deserialize,
    .stop = subghz_protocol_encoder_vag_stop,
    .yield = subghz_protocol_encoder_vag_yield,
};
#else
const SubGhzProtocolEncoder subghz_protocol_vag_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};
#endif

const SubGhzProtocol vag_protocol = {
    .name = VAG_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_vag_decoder,
    .encoder = &subghz_protocol_vag_encoder,
};

void* subghz_protocol_decoder_vag_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderVAG* instance = malloc(sizeof(SubGhzProtocolDecoderVAG));
    instance->base.protocol = &vag_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->decrypted = false;
    instance->serial = 0;
    instance->cnt = 0;
    instance->btn = 0;
    instance->check_byte = 0;
    instance->key_idx = 0xFF;

    protocol_vag_load_keys(APP_ASSETS_PATH("vag"));

    return instance;
}

void subghz_protocol_decoder_vag_free(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderVAG* instance = context;
    free(instance);
}

void subghz_protocol_decoder_vag_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderVAG* instance = context;
    instance->parser_step = VAGDecoderStepReset;
    instance->decrypted = false;
    instance->serial = 0;
    instance->cnt = 0;
    instance->btn = 0;
    instance->check_byte = 0;
    instance->key_idx = 0xFF;
}

void subghz_protocol_decoder_vag_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderVAG* instance = context;

    uint32_t diff;
    bool bit_value = false;
    ManchesterEvent event;

    switch(instance->parser_step) {
    case VAGDecoderStepReset:
        if(!level) {
            return;
        }

        if(duration < 300) {
            if((300 - duration) > 79) {
                return;
            }
            goto init_pattern1;
        } else if((duration - 300) > 79) {
            if(duration < 500) {
                diff = 500 - duration;
            } else {
                diff = duration - 500;
            }
            if(diff > 79) {
                return;
            }
            instance->parser_step = VAGDecoderStepPreamble2;
            goto init_common;
        }
    init_pattern1:
        instance->parser_step = VAGDecoderStepPreamble1;
    init_common:
        instance->data_low = 0;
        instance->data_high = 0;
        instance->header_count = 0;
        instance->mid_count = 0;
        instance->bit_count = 0;
        instance->vag_type = 0;
        instance->te_last = duration;
        manchester_advance(
            instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
        return;

    case VAGDecoderStepPreamble1:
        if(level) {
            return;
        }

        if(duration < 300) {
            if((300 - duration) < 80) {
                goto check_preamble1_prev;
            }
            instance->parser_step = VAGDecoderStepReset;
            if(instance->header_count < 201) {
                return;
            }
            duration = 600 - duration;
            goto check_gap1;
        } else {
            if((duration - 300) < 80) {
                goto check_preamble1_prev;
            }
            instance->parser_step = VAGDecoderStepReset;
            if(instance->header_count < 201) {
                return;
            }
            if(duration < 600) {
                duration = 600 - duration;
            } else {
                duration = duration - 600;
            }
        check_gap1:
            if(duration > 79) {
                return;
            }
            if(instance->te_last < 300) {
                diff = 300 - instance->te_last;
            } else {
                diff = instance->te_last - 300;
            }
            if(diff > 79) {
                return;
            }
            instance->parser_step = VAGDecoderStepData1;
            return;
        }

    check_preamble1_prev:
        if(instance->te_last < 300) {
            diff = 300 - instance->te_last;
        } else {
            diff = instance->te_last - 300;
        }
        if(diff > 79) {
            instance->parser_step = VAGDecoderStepReset;
            if(instance->header_count >= 201) {
            }
            return;
        }
        instance->te_last = duration;
        instance->header_count++;
        return;

    case VAGDecoderStepData1:
        if(instance->bit_count < 96) {
            if(duration < 300) {
                if((300 - duration) <= 79) {
                    event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
                    goto process_manchester1;
                }
            } else if((duration - 300) < 80) {
                event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
                goto process_manchester1;
            }

            if(duration < 600) {
                if((600 - duration) <= 79) {
                    event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
                    goto process_manchester1;
                }
            } else if((duration - 600) < 80) {
                event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
                goto process_manchester1;
            }

            goto check_gap1_data;

        process_manchester1:
            if(manchester_advance(
                   instance->manchester_state, event, &instance->manchester_state, &bit_value)) {
                uint32_t carry = (instance->data_low >> 31) & 1;
                instance->data_low = (instance->data_low << 1) | (bit_value ? 1 : 0);
                instance->data_high = (instance->data_high << 1) | carry;
                instance->bit_count++;

                if(instance->bit_count == 15) {
                    if(instance->data_low == 0x2F3F && instance->data_high == 0) {
                        instance->data_low = 0;
                        instance->data_high = 0;
                        instance->bit_count = 0;
                        instance->vag_type = 1;
                    } else if(instance->data_low == 0x2F1C && instance->data_high == 0) {
                        instance->data_low = 0;
                        instance->data_high = 0;
                        instance->bit_count = 0;
                        instance->vag_type = 2;
                    }
                } else if(instance->bit_count == 64) {
                    instance->key1_low = ~instance->data_low;
                    instance->key1_high = ~instance->data_high;
                    instance->data_low = 0;
                    instance->data_high = 0;
                }
            }
            return;
        }

    check_gap1_data:
        if(level) {
            return;
        }
        if(duration < 6000) {
            diff = 6000 - duration;
        } else {
            diff = duration - 6000;
        }
        if(diff >= 4000) {
            return;
        }
        if(instance->bit_count == 80) {
            instance->key2_low = (~instance->data_low) & 0xFFFF;
            instance->key2_high = 0;
            instance->data_count_bit = 80;
            FURI_LOG_I(
                TAG,
                "VAG decoded: Key1:%08lX%08lX Key2:%04X Type:%d",
                (unsigned long)instance->key1_high,
                (unsigned long)instance->key1_low,
                (unsigned int)(instance->key2_low & 0xFFFF),
                instance->vag_type);

            vag_parse_data(instance);

            if(instance->base.callback) {
                instance->base.callback(&instance->base, instance->base.context);
            }
        }
        instance->data_low = 0;
        instance->data_high = 0;
        instance->bit_count = 0;
        instance->parser_step = VAGDecoderStepReset;
        break;

    case VAGDecoderStepPreamble2:
        if(!level) {
            if(duration < 500) {
                diff = 500 - duration;
            } else {
                diff = duration - 500;
            }
            if(diff < 80) {
                if(instance->te_last < 500) {
                    diff = 500 - instance->te_last;
                } else {
                    diff = instance->te_last - 500;
                }
                if(diff < 80) {
                    instance->te_last = duration;
                    instance->header_count++;
                    return;
                }
            }
            instance->parser_step = VAGDecoderStepReset;
            return;
        }

        if(instance->header_count < 41) {
            return;
        }

        if(duration < 1000) {
            diff = 1000 - duration;
        } else {
            diff = duration - 1000;
        }
        if(diff > 79) {
            return;
        }

        if(instance->te_last < 500) {
            diff = 500 - instance->te_last;
        } else {
            diff = instance->te_last - 500;
        }
        if(diff > 79) {
            return;
        }

        instance->te_last = duration;
        instance->parser_step = VAGDecoderStepSync2A;
        break;

    case VAGDecoderStepSync2A:
        if(!level) {
            if(duration < 500) {
                diff = 500 - duration;
            } else {
                diff = duration - 500;
            }
            if(diff < 80) {
                if(instance->te_last < 1000) {
                    diff = 1000 - instance->te_last;
                } else {
                    diff = instance->te_last - 1000;
                }
                if(diff < 80) {
                    instance->te_last = duration;
                    instance->parser_step = VAGDecoderStepSync2B;
                    break;
                }
            }
        }
        instance->parser_step = VAGDecoderStepReset;
        break;

    case VAGDecoderStepSync2B:
        if(level) {
            if(duration < 750) {
                diff = 750 - duration;
            } else {
                diff = duration - 750;
            }
            if(diff < 80) {
                instance->te_last = duration;
                instance->parser_step = VAGDecoderStepSync2C;
                break;
            }
        }
        instance->parser_step = VAGDecoderStepReset;
        break;

    case VAGDecoderStepSync2C:
        if(!level) {
            if(duration < 750) {
                diff = 750 - duration;
            } else {
                diff = duration - 750;
            }
            if(diff <= 79) {
                if(instance->te_last < 750) {
                    diff = 750 - instance->te_last;
                } else {
                    diff = instance->te_last - 750;
                }
                if(diff <= 79) {
                    instance->mid_count++;
                    instance->parser_step = VAGDecoderStepSync2B;

                    if(instance->mid_count == 3) {
                        instance->data_low = 1;
                        instance->data_high = 0;
                        instance->bit_count = 1;
                        manchester_advance(
                            instance->manchester_state,
                            ManchesterEventReset,
                            &instance->manchester_state,
                            NULL);
                        instance->parser_step = VAGDecoderStepData2;
                    }
                    break;
                }
            }
        }
        instance->parser_step = VAGDecoderStepReset;
        break;

    case VAGDecoderStepData2:
        if(duration >= 380 && duration <= 620) {
            event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
            goto process_manchester2;
        }

        if(duration >= 880 && duration <= 1120) {
            event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
            goto process_manchester2;
        }

        goto check_complete2;

    process_manchester2:
        if(manchester_advance(
               instance->manchester_state, event, &instance->manchester_state, &bit_value)) {
            uint32_t carry = (instance->data_low >> 31) & 1;
            instance->data_low = (instance->data_low << 1) | (bit_value ? 1 : 0);
            instance->data_high = (instance->data_high << 1) | carry;
            instance->bit_count++;

            if(instance->bit_count == 64) {
                instance->key1_low = instance->data_low;
                instance->key1_high = instance->data_high;
                instance->data_low = 0;
                instance->data_high = 0;
            }
        }

    check_complete2:
        if(instance->bit_count != 80) {
            break;
        }
        instance->key2_low = instance->data_low & 0xFFFF;
        instance->key2_high = 0;
        instance->data_count_bit = 80;
        instance->vag_type = 3;
        FURI_LOG_I(
            TAG,
            "VAG decoded: Key1:%08lX%08lX Key2:%04X Type:%d",
            (unsigned long)instance->key1_high,
            (unsigned long)instance->key1_low,
            (unsigned int)(instance->key2_low & 0xFFFF),
            instance->vag_type);

        vag_parse_data(instance);

        if(instance->base.callback) {
            instance->base.callback(&instance->base, instance->base.context);
        }
        instance->data_low = 0;
        instance->data_high = 0;
        instance->bit_count = 0;
        instance->parser_step = VAGDecoderStepReset;
        break;

    default:
        instance->parser_step = VAGDecoderStepReset;
        break;
    }
}

uint8_t subghz_protocol_decoder_vag_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderVAG* instance = context;
    uint8_t hash = 0;
    hash ^= (instance->key1_low & 0xFF);
    hash ^= ((instance->key1_low >> 8) & 0xFF);
    hash ^= ((instance->key1_low >> 16) & 0xFF);
    hash ^= ((instance->key1_low >> 24) & 0xFF);
    hash ^= (instance->key1_high & 0xFF);
    hash ^= ((instance->key1_high >> 8) & 0xFF);
    hash ^= ((instance->key1_high >> 16) & 0xFF);
    hash ^= ((instance->key1_high >> 24) & 0xFF);
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_vag_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderVAG* instance = context;

    FURI_LOG_I(TAG, "=== VAG SERIALIZE START ===");
    FURI_LOG_I(
        TAG,
        "Before parse: decrypted=%d serial=%08lX cnt=%06lX btn=%02X type=%d",
        instance->decrypted,
        (unsigned long)instance->serial,
        (unsigned long)instance->cnt,
        instance->btn,
        instance->vag_type);

    if(!instance->decrypted && instance->data_count_bit >= 80) {
        FURI_LOG_I(TAG, "Not decrypted yet, calling vag_parse_data...");
        vag_parse_data(instance);
    }

    FURI_LOG_I(
        TAG,
        "After parse: decrypted=%d serial=%08lX cnt=%06lX btn=%02X type=%d",
        instance->decrypted,
        (unsigned long)instance->serial,
        (unsigned long)instance->cnt,
        instance->btn,
        instance->vag_type);

    uint64_t key1 = ((uint64_t)instance->key1_high << 32) | instance->key1_low;
    uint16_t key2_16bit = (uint16_t)(instance->key2_low & 0xFFFF);

    FURI_LOG_I(
        TAG,
        "Keys: Key1=%08lX%08lX Key2=%08lX%08lX",
        (unsigned long)instance->key1_high,
        (unsigned long)instance->key1_low,
        (unsigned long)instance->key2_high,
        (unsigned long)instance->key2_low);

    instance->generic.data = key1;
    instance->generic.data_count_bit = instance->data_count_bit;

    if(instance->decrypted) {
        instance->generic.serial = instance->serial;
        instance->generic.cnt = instance->cnt;
        instance->generic.btn = instance->btn;
        FURI_LOG_I(TAG, "Decrypted - setting generic fields for serialize");
    } else {
        FURI_LOG_W(TAG, "NOT decrypted - Serial/Cnt/Btn will be 0 in saved file!");
    }

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    FURI_LOG_I(TAG, "Generic serialize returned: %d", ret);

    if(ret == SubGhzProtocolStatusOk) {
        uint8_t key2_bytes[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        key2_bytes[6] = (uint8_t)((key2_16bit >> 8) & 0xFF);
        key2_bytes[7] = (uint8_t)(key2_16bit & 0xFF);
        flipper_format_write_hex(flipper_format, "Key2", key2_bytes, 8);
        FURI_LOG_I(TAG, "Wrote Key2");

        uint32_t type = instance->vag_type;
        flipper_format_write_uint32(flipper_format, "Type", &type, 1);
        FURI_LOG_I(TAG, "Wrote Type: %d", instance->vag_type);

        if(instance->decrypted) {
            flipper_format_write_uint32(flipper_format, "Serial", &instance->serial, 1);
            FURI_LOG_I(TAG, "Wrote Serial: %08lX", (unsigned long)instance->serial);

            uint32_t btn_temp = instance->btn;
            flipper_format_write_uint32(flipper_format, "Btn", &btn_temp, 1);
            FURI_LOG_I(TAG, "Wrote Btn: %02X", instance->btn);

            flipper_format_write_uint32(flipper_format, "Cnt", &instance->cnt, 1);
            FURI_LOG_I(TAG, "Wrote Cnt: %06lX", (unsigned long)instance->cnt);

            uint32_t key_idx_temp = instance->key_idx;
            flipper_format_write_uint32(flipper_format, "KeyIdx", &key_idx_temp, 1);
            FURI_LOG_I(TAG, "Wrote KeyIdx: %d", instance->key_idx);
        }
    }

    FURI_LOG_I(TAG, "=== VAG SERIALIZE END (ret=%d) ===", ret);
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_vag_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderVAG* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_vag_const.min_count_bit_for_found);

    if(ret == SubGhzProtocolStatusOk) {
        uint64_t key1 = instance->generic.data;
        instance->key1_low = (uint32_t)key1;
        instance->key1_high = (uint32_t)(key1 >> 32);

        uint8_t key2_bytes[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_hex(flipper_format, "Key2", key2_bytes, 8)) {
            uint16_t key2_16bit = ((uint16_t)key2_bytes[6] << 8) | (uint16_t)key2_bytes[7];
            instance->key2_low = (uint32_t)key2_16bit & 0xFFFF;
            instance->key2_high = 0;
            FURI_LOG_D(
                TAG,
                "Read Key2 from file: bytes[6]=0x%02X bytes[7]=0x%02X normalized=0x%04X",
                key2_bytes[6],
                key2_bytes[7],
                (unsigned int)key2_16bit);
        }

        uint32_t type = 0;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Type", &type, 1)) {
            instance->vag_type = (uint8_t)type;
        }

        instance->data_count_bit = instance->generic.data_count_bit;

        instance->decrypted = false;
        vag_parse_data(instance);
    }

    return ret;
}

#ifdef ENABLE_EMULATE_FEATURE

typedef struct SubGhzProtocolEncoderVAG {
    SubGhzProtocolEncoderBase base;
    SubGhzBlockGeneric generic;

    uint32_t key1_low;
    uint32_t key1_high;
    uint32_t key2_low;
    uint32_t key2_high;
    uint32_t serial;
    uint32_t cnt;
    uint8_t vag_type;
    uint8_t btn;
    uint8_t dispatch_byte;
    uint8_t key_idx;

    size_t repeat;
    size_t front;
    size_t size_upload;
    LevelDuration* upload;
    bool is_running;
} SubGhzProtocolEncoderVAG;

static bool vag_aut64_encrypt(uint8_t* block, int key_index) {
    struct aut64_key* key = protocol_vag_get_key(key_index + 1);
    if(!key) {
        FURI_LOG_E(TAG, "Key not found for encryption: %d", key_index + 1);
        return false;
    }
    aut64_encrypt(*key, block);
    return true;
}

static uint8_t vag_get_dispatch_byte(uint8_t btn, uint8_t vag_type) {
    if(vag_type == 1 || vag_type == 2) {
        switch(btn) {
        case 0x20:
        case 2:
            return 0x2A;
        case 0x40:
        case 4:
            return 0x46;
        case 0x10:
        case 1:
            return 0x1C;
        default:
            return 0x2A;
        }
    } else {
        switch(btn) {
        case 0x20:
        case 2:
            return 0x2B;
        case 0x40:
        case 4:
            return 0x47;
        case 0x10:
        case 1:
            return 0x1D;
        default:
            return 0x2B;
        }
    }
}

static uint8_t vag_btn_to_byte(uint8_t btn, uint8_t vag_type) {
    if(vag_type == 1) {
        return btn;
    } else {
        switch(btn) {
        case 1:
            return 0x10;
        case 2:
            return 0x20;
        case 4:
            return 0x40;
        default:
            return 0x20;
        }
    }
}

static void vag_encoder_build_type1(SubGhzProtocolEncoderVAG* instance) {
    FURI_LOG_I(TAG, "=== Building Type 1 upload (300us, AUT64) ===");

    size_t index = 0;
    LevelDuration* upload = instance->upload;

    uint8_t btn_byte = vag_btn_to_byte(instance->btn, 1);
    uint8_t dispatch = vag_get_dispatch_byte(btn_byte, 1);
    instance->dispatch_byte = dispatch;

    FURI_LOG_D(TAG, "btn=%02X -> btn_byte=%02X, dispatch=%02X", instance->btn, btn_byte, dispatch);

    uint8_t block[8];
    uint8_t type_byte = (uint8_t)(instance->key1_high >> 24);

    block[0] = (uint8_t)(instance->serial >> 24);
    block[1] = (uint8_t)(instance->serial >> 16);
    block[2] = (uint8_t)(instance->serial >> 8);
    block[3] = (uint8_t)(instance->serial);

    block[4] = (uint8_t)(instance->cnt);
    block[5] = (uint8_t)(instance->cnt >> 8);
    block[6] = (uint8_t)(instance->cnt >> 16);

    block[7] = btn_byte;

    FURI_LOG_I(
        TAG,
        "Plaintext: %02X %02X %02X %02X %02X %02X %02X %02X (ser=%08lX cnt=%06lX btn=%02X)",
        block[0],
        block[1],
        block[2],
        block[3],
        block[4],
        block[5],
        block[6],
        block[7],
        (unsigned long)instance->serial,
        (unsigned long)instance->cnt,
        btn_byte);

    int key_idx = (instance->key_idx != 0xFF) ? instance->key_idx : 0;
    FURI_LOG_D(TAG, "Encrypting with AUT64 key index %d (saved=%d)", key_idx, instance->key_idx);

    if(!vag_aut64_encrypt(block, key_idx)) {
        FURI_LOG_E(TAG, "Type1 AUT64 encryption failed! Key not loaded?");
        instance->size_upload = 0;
        return;
    }

    FURI_LOG_I(
        TAG,
        "Encrypted: %02X %02X %02X %02X %02X %02X %02X %02X",
        block[0],
        block[1],
        block[2],
        block[3],
        block[4],
        block[5],
        block[6],
        block[7]);

    instance->key1_high = ((uint32_t)type_byte << 24) | ((uint32_t)block[0] << 16) |
                          ((uint32_t)block[1] << 8) | (uint32_t)block[2];
    instance->key1_low = ((uint32_t)block[3] << 24) | ((uint32_t)block[4] << 16) |
                         ((uint32_t)block[5] << 8) | (uint32_t)block[6];
    uint32_t key2_upper = ((uint32_t)(block[7] & 0xFF) << 8);
    uint32_t key2_lower = (uint32_t)(dispatch & 0xFF);
    instance->key2_low = (key2_upper | key2_lower) & 0xFFFF;
    instance->key2_high = 0;

    for(int i = 0; i < 220; i++) {
        upload[index++] = level_duration_make(true, 300);
        upload[index++] = level_duration_make(false, 300);
    }
    upload[index++] = level_duration_make(false, 300);
    upload[index++] = level_duration_make(true, 300);

    FURI_LOG_D(TAG, "Preamble: %zu pulses (220 cycles + 2 sync)", index);

    uint16_t prefix = 0xAF3F;
#ifndef REMOVE_LOGS
    size_t prefix_start = index;
#endif
    for(int i = 15; i >= 0; i--) {
        bool bit = (prefix >> i) & 1;
        if(bit) {
            upload[index++] = level_duration_make(true, 300);
            upload[index++] = level_duration_make(false, 300);
        } else {
            upload[index++] = level_duration_make(false, 300);
            upload[index++] = level_duration_make(true, 300);
        }
    }
    FURI_LOG_D(TAG, "Prefix 0x%04X: %zu pulses", prefix, index - prefix_start);

    uint64_t key1 = ((uint64_t)instance->key1_high << 32) | instance->key1_low;
    uint64_t key1_inv = ~key1;
    FURI_LOG_D(
        TAG,
        "Key1: %08lX%08lX -> inverted: %08lX%08lX",
        (unsigned long)(key1 >> 32),
        (unsigned long)(key1 & 0xFFFFFFFF),
        (unsigned long)(key1_inv >> 32),
        (unsigned long)(key1_inv & 0xFFFFFFFF));

#ifndef REMOVE_LOGS
    size_t key1_start = index;
#endif
    for(int i = 63; i >= 0; i--) {
        bool bit = (key1_inv >> i) & 1;
        if(bit) {
            upload[index++] = level_duration_make(true, 300);
            upload[index++] = level_duration_make(false, 300);
        } else {
            upload[index++] = level_duration_make(false, 300);
            upload[index++] = level_duration_make(true, 300);
        }
    }
    FURI_LOG_D(TAG, "Key1: %zu pulses (64 bits)", index - key1_start);

    uint16_t key2 = (uint16_t)(instance->key2_low & 0xFFFF);
    uint16_t key2_inv = ~key2;
    FURI_LOG_D(TAG, "Key2: %04X -> inverted: %04X", key2, key2_inv);
#ifndef REMOVE_LOGS
    size_t key2_start = index;
#endif
    for(int i = 15; i >= 0; i--) {
        bool bit = (key2_inv >> i) & 1;
        if(bit) {
            upload[index++] = level_duration_make(true, 300);
            upload[index++] = level_duration_make(false, 300);
        } else {
            upload[index++] = level_duration_make(false, 300);
            upload[index++] = level_duration_make(true, 300);
        }
    }
    FURI_LOG_D(TAG, "Key2: %zu pulses (16 bits)", index - key2_start);

    upload[index++] = level_duration_make(false, 6000);

    instance->size_upload = index;
    FURI_LOG_I(TAG, "Type1 upload built: %zu pulses (expected: 635)", index);

    if(index != 635) {
        FURI_LOG_W(TAG, "WARNING: Pulse count %zu != expected 635!", index);
    }
}

static void vag_encoder_build_type2(SubGhzProtocolEncoderVAG* instance) {
    FURI_LOG_I(TAG, "=== Building Type 2 upload (300us, TEA) ===");

    size_t index = 0;
    LevelDuration* upload = instance->upload;

    uint8_t btn_byte = vag_btn_to_byte(instance->btn, 2);
    uint8_t dispatch = vag_get_dispatch_byte(btn_byte, 2);
    instance->dispatch_byte = dispatch;

    FURI_LOG_D(TAG, "btn=%02X -> btn_byte=%02X, dispatch=%02X", instance->btn, btn_byte, dispatch);

    uint8_t type_byte = (uint8_t)(instance->key1_high >> 24);

    uint8_t block[8];
    block[0] = (uint8_t)(instance->serial >> 24);
    block[1] = (uint8_t)(instance->serial >> 16);
    block[2] = (uint8_t)(instance->serial >> 8);
    block[3] = (uint8_t)(instance->serial);

    block[4] = (uint8_t)(instance->cnt);
    block[5] = (uint8_t)(instance->cnt >> 8);
    block[6] = (uint8_t)(instance->cnt >> 16);

    block[7] = btn_byte;

    FURI_LOG_I(
        TAG,
        "Plaintext: %02X %02X %02X %02X %02X %02X %02X %02X (ser=%08lX cnt=%06lX btn=%02X)",
        block[0],
        block[1],
        block[2],
        block[3],
        block[4],
        block[5],
        block[6],
        block[7],
        (unsigned long)instance->serial,
        (unsigned long)instance->cnt,
        btn_byte);

    uint32_t v0 = ((uint32_t)block[0] << 24) | ((uint32_t)block[1] << 16) |
                  ((uint32_t)block[2] << 8) | (uint32_t)block[3];
    uint32_t v1 = ((uint32_t)block[4] << 24) | ((uint32_t)block[5] << 16) |
                  ((uint32_t)block[6] << 8) | (uint32_t)block[7];

    FURI_LOG_D(TAG, "TEA input: v0=%08lX v1=%08lX", (unsigned long)v0, (unsigned long)v1);

    vag_tea_encrypt(&v0, &v1, vag_tea_key_schedule);

    FURI_LOG_D(TAG, "TEA output: v0=%08lX v1=%08lX", (unsigned long)v0, (unsigned long)v1);

    block[0] = (uint8_t)(v0 >> 24);
    block[1] = (uint8_t)(v0 >> 16);
    block[2] = (uint8_t)(v0 >> 8);
    block[3] = (uint8_t)(v0);
    block[4] = (uint8_t)(v1 >> 24);
    block[5] = (uint8_t)(v1 >> 16);
    block[6] = (uint8_t)(v1 >> 8);
    block[7] = (uint8_t)(v1);

    FURI_LOG_I(
        TAG,
        "Encrypted: %02X %02X %02X %02X %02X %02X %02X %02X",
        block[0],
        block[1],
        block[2],
        block[3],
        block[4],
        block[5],
        block[6],
        block[7]);

    instance->key1_high = ((uint32_t)type_byte << 24) | ((uint32_t)block[0] << 16) |
                          ((uint32_t)block[1] << 8) | (uint32_t)block[2];
    instance->key1_low = ((uint32_t)block[3] << 24) | ((uint32_t)block[4] << 16) |
                         ((uint32_t)block[5] << 8) | (uint32_t)block[6];
    uint32_t key2_upper = ((uint32_t)(block[7] & 0xFF) << 8);
    uint32_t key2_lower = (uint32_t)(dispatch & 0xFF);
    instance->key2_low = (key2_upper | key2_lower) & 0xFFFF;
    instance->key2_high = 0;

    for(int i = 0; i < 220; i++) {
        upload[index++] = level_duration_make(true, 300);
        upload[index++] = level_duration_make(false, 300);
    }
    upload[index++] = level_duration_make(false, 300);
    upload[index++] = level_duration_make(true, 300);

    FURI_LOG_D(TAG, "Preamble: %zu pulses (220 cycles + 2 sync)", index);

    uint16_t prefix = 0xAF1C;
#ifndef REMOVE_LOGS
    size_t prefix_start = index;
#endif
    for(int i = 15; i >= 0; i--) {
        bool bit = (prefix >> i) & 1;
        if(bit) {
            upload[index++] = level_duration_make(true, 300);
            upload[index++] = level_duration_make(false, 300);
        } else {
            upload[index++] = level_duration_make(false, 300);
            upload[index++] = level_duration_make(true, 300);
        }
    }
    FURI_LOG_D(TAG, "Prefix 0x%04X: %zu pulses", prefix, index - prefix_start);

    uint64_t key1 = ((uint64_t)instance->key1_high << 32) | instance->key1_low;
    uint64_t key1_inv = ~key1;
    FURI_LOG_D(
        TAG,
        "Key1: %08lX%08lX -> inverted: %08lX%08lX",
        (unsigned long)(key1 >> 32),
        (unsigned long)(key1 & 0xFFFFFFFF),
        (unsigned long)(key1_inv >> 32),
        (unsigned long)(key1_inv & 0xFFFFFFFF));
#ifndef REMOVE_LOGS
    size_t key1_start = index;
#endif
    for(int i = 63; i >= 0; i--) {
        bool bit = (key1_inv >> i) & 1;
        if(bit) {
            upload[index++] = level_duration_make(true, 300);
            upload[index++] = level_duration_make(false, 300);
        } else {
            upload[index++] = level_duration_make(false, 300);
            upload[index++] = level_duration_make(true, 300);
        }
    }
    FURI_LOG_D(TAG, "Key1: %zu pulses", index - key1_start);

    uint16_t key2 = (uint16_t)(instance->key2_low & 0xFFFF);
    uint16_t key2_inv = ~key2;
    FURI_LOG_D(TAG, "Key2: %04X -> inverted: %04X", key2, key2_inv);
#ifndef REMOVE_LOGS
    size_t key2_start = index;
#endif
    for(int i = 15; i >= 0; i--) {
        bool bit = (key2_inv >> i) & 1;
        if(bit) {
            upload[index++] = level_duration_make(true, 300);
            upload[index++] = level_duration_make(false, 300);
        } else {
            upload[index++] = level_duration_make(false, 300);
            upload[index++] = level_duration_make(true, 300);
        }
    }
    FURI_LOG_D(TAG, "Key2: %zu pulses", index - key2_start);

    upload[index++] = level_duration_make(false, 6000);

    instance->size_upload = index;
    FURI_LOG_I(TAG, "Type2 upload built: %zu pulses (expected: 635)", index);
}

static void vag_encoder_build_type3_4(SubGhzProtocolEncoderVAG* instance) {
    FURI_LOG_I(TAG, "=== Building Type %d upload (500us, AUT64) ===", instance->vag_type);

    size_t index = 0;
    LevelDuration* upload = instance->upload;

    uint8_t btn_byte = vag_btn_to_byte(instance->btn, instance->vag_type);
    uint8_t dispatch = vag_get_dispatch_byte(btn_byte, instance->vag_type);
    instance->dispatch_byte = dispatch;

    FURI_LOG_D(TAG, "btn=%02X -> btn_byte=%02X, dispatch=%02X", instance->btn, btn_byte, dispatch);

    uint8_t type_byte = (uint8_t)(instance->key1_high >> 24);

    uint8_t block[8];
    block[0] = (uint8_t)(instance->serial >> 24);
    block[1] = (uint8_t)(instance->serial >> 16);
    block[2] = (uint8_t)(instance->serial >> 8);
    block[3] = (uint8_t)(instance->serial);
    block[4] = (uint8_t)(instance->cnt);
    block[5] = (uint8_t)(instance->cnt >> 8);
    block[6] = (uint8_t)(instance->cnt >> 16);
    block[7] = btn_byte;

    FURI_LOG_I(
        TAG,
        "Plaintext: %02X %02X %02X %02X %02X %02X %02X %02X (ser=%08lX cnt=%06lX btn=%02X)",
        block[0],
        block[1],
        block[2],
        block[3],
        block[4],
        block[5],
        block[6],
        block[7],
        (unsigned long)instance->serial,
        (unsigned long)instance->cnt,
        btn_byte);

    int key_idx;
    if(instance->key_idx != 0xFF) {
        key_idx = instance->key_idx;
    } else {
        key_idx = (instance->vag_type == 4) ? 2 : 1;
    }
    FURI_LOG_D(TAG, "Encrypting with AUT64 key index %d (saved=%d)", key_idx, instance->key_idx);

    if(!vag_aut64_encrypt(block, key_idx)) {
        FURI_LOG_E(TAG, "Type%d AUT64 encryption failed! Key not loaded?", instance->vag_type);
        instance->size_upload = 0;
        return;
    }

    FURI_LOG_I(
        TAG,
        "Encrypted: %02X %02X %02X %02X %02X %02X %02X %02X",
        block[0],
        block[1],
        block[2],
        block[3],
        block[4],
        block[5],
        block[6],
        block[7]);

    instance->key1_high = ((uint32_t)type_byte << 24) | ((uint32_t)block[0] << 16) |
                          ((uint32_t)block[1] << 8) | (uint32_t)block[2];
    instance->key1_low = ((uint32_t)block[3] << 24) | ((uint32_t)block[4] << 16) |
                         ((uint32_t)block[5] << 8) | (uint32_t)block[6];
    uint32_t key2_upper = ((uint32_t)(block[7] & 0xFF) << 8);
    uint32_t key2_lower = (uint32_t)(dispatch & 0xFF);
    instance->key2_low = (key2_upper | key2_lower) & 0xFFFF;
    instance->key2_high = 0;

    uint64_t key1 = ((uint64_t)instance->key1_high << 32) | instance->key1_low;
    uint16_t key2 = (uint16_t)(instance->key2_low & 0xFFFF);
    FURI_LOG_D(
        TAG,
        "Key1: %08lX%08lX (NOT inverted for Type 3/4)",
        (unsigned long)(key1 >> 32),
        (unsigned long)(key1 & 0xFFFFFFFF));
    FURI_LOG_D(TAG, "Key2: %04X (NOT inverted for Type 3/4)", key2);
#ifndef REMOVE_LOGS
    uint8_t key1_byte6 = (key1 >> 8) & 0xFF;
    uint8_t key1_byte7 = key1 & 0xFF;
    FURI_LOG_D(
        TAG,
        "Key1 last 2 bytes: %02X %02X (bits: %d%d%d%d%d%d%d%d %d%d%d%d%d%d%d%d)",
        key1_byte6,
        key1_byte7,
        (key1_byte6 >> 7) & 1,
        (key1_byte6 >> 6) & 1,
        (key1_byte6 >> 5) & 1,
        (key1_byte6 >> 4) & 1,
        (key1_byte6 >> 3) & 1,
        (key1_byte6 >> 2) & 1,
        (key1_byte6 >> 1) & 1,
        (key1_byte6 >> 0) & 1,
        (key1_byte7 >> 7) & 1,
        (key1_byte7 >> 6) & 1,
        (key1_byte7 >> 5) & 1,
        (key1_byte7 >> 4) & 1,
        (key1_byte7 >> 3) & 1,
        (key1_byte7 >> 2) & 1,
        (key1_byte7 >> 1) & 1,
        (key1_byte7 >> 0) & 1);
#endif
    for(int repeat = 0; repeat < 2; repeat++) {
#ifndef REMOVE_LOGS
        size_t repeat_start = index;
#endif
        for(int i = 0; i < 45; i++) {
            upload[index++] = level_duration_make(true, 500);
            upload[index++] = level_duration_make(false, 500);
        }
        FURI_LOG_D(
            TAG, "Repeat %d: Preamble %zu pulses (45 cycles)", repeat + 1, index - repeat_start);

        upload[index++] = level_duration_make(true, 1000);
        upload[index++] = level_duration_make(false, 500);

        for(int i = 0; i < 3; i++) {
            upload[index++] = level_duration_make(true, 750);
            upload[index++] = level_duration_make(false, 750);
        }
#ifndef REMOVE_LOGS
        size_t key1_start = index;
        uint8_t consecutive_same = 0;

        bool prev_level = true;
#endif

        for(int i = 63; i >= 0; i--) {
            bool bit = (key1 >> i) & 1;
#ifndef REMOVE_LOGS
            bool first_level = bit ? true : false;

            if(first_level == prev_level) {
                consecutive_same++;
            }
#endif

            if(bit) {
                upload[index++] = level_duration_make(true, 500);
                upload[index++] = level_duration_make(false, 500);
#ifndef REMOVE_LOGS
                prev_level = false;
#endif
            } else {
                upload[index++] = level_duration_make(false, 500);
                upload[index++] = level_duration_make(true, 500);
#ifndef REMOVE_LOGS
                prev_level = true;
#endif
            }
        }
        FURI_LOG_D(
            TAG,
            "Repeat %d: Key1 %zu pulses (64 bits), %u double-width transitions",
            repeat + 1,
            index - key1_start,
            consecutive_same);
#ifndef REMOVE_LOGS
        size_t key2_start = index;
#endif
        bool last_level = false;
        for(int i = 15; i >= 0; i--) {
            bool bit = (key2 >> i) & 1;
            if(bit) {
                upload[index++] = level_duration_make(true, 500);
                upload[index++] = level_duration_make(false, 500);
                last_level = false;
            } else {
                upload[index++] = level_duration_make(false, 500);
                upload[index++] = level_duration_make(true, 500);
                last_level = true;
            }
        }
        FURI_LOG_D(
            TAG,
            "Repeat %d: Key2 %zu pulses (16 bits), ends %s",
            repeat + 1,
            index - key2_start,
            last_level ? "HIGH" : "LOW");

        if(!last_level) {
            upload[index++] = level_duration_make(false, 10000);
            FURI_LOG_D(TAG, "Repeat %d: Gap 10000us LOW (consecutive with data)", repeat + 1);
        } else {
            upload[index++] = level_duration_make(false, 10000);
            FURI_LOG_D(TAG, "Repeat %d: Gap 10000us LOW (after HIGH)", repeat + 1);
        }

        FURI_LOG_D(TAG, "Repeat %d: Total %zu pulses", repeat + 1, index - repeat_start);
    }

    instance->size_upload = index;
    FURI_LOG_I(TAG, "Type%d upload built: %zu pulses (expected: 518)", instance->vag_type, index);

    if(index != 518) {
        FURI_LOG_W(TAG, "WARNING: Pulse count %zu != expected 518!", index);
    }
}
#endif

void subghz_protocol_decoder_vag_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderVAG* instance = context;

    if(!instance->decrypted && instance->data_count_bit >= 80) {
        vag_parse_data(instance);
    }

    uint64_t key1 = ((uint64_t)instance->key1_high << 32) | instance->key1_low;
    uint16_t key2 = (uint16_t)(instance->key2_low & 0xFFFF);

    uint8_t type_byte = (uint8_t)(instance->key1_high >> 24);
    const char* vehicle_name;
    switch(type_byte) {
    case 0x00:
        vehicle_name = "VW Passat";
        break;
    case 0xC0:
        vehicle_name = "VW";
        break;
    case 0xC1:
        vehicle_name = "Audi";
        break;
    case 0xC2:
        vehicle_name = "Seat";
        break;
    case 0xC3:
        vehicle_name = "Skoda";
        break;
    default:
        vehicle_name = "VAG";
        break;
    }

    if(instance->decrypted) {
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key1:%08lX%08lX\r\n"
            "Key2:%04X Btn:%s\r\n"
            "Ser:%08lX Cnt:%06lX\r\n",
            vehicle_name,
            instance->data_count_bit,
            (unsigned long)(key1 >> 32),
            (unsigned long)(key1 & 0xFFFFFFFF),
            key2,
            vag_button_name(instance->btn),
            (unsigned long)instance->serial,
            (unsigned long)instance->cnt);
    } else {
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key1:%08lX%08lX\r\n"
            "Key2:%04X (corrupted)\r\n",
            vehicle_name,
            instance->data_count_bit,
            (unsigned long)(key1 >> 32),
            (unsigned long)(key1 & 0xFFFFFFFF),
            key2);
    }
}

#define VAG_ENCODER_UPLOAD_MAX_SIZE 680

#ifdef ENABLE_EMULATE_FEATURE
void* subghz_protocol_encoder_vag_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    FURI_LOG_I(TAG, "VAG encoder alloc");

    SubGhzProtocolEncoderVAG* instance = malloc(sizeof(SubGhzProtocolEncoderVAG));
    instance->base.protocol = &vag_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->upload = malloc(VAG_ENCODER_UPLOAD_MAX_SIZE * sizeof(LevelDuration));
    instance->size_upload = 0;
    instance->repeat = 10;
    instance->front = 0;
    instance->is_running = false;

    instance->key1_low = 0;
    instance->key1_high = 0;
    instance->key2_low = 0;
    instance->key2_high = 0;
    instance->serial = 0;
    instance->cnt = 0;
    instance->vag_type = 0;
    instance->btn = 0;
    instance->dispatch_byte = 0;
    instance->key_idx = 0xFF;

    protocol_vag_load_keys(APP_ASSETS_PATH("vag"));

    FURI_LOG_I(TAG, "VAG encoder alloc complete, keys loaded: %d", protocol_vag_keys_loaded);

    return instance;
}

void subghz_protocol_encoder_vag_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderVAG* instance = context;
    free(instance->upload);
    free(instance);
}

void subghz_protocol_encoder_vag_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderVAG* instance = context;
    FURI_LOG_I(TAG, "VAG encoder stop (was_running=%d)", instance->is_running);
    instance->is_running = false;
}

LevelDuration subghz_protocol_encoder_vag_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderVAG* instance = context;

    if(!instance->is_running || instance->repeat == 0) {
        if(instance->is_running) {
            FURI_LOG_I(TAG, "VAG encoder transmission complete");
        }
        instance->is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->upload[instance->front];
    instance->front++;

    if(instance->front >= instance->size_upload) {
        instance->front = 0;
        instance->repeat--;
        FURI_LOG_D(TAG, "VAG encoder repeat cycle, %zu remaining", instance->repeat);
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_vag_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderVAG* instance = context;

    FURI_LOG_I(TAG, "=== VAG ENCODER DESERIALIZE START ===");

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            FURI_LOG_E(TAG, "Encoder deserialize: generic failed (ret=%d)", ret);
            break;
        }

        uint64_t key1 = instance->generic.data;
        instance->key1_low = (uint32_t)key1;
        instance->key1_high = (uint32_t)(key1 >> 32);
        FURI_LOG_I(
            TAG,
            "Loaded Key1: %08lX%08lX",
            (unsigned long)instance->key1_high,
            (unsigned long)instance->key1_low);

        uint8_t key2_bytes[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_hex(flipper_format, "Key2", key2_bytes, 8)) {
            FURI_LOG_E(TAG, "Encoder deserialize: Key2 not found in file");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        uint16_t key2_16bit = ((uint16_t)key2_bytes[6] << 8) | (uint16_t)key2_bytes[7];
        instance->key2_low = (uint32_t)key2_16bit & 0xFFFF;
        instance->key2_high = 0;
        FURI_LOG_I(
            TAG,
            "Loaded Key2: bytes[6]=0x%02X bytes[7]=0x%02X normalized=0x%04X (stored as %08lX%08lX)",
            key2_bytes[6],
            key2_bytes[7],
            (unsigned int)key2_16bit,
            (unsigned long)instance->key2_high,
            (unsigned long)instance->key2_low);

        uint32_t type = 0;
        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_uint32(flipper_format, "Type", &type, 1)) {
            FURI_LOG_W(TAG, "Type not found in file, will try to detect");
            type = 0;
        }
        instance->vag_type = (uint8_t)type;
        FURI_LOG_I(TAG, "Loaded Type: %d", instance->vag_type);

        uint32_t file_serial = 0, file_cnt = 0, file_btn = 0, file_key_idx = 0xFF;

        flipper_format_rewind(flipper_format);
        bool has_serial = flipper_format_read_uint32(flipper_format, "Serial", &file_serial, 1);

        flipper_format_rewind(flipper_format);
        bool has_cnt = flipper_format_read_uint32(flipper_format, "Cnt", &file_cnt, 1);

        flipper_format_rewind(flipper_format);
        bool has_btn = flipper_format_read_uint32(flipper_format, "Btn", &file_btn, 1);

        flipper_format_rewind(flipper_format);
        bool has_key_idx = flipper_format_read_uint32(flipper_format, "KeyIdx", &file_key_idx, 1);

        FURI_LOG_I(
            TAG,
            "Direct file read: Serial=%08lX(%s) Cnt=%06lX(%s) Btn=%02lX(%s) KeyIdx=%d(%s)",
            (unsigned long)file_serial,
            has_serial ? "found" : "MISSING",
            (unsigned long)file_cnt,
            has_cnt ? "found" : "MISSING",
            (unsigned long)file_btn,
            has_btn ? "found" : "MISSING",
            (int)file_key_idx,
            has_key_idx ? "found" : "MISSING");

        FURI_LOG_I(
            TAG,
            "Generic values: Ser=%08lX Cnt=%06lX Btn=%02X",
            (unsigned long)instance->generic.serial,
            (unsigned long)instance->generic.cnt,
            instance->generic.btn);

        instance->serial = has_serial ? file_serial : instance->generic.serial;
        instance->cnt = has_cnt ? file_cnt : instance->generic.cnt;
        instance->btn = has_btn ? (uint8_t)file_btn : instance->generic.btn;
        instance->key_idx = has_key_idx ? (uint8_t)file_key_idx : 0xFF;

        FURI_LOG_I(
            TAG,
            "Final values: Ser=%08lX Cnt=%06lX Btn=%02X KeyIdx=%d",
            (unsigned long)instance->serial,
            (unsigned long)instance->cnt,
            instance->btn,
            instance->key_idx);

        if(instance->key_idx == 0xFF) {
            FURI_LOG_I(
                TAG, "KeyIdx not found in file, decoding original signal to find correct key...");

            SubGhzProtocolDecoderVAG decoder;
            memset(&decoder, 0, sizeof(decoder));
            decoder.key1_low = instance->key1_low;
            decoder.key1_high = instance->key1_high;
            decoder.key2_low = instance->key2_low;
            decoder.key2_high = instance->key2_high;
            decoder.vag_type = instance->vag_type;
            decoder.data_count_bit = 80;
            decoder.key_idx = 0xFF;
            vag_parse_data(&decoder);

            if(decoder.decrypted) {
                instance->key_idx = decoder.key_idx;
                FURI_LOG_I(TAG, "Decoded key_idx=%d from original signal", instance->key_idx);

                if(instance->serial == 0 && instance->cnt == 0) {
                    instance->serial = decoder.serial;
                    instance->cnt = decoder.cnt;
                    instance->btn = decoder.btn;
                    FURI_LOG_I(
                        TAG,
                        "Also decoded: Ser=%08lX Cnt=%06lX Btn=%02X",
                        (unsigned long)instance->serial,
                        (unsigned long)instance->cnt,
                        instance->btn);
                }
            } else {
                FURI_LOG_W(
                    TAG,
                    "Could not decode original signal - check if keys are loaded and Type is correct");
            }
        }
#ifndef REMOVE_LOGS
        uint32_t old_cnt = instance->cnt;
#endif
        instance->cnt = (instance->cnt + 1) & 0xFFFFFF;
        FURI_LOG_I(
            TAG,
            "Counter incremented: %06lX -> %06lX",
            (unsigned long)old_cnt,
            (unsigned long)instance->cnt);

        uint8_t type_byte = (uint8_t)(instance->key1_high >> 24);
        if(instance->vag_type == 1 && type_byte == 0x00) {
            FURI_LOG_I(
                TAG,
                "Detected Passat signal (type_byte 0x00), converting type 1 -> type 2 to fix key2 issue");
            instance->vag_type = 2;
        }

        FURI_LOG_I(TAG, "Building upload for type %d...", instance->vag_type);
        switch(instance->vag_type) {
        case 1:
            vag_encoder_build_type1(instance);
            break;
        case 2:
            vag_encoder_build_type2(instance);
            break;
        case 3:
        case 4:
            vag_encoder_build_type3_4(instance);
            break;
        default:
            FURI_LOG_W(TAG, "Unknown type %d, defaulting to type 1", instance->vag_type);
            instance->vag_type = 1;
            vag_encoder_build_type1(instance);
            break;
        }

        if(instance->size_upload == 0) {
            FURI_LOG_E(TAG, "Failed to build upload buffer!");
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }

        FURI_LOG_I(
            TAG,
            "Upload built: %zu pulses, Key1=%08lX%08lX Key2=%04lX",
            instance->size_upload,
            (unsigned long)instance->key1_high,
            (unsigned long)instance->key1_low,
            (unsigned long)(instance->key2_low & 0xFFFF));

        flipper_format_rewind(flipper_format);
        uint8_t key1_bytes[8];
        key1_bytes[0] = (uint8_t)(instance->key1_high >> 24);
        key1_bytes[1] = (uint8_t)(instance->key1_high >> 16);
        key1_bytes[2] = (uint8_t)(instance->key1_high >> 8);
        key1_bytes[3] = (uint8_t)(instance->key1_high);
        key1_bytes[4] = (uint8_t)(instance->key1_low >> 24);
        key1_bytes[5] = (uint8_t)(instance->key1_low >> 16);
        key1_bytes[6] = (uint8_t)(instance->key1_low >> 8);
        key1_bytes[7] = (uint8_t)(instance->key1_low);
        if(!flipper_format_update_hex(flipper_format, "Key", key1_bytes, 8)) {
            FURI_LOG_W(TAG, "Failed to update Key in file (non-fatal)");
        }

        flipper_format_rewind(flipper_format);
        instance->key2_high = 0;
        uint16_t key2_write = (uint16_t)(instance->key2_low & 0xFFFF);
        instance->key2_low = (uint32_t)key2_write;
        uint8_t key2_write_bytes[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        key2_write_bytes[6] = (uint8_t)((key2_write >> 8) & 0xFF);
        key2_write_bytes[7] = (uint8_t)(key2_write & 0xFF);
        if(!flipper_format_update_hex(flipper_format, "Key2", key2_write_bytes, 8)) {
            FURI_LOG_W(TAG, "Failed to update Key2 in file (non-fatal)");
        }

        flipper_format_rewind(flipper_format);
        uint32_t serial32 = instance->serial;
        if(!flipper_format_update_uint32(flipper_format, "Serial", &serial32, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "Serial", &serial32, 1);
        }

        flipper_format_rewind(flipper_format);
        uint32_t cnt32 = instance->cnt;
        if(!flipper_format_update_uint32(flipper_format, "Cnt", &cnt32, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt32, 1);
        }

        flipper_format_rewind(flipper_format);
        uint32_t btn32 = instance->btn;
        if(!flipper_format_update_uint32(flipper_format, "Btn", &btn32, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "Btn", &btn32, 1);
        }

        flipper_format_rewind(flipper_format);
        uint32_t key_idx32 = instance->key_idx;
        if(!flipper_format_update_uint32(flipper_format, "KeyIdx", &key_idx32, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "KeyIdx", &key_idx32, 1);
        }

        flipper_format_rewind(flipper_format);
        uint32_t type32 = instance->vag_type;
        if(!flipper_format_update_uint32(flipper_format, "Type", &type32, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "Type", &type32, 1);
        }

        FURI_LOG_I(
            TAG,
            "Updated file: Serial=%08lX Cnt=%06lX Btn=%02X KeyIdx=%d Type=%d",
            (unsigned long)instance->serial,
            (unsigned long)instance->cnt,
            instance->btn,
            instance->key_idx,
            instance->vag_type);

        instance->repeat = 10;
        instance->front = 0;
        instance->is_running = true;

        FURI_LOG_I(TAG, "=== VAG ENCODER READY (repeat=%zu) ===", instance->repeat);
        ret = SubGhzProtocolStatusOk;
    } while(false);

    if(ret != SubGhzProtocolStatusOk) {
        FURI_LOG_E(TAG, "=== VAG ENCODER DESERIALIZE FAILED (ret=%d) ===", ret);
    }

    return ret;
}
#endif
