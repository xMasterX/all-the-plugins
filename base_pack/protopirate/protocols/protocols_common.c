#include "protocols_common.h"

const char* protopirate_get_short_preset_name(const char* preset_name) {
    if(!strcmp(preset_name, "FuriHalSubGhzPresetOok270Async")) {
        return "AM270";
    } else if(!strcmp(preset_name, "FuriHalSubGhzPresetOok650Async")) {
        return "AM650";
    } else if(!strcmp(preset_name, "FuriHalSubGhzPreset2FSKDev238Async")) {
        return "FM238";
    } else if(!strcmp(preset_name, "FuriHalSubGhzPreset2FSKDev12KAsync")) {
        return "FM12K";
    } else if(!strcmp(preset_name, "FuriHalSubGhzPreset2FSKDev476Async")) {
        return "FM476";
    } else if(!strcmp(preset_name, "FuriHalSubGhzPresetCustom")) {
        return "CUSTOM";
    }
    return preset_name;
}
