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
                    instance->validation_field = (uint16_t)(instance->decode_data_low & 0xFFFF);
                    instance->key2_low = instance->decode_data_low;
                    instance->key2_high = instance->decode_data_high;
                    instance->mode_serialize = 1;
                    instance->status_flag = 0x80;
                    
                    bool validation_passed = ((instance->validation_field & 0xf) == 0xa);
                    if(validation_passed) {
                        instance->decrypted = 0x50;
                    } else {
                        instance->decrypted = 0x00;
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
                    instance->key2_low = instance->decode_data_low;
                    instance->key2_high = instance->decode_data_high;
                    instance->decrypted = 0x50;
                    instance->mode_serialize = 1;
                    instance->status_flag = 0x80;
                    
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
                
                instance->validation_field = (uint16_t)(instance->decode_data_low & 0xFFFF);
                instance->key2_low = instance->decode_data_low;
                instance->key2_high = instance->decode_data_high;
                instance->mode_serialize = 2;
                instance->status_flag = 0x80;
                
                bool validation_passed = ((instance->validation_field & 0xf) == 0xa);
                if(validation_passed) {
                    instance->decrypted = 0x50;
                } else {
                    instance->decrypted = 0x00;
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
        
        ret = SubGhzProtocolStatusOk;
    } while(false);
    
    furi_string_free(temp_str);
    return ret;
}

void subghz_protocol_decoder_psa_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* instance = context;
    
    uint16_t key2_value = (uint16_t)(instance->key2_low & 0xFFFF);
    
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


