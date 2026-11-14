#pragma once

#include "core/common_defines.h"
#include <stddef.h>
#include <stdint.h>

// The config dump length is firmware specific: known UMC variants answer
// CF <pwd> C6 with 30, 31 or 32 bytes, CRC already stripped by the ISO14443-3A
// poller. Bytes 0..29 hold the documented fields in every variant; the bytes
// past them are undocumented, never interpreted, and written back unchanged.
#define GEN4_CONFIG_SIZE_MIN (30)
#define GEN4_CONFIG_SIZE_MAX (32)
// Buffer size for a config of any length, so always the longest one
#define GEN4_CONFIG_SIZE     (GEN4_CONFIG_SIZE_MAX)
#define GEN4_CONFIG_TAIL_LEN (GEN4_CONFIG_SIZE_MAX - GEN4_CONFIG_SIZE_MIN)
#define GEN4_REVISION_SIZE   (5)

#define GEN4_PASSWORD_LEN (4)
#define GEN4_ATS_MAX_LEN  (16)
#define GEN4_ATQA_LEN     (2)

typedef enum {
    Gen4ProtocolMfClassic = 0x00,
    Gen4ProtocolMfUltralight = 0x01,
} Gen4Protocol;

typedef struct {
    uint8_t bytes[GEN4_PASSWORD_LEN];
} Gen4Password;

typedef enum {
    Gen4UIDLengthSingle = 0x00,
    Gen4UIDLengthDouble = 0x01,
    Gen4UIDLengthTriple = 0x02
} Gen4UIDLength;

typedef enum {
    Gen4UltralightModeUL_EV1 = 0x00,
    Gen4UltralightModeNTAG = 0x01,
    Gen4UltralightModeUL_C = 0x02,
    Gen4UltralightModeUL = 0x03
} Gen4UltralightMode;

typedef enum {
    // for writing original (shadow) data
    Gen4ShadowModePreWrite = 0x00,
    // written data can be read once before restored to original
    Gen4ShadowModeRestore = 0x01,
    // shadow mode disabled
    Gen4ShadowModeDisabled = 0x02,
    // apparently for UL?
    Gen4ShadowModeHighSpeedDisabled = 0x03,
    // work with new UMC. With old UMC is untested
    Gen4ShadowModeSplit = 0x04,
} Gen4ShadowMode;

typedef enum {
    // gen2 card behavour
    Gen4DirectWriteBlock0ModeEnabled = 0x00,
    // common card behavour
    Gen4DirectWriteBlock0ModeDisabled = 0x01,
    // default mode. same behavour as Gen4DirectWriteBlock0ModeActivate
    Gen4DirectWriteBlock0ModeDefault = 0x02,
} Gen4DirectWriteBlock0Mode;

typedef union {
    uint8_t data_raw[GEN4_CONFIG_SIZE];
    struct {
        Gen4Protocol protocol;
        Gen4UIDLength uid_len_code;
        Gen4Password password;
        Gen4ShadowMode gtu_mode;
        uint8_t ats_len;
        uint8_t ats[GEN4_ATS_MAX_LEN]; // mb another class?
        uint8_t atqa[GEN4_ATQA_LEN];
        uint8_t sak;
        Gen4UltralightMode mfu_mode;
        uint8_t total_blocks;
        Gen4DirectWriteBlock0Mode direct_write_mode;
        // Undocumented bytes past the documented layout: 0, 1 or 2 of them
        // depending on firmware, so only readable together with config_size.
        // Not a CRC, the ISO14443-3A poller verifies and trims the CRC for us.
        uint8_t tail[GEN4_CONFIG_TAIL_LEN];
    } FURI_PACKED data_parsed;
} Gen4Config;

// data_parsed only lines up with data_raw while every enum field above is one
// byte wide, which FURI_PACKED does not guarantee on its own.
_Static_assert(sizeof(Gen4Config) == GEN4_CONFIG_SIZE_MAX, "Gen4Config is no longer 32 bytes");
_Static_assert(
    offsetof(Gen4Config, data_parsed.tail) == GEN4_CONFIG_SIZE_MIN,
    "Undocumented config tail moved off the end of the documented fields");

typedef struct {
    uint8_t data[GEN4_REVISION_SIZE];
} Gen4Revision;

typedef struct {
    Gen4Config config;
    // Config bytes the card returned, 0 until read. Only the first config_size
    // bytes of config came from the card, the rest is our own zero padding.
    size_t config_size;
    Gen4Revision revision;
} Gen4;

Gen4* gen4_alloc();

void gen4_free(Gen4* instance);

void gen4_reset(Gen4* instance);

void gen4_copy(Gen4* dest, const Gen4* source);

bool gen4_password_is_set(const Gen4Password* instance);

void gen4_password_reset(Gen4Password* instance);

void gen4_password_copy(Gen4Password* dest, const Gen4Password* source);

const char* gen4_get_shadow_mode_name(Gen4ShadowMode mode);

const char* gen4_get_direct_write_mode_name(Gen4DirectWriteBlock0Mode mode);

const char* gen4_get_uid_len_num(Gen4UIDLength code);

const char* gen4_get_configuration_name(const Gen4Config* config);

// Validates a length read FROM a card. Writing a shorter config is legal, so
// this is not the right check for a config being sent.
bool gen4_config_dump_size_is_valid(size_t config_size);

bool gen4_config_size_is_known(const Gen4* instance);

// Clamped for use as a loop bound over data_raw. Reports a length the card
// never gave, so not for display.
size_t gen4_get_config_size_or_max(const Gen4* instance);
