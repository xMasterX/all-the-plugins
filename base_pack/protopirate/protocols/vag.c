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
    furi_assert(instance);

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

    uint8_t type_byte = key1_bytes[0];

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

const SubGhzProtocol vag_protocol = {
    .name = VAG_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_vag_decoder,
    .encoder = NULL,
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

    protocol_vag_load_keys(APP_ASSETS_PATH("vag"));

    return instance;
}

void subghz_protocol_decoder_vag_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderVAG* instance = context;
    free(instance);
}

void subghz_protocol_decoder_vag_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderVAG* instance = context;
    instance->parser_step = VAGDecoderStepReset;
    instance->decrypted = false;
    instance->serial = 0;
    instance->cnt = 0;
    instance->btn = 0;
    instance->check_byte = 0;
}

void subghz_protocol_decoder_vag_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
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
            instance->key2_low = ~instance->data_low;
            instance->key2_high = ~instance->data_high;
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
        instance->key2_low = instance->data_low;
        instance->key2_high = instance->data_high;
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
    furi_assert(context);
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
    furi_assert(context);
    SubGhzProtocolDecoderVAG* instance = context;

    if(!instance->decrypted && instance->data_count_bit >= 80) {
        vag_parse_data(instance);
    }

    uint64_t key1 = ((uint64_t)instance->key1_high << 32) | instance->key1_low;
    uint64_t key2 = ((uint64_t)instance->key2_high << 32) | instance->key2_low;

    instance->generic.data = key1;
    instance->generic.data_count_bit = instance->data_count_bit;

    if(instance->decrypted) {
        instance->generic.serial = instance->serial;
        instance->generic.cnt = instance->cnt;
        instance->generic.btn = instance->btn;
    }

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        flipper_format_write_hex(flipper_format, "Key2", (uint8_t*)&key2, 8);
        uint32_t type = instance->vag_type;
        flipper_format_write_uint32(flipper_format, "Type", &type, 1);
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_vag_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderVAG* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_vag_const.min_count_bit_for_found);

    if(ret == SubGhzProtocolStatusOk) {
        uint64_t key1 = instance->generic.data;
        instance->key1_low = (uint32_t)key1;
        instance->key1_high = (uint32_t)(key1 >> 32);

        uint64_t key2 = 0;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_hex(flipper_format, "Key2", (uint8_t*)&key2, 8)) {
            instance->key2_low = (uint32_t)key2;
            instance->key2_high = (uint32_t)(key2 >> 32);
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

void subghz_protocol_decoder_vag_get_string(void* context, FuriString* output) {
    furi_assert(context);
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

    //instance->generic.protocol_name = vehicle_name;
    // Do not rename protocol ?

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
