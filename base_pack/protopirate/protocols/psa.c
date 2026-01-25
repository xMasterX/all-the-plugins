#include "psa.h"

#define TAG "PSAProtocol"

static const SubGhzBlockConst subghz_protocol_psa_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 128,
};

#define PSA_TE_SHORT_125 0x7d
#define PSA_TE_LONG_250 0xfa
#define PSA_TE_END_1000 1000
#define PSA_TE_END_500 500
#define PSA_TOLERANCE_99 99
#define PSA_TOLERANCE_100 100
#define PSA_TOLERANCE_49 0x31
#define PSA_TOLERANCE_50 0x32
#define PSA_PATTERN_THRESHOLD_1 0x46
#define PSA_PATTERN_THRESHOLD_2 0x45
#define PSA_MAX_BITS 0x79
#define PSA_KEY1_BITS 0x40
#define PSA_KEY2_BITS 0x50

#define TEA_DELTA 0x9E3779B9U
#define TEA_ROUNDS 32

#define PSA_BF1_CONST_U4 0x0E0F5C41U
#define PSA_BF1_CONST_U5 0x0F5C4123U

static const uint32_t PSA_BF1_KEY_SCHEDULE[4] = {
    0x4A434915U,
    0xD6743C2BU,
    0x1F29D308U,
    0xE6B79A64U,
};

static const uint32_t PSA_BF2_KEY_SCHEDULE[4] = {
    0x4039C240U,
    0xEDA92CABU,
    0x4306C02AU,
    0x02192A04U,
};

#define PSA_BF1_START 0x23000000U
#define PSA_BF1_END 0x24000000U
#define PSA_BF2_START 0xF3000000U
#define PSA_BF2_END 0xF4000000U

typedef enum {
    PSADecoderState0 = 0,
    PSADecoderState1 = 1,
    PSADecoderState2 = 2,
    PSADecoderState3 = 3,
    PSADecoderState4 = 4,
} PSADecoderState;

struct SubGhzProtocolDecoderPSA {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    
    uint32_t state;
    uint32_t prev_duration;
    
    uint32_t decode_data_low;
    uint32_t decode_data_high;
    uint8_t decode_count_bit;
    
    uint32_t seed;
    uint32_t key1_low;
    uint32_t key1_high;
    uint16_t validation_field;
    uint32_t key2_low;
    uint32_t key2_high;
    
    uint32_t status_flag;
    uint16_t decrypted;
    uint8_t mode_serialize;
    
    uint16_t pattern_counter;
    ManchesterState manchester_state;
    
    uint8_t decrypted_button;
    uint32_t decrypted_serial;
    uint32_t decrypted_counter;
    uint16_t decrypted_crc;
    uint32_t decrypted_seed;
    uint8_t decrypted_type;
};

struct SubGhzProtocolEncoderPSA {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

const SubGhzProtocolDecoder subghz_protocol_psa_decoder = {
    .alloc = subghz_protocol_decoder_psa_alloc,
    .free = subghz_protocol_decoder_psa_free,
    .feed = subghz_protocol_decoder_psa_feed,
    .reset = subghz_protocol_decoder_psa_reset,
    .get_hash_data = subghz_protocol_decoder_psa_get_hash_data,
    .serialize = subghz_protocol_decoder_psa_serialize,
    .deserialize = subghz_protocol_decoder_psa_deserialize,
    .get_string = subghz_protocol_decoder_psa_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_psa_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol psa_protocol = {
    .name = PSA_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Load,
    .decoder = &subghz_protocol_psa_decoder,
    .encoder = &subghz_protocol_psa_encoder,
};

void* subghz_protocol_encoder_psa_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return NULL;
}

void subghz_protocol_encoder_psa_free(void* context) {
    UNUSED(context);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_psa_deserialize(void* context, FlipperFormat* flipper_format) {
    UNUSED(context);
    UNUSED(flipper_format);
    return SubGhzProtocolStatusError;
}

void subghz_protocol_encoder_psa_stop(void* context) {
    UNUSED(context);
}

LevelDuration subghz_protocol_encoder_psa_yield(void* context) {
    UNUSED(context);
    return level_duration_reset();
}

static uint32_t psa_abs_diff(uint32_t a, uint32_t b) {
    if(a < b) {
        return b - a;
    } else {
        return a - b;
    }
}

static void psa_setup_byte_buffer(uint8_t* buffer, uint32_t key1_low, uint32_t key1_high, uint32_t key2_low) {
    for(int i = 0; i < 8; i++) {
        int shift = i * 8;
        uint8_t byte_val;
        if(shift < 32) {
            byte_val = (uint8_t)((key1_low >> shift) & 0xFF);
        } else {
            byte_val = (uint8_t)((key1_high >> (shift - 32)) & 0xFF);
        }
        buffer[7 - i] = byte_val;
    }
    buffer[9] = (uint8_t)(key2_low & 0xFF);
    buffer[8] = (uint8_t)((key2_low >> 8) & 0xFF);
}

static void psa_calculate_checksum(uint8_t* buffer) {
    uint32_t checksum = 0;
    for(int i = 2; i < 8; i++) {
        checksum += (buffer[i] & 0xF) + ((buffer[i] >> 4) & 0xF);
    }
    buffer[11] = (uint8_t)((checksum * 0x10) & 0xFF);
}

static void psa_copy_reverse(uint8_t* temp, uint8_t* source) {
    temp[0] = source[5];
    temp[1] = source[4];
    temp[2] = source[3];
    temp[3] = source[2];
    temp[4] = source[9];
    temp[5] = source[8];
    temp[6] = source[7];
    temp[7] = source[6];
}

static void psa_second_stage_xor_decrypt(uint8_t* buffer) {
    uint8_t temp[8];
    psa_copy_reverse(temp, buffer);
    buffer[2] = temp[0] ^ temp[6];
    buffer[3] = temp[2] ^ temp[0];
    buffer[4] = temp[6] ^ temp[3];
    buffer[5] = temp[7] ^ temp[1];
    buffer[6] = temp[3] ^ temp[1];
    buffer[7] = temp[6] ^ temp[4] ^ temp[5];
}

static void psa_tea_encrypt(uint32_t* v0, uint32_t* v1, const uint32_t* key) {
    uint32_t sum = 0;
    for(int i = 0; i < TEA_ROUNDS; i++) {
        uint32_t k_idx1 = sum & 3;
        uint32_t temp = key[k_idx1] + sum;
        sum = sum + TEA_DELTA;
        *v0 = *v0 + (temp ^ (((*v1 >> 5) ^ (*v1 << 4)) + *v1));
        uint32_t k_idx2 = (sum >> 11) & 3;
        temp = key[k_idx2] + sum;
        *v1 = *v1 + (temp ^ (((*v0 >> 5) ^ (*v0 << 4)) + *v0));
    }
}

static void psa_tea_decrypt(uint32_t* v0, uint32_t* v1, const uint32_t* key) {
    uint32_t sum = TEA_DELTA * TEA_ROUNDS;
    for(int i = 0; i < TEA_ROUNDS; i++) {
        uint32_t k_idx2 = (sum >> 11) & 3;
        uint32_t temp = key[k_idx2] + sum;
        sum = sum - TEA_DELTA;
        *v1 = *v1 - (temp ^ (((*v0 >> 5) ^ (*v0 << 4)) + *v0));
        uint32_t k_idx1 = sum & 3;
        temp = key[k_idx1] + sum;
        *v0 = *v0 - (temp ^ (((*v1 >> 5) ^ (*v1 << 4)) + *v1));
    }
}

static void psa_prepare_tea_data(uint8_t* buffer, uint32_t* w0, uint32_t* w1) {
    *w0 = ((uint32_t)buffer[3] << 16) | ((uint32_t)buffer[2] << 24) |
          ((uint32_t)buffer[4] << 8) | (uint32_t)buffer[5];
    *w1 = ((uint32_t)buffer[7] << 16) | ((uint32_t)buffer[6] << 24) |
          ((uint32_t)buffer[8] << 8) | (uint32_t)buffer[9];
}

static uint8_t psa_calculate_tea_crc(uint32_t v0, uint32_t v1) {
    uint32_t crc = ((v0 >> 24) & 0xFF) + ((v0 >> 16) & 0xFF) +
                   ((v0 >> 8) & 0xFF) + (v0 & 0xFF);
    crc += ((v1 >> 24) & 0xFF) + ((v1 >> 16) & 0xFF) + ((v1 >> 8) & 0xFF);
    return (uint8_t)(crc & 0xFF);
}

static uint16_t psa_calculate_crc16_bf2(uint8_t* buffer, int length) {
    uint16_t crc = 0;
    for(int i = 0; i < length; i++) {
        crc = crc ^ ((uint16_t)buffer[i] << 8);
        for(int j = 0; j < 8; j++) {
            if(crc & 0x8000) {
                crc = (crc << 1) ^ 0x8005;
            } else {
                crc = crc << 1;
            }
            crc = crc & 0xFFFF;
        }
    }
    return crc & 0xFFFF;
}

static void psa_unpack_tea_result_to_buffer(uint8_t* buffer, uint32_t v0, uint32_t v1) {
    buffer[2] = (uint8_t)((v0 >> 24) & 0xFF);
    buffer[3] = (uint8_t)((v0 >> 16) & 0xFF);
    buffer[4] = (uint8_t)((v0 >> 8) & 0xFF);
    buffer[5] = (uint8_t)(v0 & 0xFF);
    buffer[6] = (uint8_t)((v1 >> 24) & 0xFF);
    buffer[7] = (uint8_t)((v1 >> 16) & 0xFF);
    buffer[8] = (uint8_t)((v1 >> 8) & 0xFF);
    buffer[9] = (uint8_t)(v1 & 0xFF);
}

static void psa_extract_fields_mode23(uint8_t* buffer, SubGhzProtocolDecoderPSA* instance) {
    instance->decrypted_button = buffer[8] & 0xF;
    instance->decrypted_serial = ((uint32_t)buffer[3] << 8) | ((uint32_t)buffer[2] << 16) | (uint32_t)buffer[4];
    instance->decrypted_counter = (uint32_t)buffer[6] | ((uint32_t)buffer[5] << 8);
    instance->decrypted_crc = (uint16_t)buffer[7];
    instance->decrypted_type = 0x23;
    instance->decrypted_seed = instance->decrypted_serial;
}

static void psa_extract_fields_mode36(uint8_t* buffer, SubGhzProtocolDecoderPSA* instance) {
    instance->decrypted_button = (buffer[5] >> 4) & 0xF;
    instance->decrypted_serial = ((uint32_t)buffer[3] << 8) | ((uint32_t)buffer[2] << 16) | (uint32_t)buffer[4];
    instance->decrypted_counter = ((uint32_t)buffer[7] << 8) | ((uint32_t)buffer[6] << 16) | 
                                   (uint32_t)buffer[8] | (((uint32_t)buffer[5] & 0xF) << 24);
    instance->decrypted_crc = (uint16_t)buffer[9];
    instance->decrypted_type = 0x36;
    instance->decrypted_seed = instance->decrypted_serial;
}

static bool psa_brute_force_decrypt_bf1(SubGhzProtocolDecoderPSA* instance, uint8_t* buffer, uint32_t w0, uint32_t w1) {
    for(uint32_t counter = PSA_BF1_START; counter < PSA_BF1_END; counter++) {
        uint32_t wk2 = PSA_BF1_CONST_U4;
        uint32_t wk3 = counter;
        psa_tea_encrypt(&wk2, &wk3, PSA_BF1_KEY_SCHEDULE);
        
        uint32_t wk0 = (counter << 8) | 0x0E;
        uint32_t wk1 = PSA_BF1_CONST_U5;
        psa_tea_encrypt(&wk0, &wk1, PSA_BF1_KEY_SCHEDULE);
        
        uint32_t working_key[4] = {wk0, wk1, wk2, wk3};
        
        uint32_t dec_v0 = w0;
        uint32_t dec_v1 = w1;
        psa_tea_decrypt(&dec_v0, &dec_v1, working_key);
        
        if((counter & 0xFFFFFF) == (dec_v0 >> 8)) {
            uint8_t crc = psa_calculate_tea_crc(dec_v0, dec_v1);
            if(crc == (dec_v1 & 0xFF)) {
                psa_unpack_tea_result_to_buffer(buffer, dec_v0, dec_v1);
                psa_extract_fields_mode36(buffer, instance);
                return true;
            }
        }
    }
    return false;
}

static bool psa_brute_force_decrypt_bf2(SubGhzProtocolDecoderPSA* instance, uint8_t* buffer, uint32_t w0, uint32_t w1) {
    for(uint32_t counter = PSA_BF2_START; counter < PSA_BF2_END; counter++) {
        uint32_t working_key[4] = {
            PSA_BF2_KEY_SCHEDULE[0] ^ counter,
            PSA_BF2_KEY_SCHEDULE[1] ^ counter,
            PSA_BF2_KEY_SCHEDULE[2] ^ counter,
            PSA_BF2_KEY_SCHEDULE[3] ^ counter,
        };
        
        uint32_t dec_v0 = w0;
        uint32_t dec_v1 = w1;
        psa_tea_decrypt(&dec_v0, &dec_v1, working_key);
        
        if((counter & 0xFFFFFF) == (dec_v0 >> 8)) {
            psa_unpack_tea_result_to_buffer(buffer, dec_v0, dec_v1);
            
            uint8_t crc_buffer[6] = {
                (uint8_t)((dec_v0 >> 24) & 0xFF),
                (uint8_t)((dec_v0 >> 8) & 0xFF),
                (uint8_t)((dec_v0 >> 16) & 0xFF),
                (uint8_t)(dec_v0 & 0xFF),
                (uint8_t)((dec_v1 >> 24) & 0xFF),
                (uint8_t)((dec_v1 >> 16) & 0xFF),
            };
            uint16_t crc16 = psa_calculate_crc16_bf2(crc_buffer, 6);
            uint16_t expected_crc = (((dec_v1 >> 16) & 0xFF) << 8) | (dec_v1 & 0xFF);
            
            if(crc16 == expected_crc) {
                psa_extract_fields_mode36(buffer, instance);
                return true;
            }
        }
    }
    return false;
}

static bool psa_direct_xor_decrypt(SubGhzProtocolDecoderPSA* instance, uint8_t* buffer) {
    psa_calculate_checksum(buffer);
    uint8_t checksum = buffer[11];
    uint8_t key2_high = buffer[8];
    
    uint8_t validation_result = (checksum ^ key2_high) & 0xF0;
    FURI_LOG_D(TAG, "Direct XOR validation: checksum=0x%02X, key2_high=0x%02X, result=0x%02X", 
        checksum, key2_high, validation_result);
    
    if(validation_result == 0) {
        buffer[13] = buffer[9] ^ buffer[8];
        psa_second_stage_xor_decrypt(buffer);
        psa_extract_fields_mode23(buffer, instance);
        FURI_LOG_D(TAG, "Direct XOR decryption completed");
        return true;
    }
    FURI_LOG_D(TAG, "Direct XOR validation FAILED: (checksum ^ key2_high) & 0xF0 = 0x%02X (expected 0x00)", 
        validation_result);
    return false;
}

static void psa_decrypt_router(SubGhzProtocolDecoderPSA* instance) {
    FURI_LOG_I(TAG, "=== DECRYPTION ROUTER CALLED ===");
    FURI_LOG_I(TAG, "Key1:%08lX%08lX Key2:%04X mode_serialize:0x%02X decrypted:0x%02X", 
        (unsigned long)instance->key1_high, (unsigned long)instance->key1_low, 
        (unsigned int)(instance->key2_low & 0xFFFF), 
        (unsigned int)instance->mode_serialize, (unsigned int)instance->decrypted);
    
    uint8_t buffer[48] = {0};
    
    psa_setup_byte_buffer(buffer, instance->key1_low, instance->key1_high, instance->key2_low);
    
    uint8_t mode = instance->mode_serialize;
    
    if(mode == 1 || mode == 2) {
        if(psa_direct_xor_decrypt(instance, buffer)) {
            mode = 0x23;
            FURI_LOG_I(TAG, "Converted mode %d -> 0x23 (Direct XOR)", instance->mode_serialize);
        } else {
            mode = 0x36;
            FURI_LOG_I(TAG, "Converted mode %d -> 0x36 (TEA Brute Force)", instance->mode_serialize);
        }
    }
    
    if(mode == 0x23) {
        FURI_LOG_I(TAG, "Attempting Direct XOR decryption (Type 0x23)");
        if(psa_direct_xor_decrypt(instance, buffer)) {
            instance->mode_serialize = 0x23;
            instance->decrypted = 0x50;
            FURI_LOG_I(TAG, "Direct XOR decryption SUCCESS");
            return;
        }
        FURI_LOG_W(TAG, "Direct XOR decryption FAILED");
    } else if(mode == 0x36) {
        FURI_LOG_I(TAG, "Attempting TEA brute force decryption (Type 0x36)");
        
        uint32_t w0, w1;
        psa_prepare_tea_data(buffer, &w0, &w1);
        FURI_LOG_D(TAG, "Prepared TEA data: w0=0x%08lX w1=0x%08lX", (unsigned long)w0, (unsigned long)w1);
        
        FURI_LOG_I(TAG, "Starting BF1 brute force...");
        if(psa_brute_force_decrypt_bf1(instance, buffer, w0, w1)) {
            instance->mode_serialize = 0x36;
            instance->decrypted = 0x50;
            FURI_LOG_I(TAG, "BF1 brute force SUCCESS - Btn:%02X Ser:%06lX Cnt:%08lX", 
                (unsigned int)instance->decrypted_button, 
                (unsigned long)instance->decrypted_serial, (unsigned long)instance->decrypted_counter);
            return;
        }
        FURI_LOG_W(TAG, "BF1 brute force FAILED - trying BF2...");
        if(psa_brute_force_decrypt_bf2(instance, buffer, w0, w1)) {
            instance->mode_serialize = 0x36;
            instance->decrypted = 0x50;
            FURI_LOG_I(TAG, "BF2 brute force SUCCESS - Btn:%02X Ser:%06lX Cnt:%08lX", 
                (unsigned int)instance->decrypted_button, 
                (unsigned long)instance->decrypted_serial, (unsigned long)instance->decrypted_counter);
            return;
        }
        FURI_LOG_E(TAG, "Both BF1 and BF2 brute force FAILED");
    } else {
        FURI_LOG_I(TAG, "Mode uninitialized (0x00) - trying direct XOR first");
        if(psa_direct_xor_decrypt(instance, buffer)) {
            instance->mode_serialize = 0x23;
            instance->decrypted = 0x50;
            FURI_LOG_I(TAG, "Direct XOR decryption SUCCESS");
            return;
        }
        
        FURI_LOG_I(TAG, "Direct XOR failed - trying brute force...");
        uint32_t w0, w1;
        psa_prepare_tea_data(buffer, &w0, &w1);
        
        if(psa_brute_force_decrypt_bf1(instance, buffer, w0, w1)) {
            instance->mode_serialize = 0x36;
            instance->decrypted = 0x50;
            FURI_LOG_I(TAG, "BF1 brute force SUCCESS");
            return;
        }
        if(psa_brute_force_decrypt_bf2(instance, buffer, w0, w1)) {
            instance->mode_serialize = 0x36;
            instance->decrypted = 0x50;
            FURI_LOG_I(TAG, "BF2 brute force SUCCESS");
            return;
        }
    }
    
    FURI_LOG_E(TAG, "=== ALL DECRYPTION ATTEMPTS FAILED ===");
    instance->decrypted = 0x00;
}

void* subghz_protocol_decoder_psa_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderPSA* instance = malloc(sizeof(SubGhzProtocolDecoderPSA));
    if(instance) {
        memset(instance, 0, sizeof(SubGhzProtocolDecoderPSA));
        instance->base.protocol = &psa_protocol;
        instance->manchester_state = ManchesterStateMid1;
    }
    return instance;
}

void subghz_protocol_decoder_psa_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* instance = context;
    free(instance);
}

void subghz_protocol_decoder_psa_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* instance = context;
    instance->state = 0;
    instance->status_flag = 0;
    instance->mode_serialize = 0;
    instance->key1_low = 0;
    instance->key1_high = 0;
    instance->key2_low = 0;
    instance->key2_high = 0;
    instance->decode_data_low = 0;
    instance->decode_data_high = 0;
    instance->decode_count_bit = 0;
    instance->pattern_counter = 0;
    instance->manchester_state = ManchesterStateMid1;
    instance->decrypted_button = 0;
    instance->decrypted_serial = 0;
    instance->decrypted_counter = 0;
    instance->decrypted_crc = 0;
    instance->decrypted_seed = 0;
    instance->decrypted_type = 0;
}

void subghz_protocol_decoder_psa_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* instance = context;
    
    uint32_t tolerance;
    uint32_t new_state = instance->state;
    uint32_t prev_dur = instance->prev_duration;
    uint32_t te_short = subghz_protocol_psa_const.te_short;
    uint32_t te_long = subghz_protocol_psa_const.te_long;
    
    switch(instance->state) {
    case PSADecoderState0:
        if(!level) {
            return;
        }
        
        if(duration < te_short) {
            tolerance = te_short - duration;
            if(tolerance > PSA_TOLERANCE_99) {
                if(duration < PSA_TE_SHORT_125) {
                    tolerance = PSA_TE_SHORT_125 - duration;
                } else {
                    tolerance = duration - PSA_TE_SHORT_125;
                }
                if(tolerance > PSA_TOLERANCE_49) {
                    return;
                }
                new_state = PSADecoderState3;
            } else {
                new_state = PSADecoderState1;
            }
        } else {
            tolerance = duration - te_short;
            if(tolerance > PSA_TOLERANCE_99) {
                return;
            }
            new_state = PSADecoderState1;
        }
        
        instance->decode_data_low = 0;
        instance->decode_data_high = 0;
        instance->pattern_counter = 0;
        instance->decode_count_bit = 0;
        instance->mode_serialize = 0;
        instance->prev_duration = duration;
        instance->decrypted_type = 0;
        instance->decrypted_button = 0;
        instance->decrypted_serial = 0;
        instance->decrypted_counter = 0;
        instance->decrypted_crc = 0;
        instance->decrypted_seed = 0;
        instance->decrypted = 0x00;
        manchester_advance(instance->manchester_state, ManchesterEventReset, 
                         &instance->manchester_state, NULL);
        break;
        
    case PSADecoderState1:
        if(level) {
            return;
        }
        
        if(duration < te_short) {
            tolerance = te_short - duration;
            if(tolerance < PSA_TOLERANCE_100) {
                uint32_t prev_diff = psa_abs_diff(prev_dur, te_short);
                if(prev_diff <= PSA_TOLERANCE_99) {
                    instance->pattern_counter++;
                }
                instance->prev_duration = duration;
                return;
            }
        } else {
            tolerance = duration - te_short;
            if(tolerance < PSA_TOLERANCE_100) {
                uint32_t prev_diff = psa_abs_diff(prev_dur, te_short);
                if(prev_diff <= PSA_TOLERANCE_99) {
                    instance->pattern_counter++;
                }
                instance->prev_duration = duration;
                return;
            } else {
                uint32_t long_diff;
                if(duration < te_long) {
                    long_diff = te_long - duration;
                } else {
                    long_diff = duration - te_long;
                }
                if(long_diff < 100) {
                    if(instance->pattern_counter > PSA_PATTERN_THRESHOLD_1) {
                        new_state = PSADecoderState2;
                        instance->decode_data_low = 0;
                        instance->decode_data_high = 0;
                        instance->decode_count_bit = 0;
                        manchester_advance(instance->manchester_state, ManchesterEventReset, 
                                         &instance->manchester_state, NULL);
                        instance->state = new_state;
                    }
                    instance->pattern_counter = 0;
                    instance->prev_duration = duration;
                    return;
                }
            }
        }
        
        new_state = PSADecoderState0;
        instance->pattern_counter = 0;
        break;
        
    case PSADecoderState2:
        if(instance->decode_count_bit >= PSA_MAX_BITS) {
            new_state = PSADecoderState0;
            break;
        }
        
        if(level && instance->decode_count_bit == PSA_KEY2_BITS) {
            if(duration >= 800) {
                uint32_t end_diff;
                if(duration < PSA_TE_END_1000) {
                    end_diff = PSA_TE_END_1000 - duration;
                } else {
                    end_diff = duration - PSA_TE_END_1000;
                }
                if(end_diff <= 199) {
                    uint32_t new_key2_low = instance->decode_data_low;
                    
                    instance->decrypted_type = 0;
                    instance->decrypted_button = 0;
                    instance->decrypted_serial = 0;
                    instance->decrypted_counter = 0;
                    instance->decrypted_crc = 0;
                    instance->decrypted_seed = 0;
                    instance->decrypted = 0x00;
                    
                    instance->validation_field = (uint16_t)(instance->decode_data_low & 0xFFFF);
                    instance->key2_low = new_key2_low;
                    instance->key2_high = instance->decode_data_high;
                    instance->mode_serialize = 1;
                    instance->status_flag = 0x80;
                
                FURI_LOG_I(TAG, "Signal decoded - Key1:%08lX%08lX Key2:%04X ValField:%04X", 
                    (unsigned long)instance->key1_high, (unsigned long)instance->key1_low, 
                    (unsigned int)(instance->key2_low & 0xFFFF), (unsigned int)instance->validation_field);
                
                uint8_t buffer[48] = {0};
                psa_setup_byte_buffer(buffer, instance->key1_low, instance->key1_high, instance->key2_low);
                if(psa_direct_xor_decrypt(instance, buffer)) {
                    instance->mode_serialize = 0x23;
                    instance->decrypted = 0x50;
                    FURI_LOG_I(TAG, "Direct XOR decryption SUCCESS - Type:0x23 Btn:%02X Ser:%06lX Cnt:%08lX", 
                        (unsigned int)instance->decrypted_button, (unsigned long)instance->decrypted_serial, (unsigned long)instance->decrypted_counter);
                } else {
                    instance->decrypted = 0x00;
                    instance->mode_serialize = 0x36;
                    FURI_LOG_I(TAG, "Direct XOR decryption FAILED - marked for brute force (Type:0x36) - deferred to get_string");
                }
                
                if(instance->base.callback) {
                    instance->base.callback(&instance->base, instance->base.context);
                }
                    
                    instance->decode_data_low = 0;
                    instance->decode_data_high = 0;
                    instance->decode_count_bit = 0;
                    new_state = PSADecoderState0;
                    instance->state = new_state;
                    return;
                }
            }
        }
        
        uint8_t manchester_input = 0;
        bool should_process = false;
        
        if(duration < te_short) {
            tolerance = te_short - duration;
            if(tolerance >= PSA_TOLERANCE_100) {
                return;
            }
            manchester_input = ((level ^ 1) & 0x7f) << 1;
            should_process = true;
        } else {
            tolerance = duration - te_short;
            if(tolerance < PSA_TOLERANCE_100) {
                manchester_input = ((level ^ 1) & 0x7f) << 1;
                should_process = true;
            } else if(duration < te_long) {
                uint32_t diff_from_250 = duration - te_short;
                uint32_t diff_from_500 = te_long - duration;
                
                if(diff_from_500 < 150 || diff_from_250 > diff_from_500) {
                    if(level == 0) {
                        manchester_input = 6;
                    } else {
                        manchester_input = 4;
                    }
                    should_process = true;
                } else if(diff_from_250 < 150) {
                    manchester_input = ((level ^ 1) & 0x7f) << 1;
                    should_process = true;
                } else {
                    if(duration > 10000) {
                        new_state = PSADecoderState0;
                        instance->pattern_counter = 0;
                        return;
                    }
                    if(duration >= 350 && duration <= 400) {
                        if(level == 0) {
                            manchester_input = 6;
                        } else {
                            manchester_input = 4;
                        }
                        should_process = true;
                    } else {
                        return;
                    }
                }
            } else {
                uint32_t long_diff = duration - te_long;
                if(long_diff < 100) {
                    if(level == 0) {
                        manchester_input = 6;
                    } else {
                        manchester_input = 4;
                    }
                    should_process = true;
                } else {
                    if(!level) {
                        if(duration > 10000) {
                            new_state = PSADecoderState0;
                            instance->pattern_counter = 0;
                            return;
                        }
                        return;
                    }
                    should_process = false;
                }
            }
        }
        
        if(should_process && instance->decode_count_bit < PSA_KEY2_BITS) {
            bool decoded_bit = false;
            
            if(manchester_advance(instance->manchester_state, 
                               (ManchesterEvent)manchester_input,
                               &instance->manchester_state, 
                               &decoded_bit)) {
                uint32_t carry = (instance->decode_data_low >> 31) & 1;
                instance->decode_data_low = (instance->decode_data_low << 1) | (decoded_bit ? 1 : 0);
                instance->decode_data_high = (instance->decode_data_high << 1) | carry;
                instance->decode_count_bit++;
                
                if(instance->decode_count_bit == PSA_KEY1_BITS) {
                    instance->key1_low = instance->decode_data_low;
                    instance->key1_high = instance->decode_data_high;
                    instance->decode_data_low = 0;
                    instance->decode_data_high = 0;
                }
            }
        }
        
        if(!level) {
            return;
        }
        
        if(!should_process) {
            uint32_t end_diff;
            if(duration < PSA_TE_END_1000) {
                end_diff = PSA_TE_END_1000 - duration;
            } else {
                end_diff = duration - PSA_TE_END_1000;
            }
            if(end_diff <= 199) {
                if(instance->decode_count_bit != PSA_KEY2_BITS) {
                    return;
                }
                
                instance->validation_field = (uint16_t)(instance->decode_data_low & 0xFFFF);
                
                if((instance->validation_field & 0xf) == 0xa) {
                    uint32_t new_key2_low = instance->decode_data_low;
                    
                    instance->decrypted_type = 0;
                    instance->decrypted_button = 0;
                    instance->decrypted_serial = 0;
                    instance->decrypted_counter = 0;
                    instance->decrypted_crc = 0;
                    instance->decrypted_seed = 0;
                    instance->decrypted = 0x00;
                    
                    instance->key2_low = new_key2_low;
                    instance->key2_high = instance->decode_data_high;
                    instance->status_flag = 0x80;
                    
                    FURI_LOG_I(TAG, "Signal decoded (alt path) - Key1:%08lX%08lX Key2:%04X ValField:%04X", 
                        (unsigned long)instance->key1_high, (unsigned long)instance->key1_low, 
                        (unsigned int)(instance->key2_low & 0xFFFF), (unsigned int)instance->validation_field);
                    
                    uint8_t buffer[48] = {0};
                    psa_setup_byte_buffer(buffer, instance->key1_low, instance->key1_high, instance->key2_low);
                    if(psa_direct_xor_decrypt(instance, buffer)) {
                        instance->mode_serialize = 0x23;
                        instance->decrypted = 0x50;
                        FURI_LOG_I(TAG, "Direct XOR decryption SUCCESS - Type:0x23 Btn:%02X Ser:%06lX Cnt:%08lX", 
                            (unsigned int)instance->decrypted_button, (unsigned long)instance->decrypted_serial, (unsigned long)instance->decrypted_counter);
                    } else {
                        instance->decrypted = 0x00;
                        FURI_LOG_I(TAG, "Direct XOR decryption FAILED - marked for brute force (Type:0x36) - deferred to get_string");
                        instance->mode_serialize = 0x36;
                    }
                    
                    if(instance->base.callback) {
                        instance->base.callback(&instance->base, instance->base.context);
                    }
                    
                    instance->decode_data_low = 0;
                    instance->decode_data_high = 0;
                    instance->decode_count_bit = 0;
                    new_state = PSADecoderState0;
                } else {
                    return;
                }
            } else {
                return;
            }
        }
        break;
        
    case PSADecoderState3:
        if(level) {
            return;
        }
        
        if(duration < PSA_TE_SHORT_125) {
            tolerance = PSA_TE_SHORT_125 - duration;
            if(tolerance < PSA_TOLERANCE_50) {
                uint32_t prev_diff = psa_abs_diff(prev_dur, PSA_TE_SHORT_125);
                if(prev_diff <= PSA_TOLERANCE_49) {
                    instance->pattern_counter++;
                } else {
                    instance->pattern_counter = 0;
                }
                instance->prev_duration = duration;
                return;
            }
        } else {
            tolerance = duration - PSA_TE_SHORT_125;
            if(tolerance < PSA_TOLERANCE_50) {
                uint32_t prev_diff = psa_abs_diff(prev_dur, PSA_TE_SHORT_125);
                if(prev_diff <= PSA_TOLERANCE_49) {
                    instance->pattern_counter++;
                } else {
                    instance->pattern_counter = 0;
                }
                instance->prev_duration = duration;
                return;
            } else if(duration >= PSA_TE_LONG_250 && duration < 0x12c) {
                if(instance->pattern_counter > PSA_PATTERN_THRESHOLD_2) {
                    new_state = PSADecoderState4;
                    instance->decode_data_low = 0;
                    instance->decode_data_high = 0;
                    instance->decode_count_bit = 0;
                    manchester_advance(instance->manchester_state, ManchesterEventReset, 
                                     &instance->manchester_state, NULL);
                    instance->state = new_state;
                }
                instance->pattern_counter = 0;
                instance->prev_duration = duration;
                return;
            }
        }
        
        new_state = PSADecoderState0;
        break;
        
    case PSADecoderState4:
        if(instance->decode_count_bit >= PSA_MAX_BITS) {
            new_state = PSADecoderState0;
            break;
        }
        
        if(!level) {
            uint8_t manchester_input;
            bool decoded_bit = false;
            
            if(duration < PSA_TE_SHORT_125) {
                tolerance = PSA_TE_SHORT_125 - duration;
                if(tolerance > PSA_TOLERANCE_49) {
                    return;
                }
                manchester_input = ((level ^ 1) & 0x7f) << 1;
            } else {
                tolerance = duration - PSA_TE_SHORT_125;
                if(tolerance < PSA_TOLERANCE_50) {
                    manchester_input = ((level ^ 1) & 0x7f) << 1;
                } else if(duration >= PSA_TE_LONG_250 && duration < 0x12c) {
                    if(level == 0) {
                        manchester_input = 6;
                    } else {
                        manchester_input = 4;
                    }
                } else {
                    return;
                }
            }
            
            if(manchester_advance(instance->manchester_state, 
                                 (ManchesterEvent)manchester_input,
                                 &instance->manchester_state, 
                                 &decoded_bit)) {
                uint32_t carry = (instance->decode_data_low >> 31) & 1;
                instance->decode_data_low = (instance->decode_data_low << 1) | (decoded_bit ? 1 : 0);
                instance->decode_data_high = (instance->decode_data_high << 1) | carry;
                instance->decode_count_bit++;
                
                if(instance->decode_count_bit == PSA_KEY1_BITS) {
                    instance->key1_low = instance->decode_data_low;
                    instance->key1_high = instance->decode_data_high;
                    instance->decode_data_low = 0;
                    instance->decode_data_high = 0;
                }
            }
        } else if(level) {
            uint32_t end_diff;
            if(duration < PSA_TE_END_500) {
                end_diff = PSA_TE_END_500 - duration;
            } else {
                end_diff = duration - PSA_TE_END_500;
            }
            if(end_diff <= 99) {
                if(instance->decode_count_bit != PSA_KEY2_BITS) {
                    return;
                }
                
                uint32_t new_key2_low = instance->decode_data_low;
                
                instance->decrypted_type = 0;
                instance->decrypted_button = 0;
                instance->decrypted_serial = 0;
                instance->decrypted_counter = 0;
                instance->decrypted_crc = 0;
                instance->decrypted_seed = 0;
                instance->decrypted = 0x00;
                
                instance->validation_field = (uint16_t)(instance->decode_data_low & 0xFFFF);
                instance->key2_low = new_key2_low;
                instance->key2_high = instance->decode_data_high;
                instance->mode_serialize = 2;
                instance->status_flag = 0x80;
                
                FURI_LOG_I(TAG, "Signal decoded (state4) - Key1:%08lX%08lX Key2:%04X ValField:%04X", 
                    (unsigned long)instance->key1_high, (unsigned long)instance->key1_low, 
                    (unsigned int)(instance->key2_low & 0xFFFF), (unsigned int)instance->validation_field);
                
                uint8_t buffer[48] = {0};
                psa_setup_byte_buffer(buffer, instance->key1_low, instance->key1_high, instance->key2_low);
                if(psa_direct_xor_decrypt(instance, buffer)) {
                    instance->mode_serialize = 0x23;
                    instance->decrypted = 0x50;
                    FURI_LOG_I(TAG, "Direct XOR decryption SUCCESS - Type:0x23 Btn:%02X Ser:%06lX Cnt:%08lX", 
                        (unsigned int)instance->decrypted_button, (unsigned long)instance->decrypted_serial, (unsigned long)instance->decrypted_counter);
                } else {
                    instance->decrypted = 0x00;
                    instance->mode_serialize = 0x36;
                    FURI_LOG_I(TAG, "Direct XOR decryption FAILED - marked for brute force (Type:0x36) - deferred to get_string");
                }
                
                if(instance->base.callback) {
                    instance->base.callback(&instance->base, instance->base.context);
                }
                
                instance->decode_data_low = 0;
                instance->decode_data_high = 0;
                instance->decode_count_bit = 0;
                new_state = PSADecoderState0;
                instance->state = new_state;
                return;
            } else {
                return;
            }
        }
        break;
    }
    
    instance->state = new_state;
    instance->prev_duration = duration;
}

uint8_t subghz_protocol_decoder_psa_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* instance = context;
    uint64_t combined_data = ((uint64_t)instance->key1_high << 32) | instance->key1_low;
    SubGhzBlockDecoder decoder = {
        .decode_data = combined_data,
        .decode_count_bit = 64
    };
    return subghz_protocol_blocks_get_hash_data(&decoder, 16);
}

SubGhzProtocolStatus subghz_protocol_decoder_psa_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* instance = context;
    
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        if(!flipper_format_write_uint32(flipper_format, "Frequency", &preset->frequency, 1)) break;
        
        if(!flipper_format_write_string_cstr(
               flipper_format, "Preset", furi_string_get_cstr(preset->name)))
            break;
        
        if(!flipper_format_write_string_cstr(
               flipper_format, "Protocol", instance->base.protocol->name))
            break;
        
        uint32_t bits = 128;
        if(!flipper_format_write_uint32(flipper_format, "Bit", &bits, 1)) break;
        
        char key1_str[20];
        uint64_t key1 = ((uint64_t)instance->key1_high << 32) | instance->key1_low;
        snprintf(key1_str, sizeof(key1_str), "%016llX", key1);
        if(!flipper_format_write_string_cstr(flipper_format, "Key", key1_str)) break;
        
        char key2_str[20];
        uint64_t key2 = ((uint64_t)instance->key2_high << 32) | instance->key2_low;
        snprintf(key2_str, sizeof(key2_str), "%016llX", key2);
        if(!flipper_format_write_string_cstr(flipper_format, "Key_2", key2_str)) break;
        
        ret = SubGhzProtocolStatusOk;
    } while(false);
    
    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_psa_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* instance = context;
    
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    FuriString* temp_str = furi_string_alloc();
    
    do {
        if(!flipper_format_read_string(flipper_format, "Key", temp_str)) {
            break;
        }
        
        const char* key1_str = furi_string_get_cstr(temp_str);
        uint64_t key1 = 0;
        size_t str_len = strlen(key1_str);
        for(size_t i = 0; i < str_len && i < 16; i++) {
            char c = key1_str[i];
            if(c == ' ') continue;
            
            uint8_t nibble;
            if(c >= '0' && c <= '9') {
                nibble = c - '0';
            } else if(c >= 'A' && c <= 'F') {
                nibble = c - 'A' + 10;
            } else if(c >= 'a' && c <= 'f') {
                nibble = c - 'a' + 10;
            } else {
                break;
            }
            key1 = (key1 << 4) | nibble;
        }
        instance->key1_low = (uint32_t)(key1 & 0xFFFFFFFF);
        instance->key1_high = (uint32_t)((key1 >> 32) & 0xFFFFFFFF);
        
        if(!flipper_format_read_string(flipper_format, "Key_2", temp_str)) {
            break;
        }
        
        const char* key2_str = furi_string_get_cstr(temp_str);
        uint64_t key2 = 0;
        str_len = strlen(key2_str);
        for(size_t i = 0; i < str_len && i < 16; i++) {
            char c = key2_str[i];
            if(c == ' ') continue;
            
            uint8_t nibble;
            if(c >= '0' && c <= '9') {
                nibble = c - '0';
            } else if(c >= 'A' && c <= 'F') {
                nibble = c - 'A' + 10;
            } else if(c >= 'a' && c <= 'f') {
                nibble = c - 'a' + 10;
            } else {
                break;
            }
            key2 = (key2 << 4) | nibble;
        }
        instance->key2_low = (uint32_t)(key2 & 0xFFFFFFFF);
        instance->key2_high = (uint32_t)((key2 >> 32) & 0xFFFFFFFF);
        
        instance->status_flag = 0x80;
        
        psa_decrypt_router(instance);
        
        ret = SubGhzProtocolStatusOk;
    } while(false);
    
    furi_string_free(temp_str);
    return ret;
}

void subghz_protocol_decoder_psa_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* instance = context;
    
    if(instance->status_flag == 0x80 && 
       (instance->key1_low != 0 || instance->key1_high != 0) &&
       instance->decrypted_type == 0) {
        FURI_LOG_I(TAG, "get_string: Calling decryption router (decrypted_type=0)");
        psa_decrypt_router(instance);
    } else {
        FURI_LOG_D(TAG, "get_string: Skipping router - status_flag=0x%08lX decrypted_type=0x%02X", 
            (unsigned long)instance->status_flag, (unsigned int)instance->decrypted_type);
    }
    
    uint16_t key2_value = (uint16_t)(instance->key2_low & 0xFFFF);
    
    if(instance->decrypted == 0x50 && instance->decrypted_type != 0) {
        if(instance->decrypted_type == 0x23) {
            furi_string_printf(
                output,
                "%s %dbit\r\n"
                "Key1:%08lX%08lX\r\n"
                "Key2:%04X\r\n"
                "Btn:%01X\r\n"
                "Ser:%06lX\r\n"
                "Cnt:%04lX\r\n"
                "CRC:%02X\r\n"
                "Type:%02X\r\n"
                "Sd:%06lX",
                instance->base.protocol->name,
                128,
                instance->key1_high,
                instance->key1_low,
                key2_value,
                instance->decrypted_button,
                instance->decrypted_serial,
                (uint32_t)instance->decrypted_counter,
                instance->decrypted_crc,
                instance->decrypted_type,
                instance->decrypted_seed);
        } else if(instance->decrypted_type == 0x36) {
            furi_string_printf(
                output,
                "%s %dbit\r\n"
                "Key1:%08lX%08lX\r\n"
                "Key2:%04X\r\n"
                "Btn:%02X\r\n"
                "Ser:%06lX\r\n"
                "Cnt:%08lX\r\n"
                "CRC:%02X\r\n"
                "Type:%02X\r\n"
                "Sd:%06lX",
                instance->base.protocol->name,
                128,
                instance->key1_high,
                instance->key1_low,
                key2_value,
                instance->decrypted_button,
                instance->decrypted_serial,
                instance->decrypted_counter,
                instance->decrypted_crc,
                instance->decrypted_type,
                instance->decrypted_seed);
        } else {
            furi_string_printf(
                output,
                "%s %dbit\r\n"
                "Key1:%08lX%08lX\r\n"
                "Key2:%X",
                instance->base.protocol->name,
                128,
                instance->key1_high,
                instance->key1_low,
                key2_value);
        }
    } else {
        furi_string_printf(
            output,
            "%s %dbit\r\n"
            "Key1:%08lX%08lX\r\n"
            "Key2:%X",
            instance->base.protocol->name,
            128,
            instance->key1_high,
            instance->key1_low,
            key2_value);
    }
}


