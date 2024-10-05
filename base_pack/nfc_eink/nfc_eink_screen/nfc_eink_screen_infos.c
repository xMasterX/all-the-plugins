#include "nfc_eink_screen_infos.h"

typedef bool (*NfcEinkDescriptorCompareDelegate)(
    const NfcEinkScreenInfo* const a,
    const NfcEinkScreenInfo* const b);

static const NfcEinkScreenInfo screen_descriptors[] = {
    {
        .name = "Unknown",
        .width = 0,
        .height = 0,
        .screen_size = NfcEinkScreenSizeUnknown,
        .screen_manufacturer = NfcEinkManufacturerUnknown,
        .screen_type = NfcEinkScreenTypeUnknown,
        .data_block_size = 0,
    },
    {
        .name = "Waveshare 2.13 inch",
        .width = 250,
        .height = 122,
        .screen_size = NfcEinkScreenSize2n13inch,
        .screen_manufacturer = NfcEinkManufacturerWaveshare,
        .screen_type = NfcEinkScreenTypeWaveshare2Color2n13inch,
        .data_block_size = 16,
    },
    {
        .name = "Waveshare 2.7 inch",
        .width = 264,
        .height = 176,
        .screen_size = NfcEinkScreenSize2n7inch,
        .screen_manufacturer = NfcEinkManufacturerWaveshare,
        .screen_type = NfcEinkScreenTypeWaveshare2Color2n7inch,
        .data_block_size = 121,
    },
    {
        .name = "Waveshare 2.9 inch",
        .width = 296,
        .height = 128,
        .screen_size = NfcEinkScreenSize2n9inch,
        .screen_manufacturer = NfcEinkManufacturerWaveshare,
        .screen_type = NfcEinkScreenTypeWaveshare2Color2n9inch,
        .data_block_size = 16,
    },
    {
        .name = "Waveshare 4.2 inch",
        .width = 300,
        .height = 400,
        .screen_size = NfcEinkScreenSize4n2inch,
        .screen_manufacturer = NfcEinkManufacturerWaveshare,
        .screen_type = NfcEinkScreenTypeWaveshare2Color4n2inch,
        .data_block_size = 100,
    },
    {
        .name = "Waveshare 7.5 inch",
        .width = 480,
        .height = 800,
        .screen_size = NfcEinkScreenSize7n5inch,
        .screen_manufacturer = NfcEinkManufacturerWaveshare,
        .screen_type = NfcEinkScreenTypeWaveshare2Color7n5inch,
        .data_block_size = 120,
    },
    {
        .name = "GDEY0154D67",
        .width = 200,
        .height = 200,
        .screen_size = NfcEinkScreenSize1n54inch,
        .screen_manufacturer = NfcEinkManufacturerGoodisplay,
        .screen_type = NfcEinkScreenTypeGoodisplayEY2Color1n54inch,
        .data_block_size = 0xFA,
    },
    {
        .name = "GDEY0213B74",
        .width = 250,
        .height = 122,
        .screen_size = NfcEinkScreenSize2n13inch,
        .screen_manufacturer = NfcEinkManufacturerGoodisplay,
        .screen_type = NfcEinkScreenTypeGoodisplayEY2Color2n13inch,
        .data_block_size = 0xFA,
    },
    {
        .name = "GDEY029T94",
        .width = 296,
        .height = 128,
        .screen_size = NfcEinkScreenSize2n9inch,
        .screen_manufacturer = NfcEinkManufacturerGoodisplay,
        .screen_type = NfcEinkScreenTypeGoodisplayEY2Color2n9inch,
        .data_block_size = 0xFA,
    },
    {
        .name = "GDEY037T03",
        .width = 416,
        .height = 240,
        .screen_size = NfcEinkScreenSize3n71inch,
        .screen_manufacturer = NfcEinkManufacturerGoodisplay,
        .screen_type = NfcEinkScreenTypeGoodisplayEY2Color3n71inch,
        .data_block_size = 0xFA,
    },
};

const NfcEinkScreenInfo* nfc_eink_descriptor_get_by_type(const NfcEinkScreenType type) {
    furi_assert(type < NfcEinkScreenTypeNum);
    furi_assert(type != NfcEinkScreenTypeUnknown);

    const NfcEinkScreenInfo* item = NULL;
    for(uint8_t i = 0; i < COUNT_OF(screen_descriptors); i++) {
        if(screen_descriptors[i].screen_type == type) {
            item = &screen_descriptors[i];
            break;
        }
    }
    return item;
}

static uint8_t nfc_eink_descriptor_filter_by(
    EinkScreenInfoArray_t result,
    const NfcEinkScreenInfo* sample,
    NfcEinkDescriptorCompareDelegate compare) {
    uint8_t count = 0;
    for(uint8_t i = 0; i < COUNT_OF(screen_descriptors); i++) {
        if(compare(&screen_descriptors[i], sample)) {
            EinkScreenInfoArray_push_back(result, &screen_descriptors[i]);
            count++;
        }
    }
    return count;
}

static inline bool nfc_eink_descriptor_compare_by_manufacturer(
    const NfcEinkScreenInfo* const a,
    const NfcEinkScreenInfo* const b) {
    return a->screen_manufacturer == b->screen_manufacturer;
}

static inline bool nfc_eink_descriptor_compare_by_screen_size(
    const NfcEinkScreenInfo* const a,
    const NfcEinkScreenInfo* const b) {
    return a->screen_size == b->screen_size;
}

uint8_t nfc_eink_descriptor_filter_by_manufacturer(
    EinkScreenInfoArray_t result,
    NfcEinkManufacturer manufacturer) {
    furi_assert(result);
    furi_assert(manufacturer < NfcEinkManufacturerNum);
    furi_assert(manufacturer != NfcEinkManufacturerUnknown);

    NfcEinkScreenInfo dummy = {.screen_manufacturer = manufacturer};
    return nfc_eink_descriptor_filter_by(
        result, &dummy, nfc_eink_descriptor_compare_by_manufacturer);
}

uint8_t nfc_eink_descriptor_filter_by_screen_size(
    EinkScreenInfoArray_t result,
    NfcEinkScreenSize screen_size) {
    furi_assert(result);
    furi_assert(screen_size != NfcEinkScreenSizeUnknown);
    furi_assert(screen_size < NfcEinkScreenSizeNum);

    NfcEinkScreenInfo dummy = {.screen_size = screen_size};
    return nfc_eink_descriptor_filter_by(
        result, &dummy, nfc_eink_descriptor_compare_by_screen_size);
}

uint8_t nfc_eink_descriptor_filter_by_screen_type(
    EinkScreenInfoArray_t result,
    NfcEinkScreenType screen_type) {
    furi_assert(result);
    furi_assert(screen_type != NfcEinkScreenTypeUnknown);
    furi_assert(screen_type < NfcEinkScreenTypeNum);

    EinkScreenInfoArray_push_back(result, nfc_eink_descriptor_get_by_type(screen_type));
    return 1;
}

uint8_t nfc_eink_descriptor_get_all_usable(EinkScreenInfoArray_t result) {
    furi_assert(result);
    uint8_t count = 0;
    for(uint8_t i = 0; i < COUNT_OF(screen_descriptors); i++) {
        if(i == NfcEinkScreenTypeUnknown) continue;
        EinkScreenInfoArray_push_back(result, &screen_descriptors[i]);
        count++;
    }
    return count;
}
