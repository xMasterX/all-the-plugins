#pragma once
#include <furi.h>
#include <m-array.h>

typedef enum {
    NfcEinkScreenSizeUnknown,
    NfcEinkScreenSize2n13inch,
    NfcEinkScreenSize2n9inch,
    NfcEinkScreenSize1n54inch,
    NfcEinkScreenSize3n71inch,
    NfcEinkScreenSize2n7inch,
    NfcEinkScreenSize4n2inch,
    NfcEinkScreenSize7n5inch,

    NfcEinkScreenSizeNum
} NfcEinkScreenSize;

typedef enum {
    NfcEinkManufacturerWaveshare,
    NfcEinkManufacturerGoodisplay,

    NfcEinkManufacturerNum,
    NfcEinkManufacturerUnknown
} NfcEinkManufacturer;

typedef enum {
    NfcEinkScreenTypeUnknown,
    NfcEinkScreenTypeGoodisplayEY2Color1n54inch,
    NfcEinkScreenTypeGoodisplayEY2Color2n13inch,
    NfcEinkScreenTypeGoodisplayEY2Color2n9inch,
    NfcEinkScreenTypeGoodisplayEY2Color3n71inch,
    NfcEinkScreenTypeGoodisplayEY2Color4n2inch,

    NfcEinkScreenTypeGoodisplayEW2Color1n54inch,
    NfcEinkScreenTypeGoodisplayEW2Color2n13inch,
    NfcEinkScreenTypeGoodisplayEW2Color2n9inch,
    NfcEinkScreenTypeGoodisplayEW2Color4n2inch,

    NfcEinkScreenTypeGoodisplayEY3Color1n54inch,
    NfcEinkScreenTypeGoodisplayEY3Color2n13inch,
    NfcEinkScreenTypeGoodisplayEY3Color2n9inch,
    NfcEinkScreenTypeGoodisplayEY3Color4n2inch,

    NfcEinkScreenTypeGoodisplayEW3Color2n13inch,
    NfcEinkScreenTypeGoodisplayEW3Color2n9inch,
    NfcEinkScreenTypeGoodisplayEQ3Color4n2inch,
    //-----------------------------------------------
    NfcEinkScreenTypeWaveshare2Color2n13inch,
    NfcEinkScreenTypeWaveshare2Color2n7inch,
    NfcEinkScreenTypeWaveshare2Color2n9inch,
    NfcEinkScreenTypeWaveshare2Color4n2inch,
    NfcEinkScreenTypeWaveshare2Color7n5inch,
    NfcEinkScreenTypeWaveshare2ColorHD7n5inch,

    NfcEinkScreenTypeWaveshare3Color1n54inch,
    NfcEinkScreenTypeWaveshare3Color2n9inch,
    //All new screens can be added here

    NfcEinkScreenTypeNum
} NfcEinkScreenType;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t data_block_size;
    NfcEinkScreenType screen_type;
    NfcEinkScreenSize screen_size;
    NfcEinkManufacturer screen_manufacturer;
    const char* name;
} NfcEinkScreenInfo;

#define M_ARRAY_SIZE (sizeof(NfcEinkScreenInfo*) * NfcEinkScreenTypeNum)
#define M_INIT(a)    ((a) = malloc(M_ARRAY_SIZE))

#define M_INIT_SET(new, old)                          \
    do {                                              \
        M_INIT(new);                                  \
        memcpy((void*)new, (void*)old, M_ARRAY_SIZE); \
        free((void*)old);                             \
    } while(false)

#define M_CLEAR(a) (free((void*)a))

#define M_DESCRIPTOR_ARRAY_OPLIST \
    (INIT(M_INIT), INIT_SET(M_INIT_SET), CLEAR(M_CLEAR), TYPE(const NfcEinkScreenInfo*))

ARRAY_DEF(EinkScreenInfoArray, const NfcEinkScreenInfo*, M_DESCRIPTOR_ARRAY_OPLIST);

const NfcEinkScreenInfo* nfc_eink_descriptor_get_by_type(NfcEinkScreenType type);

uint8_t nfc_eink_descriptor_get_all_usable(EinkScreenInfoArray_t result);

uint8_t nfc_eink_descriptor_filter_by_manufacturer(
    EinkScreenInfoArray_t result,
    NfcEinkManufacturer manufacturer);

uint8_t nfc_eink_descriptor_filter_by_screen_size(
    EinkScreenInfoArray_t result,
    NfcEinkScreenSize screen_size);

uint8_t nfc_eink_descriptor_filter_by_screen_type(
    EinkScreenInfoArray_t result,
    NfcEinkScreenType screen_type);
