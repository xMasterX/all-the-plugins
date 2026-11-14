#pragma once

#include <furi.h>

#define UFI_BLOCK_SIZE (0x200UL)
#define UFI_CDB_LEN    (16)

typedef struct {
    const char* name;
    uint8_t heads;
    uint8_t sectors;
    uint16_t cylinders;
    uint16_t transfer_rate; // kbit/s
    uint8_t medium_type; // UFI mode parameter header medium type code
} UfiGeometry;

typedef struct {
    void* ctx;
    const UfiGeometry* geometry;
    bool write_protect;
    bool (*read)(void* ctx, uint32_t lba, uint32_t count, uint8_t* out);
    bool (*write)(void* ctx, uint32_t lba, uint32_t count, const uint8_t* buf);
    void (*eject)(void* ctx);
} UfiDevice;

typedef enum {
    UfiDataNone,
    UfiDataIn, // device to host
    UfiDataOut, // host to device
} UfiDataDir;

typedef struct {
    UfiDevice dev;

    uint8_t cmd[UFI_CDB_LEN];

    // data stage the command expects, worked out from the CDB
    // valid from cmd_start to cmd_end
    UfiDataDir data_dir;
    uint32_t data_len;
    bool rx_done;
    bool tx_done;

    uint8_t sk; // sense key
    uint8_t asc; // additional sense code
    uint8_t ascq; // additional sense code qualifier

    // small responses are built in cmd_start and copied out by cmd_tx_data
    uint8_t resp[64];
    uint32_t resp_len;
    uint32_t resp_sent;

    struct {
        uint32_t lba;
        uint32_t count;
    } rw; // READ and WRITE, 10 and 12 byte forms
} UfiSession;

const UfiGeometry* ufi_geometry_find(uint64_t image_size);
uint32_t ufi_geometry_blocks(const UfiGeometry* geometry);

bool ufi_cmd_start(UfiSession* ufi, const uint8_t* cmd, uint8_t len);
bool ufi_cmd_rx_data(UfiSession* ufi, const uint8_t* data, uint32_t len);
bool ufi_cmd_tx_data(UfiSession* ufi, uint8_t* data, uint32_t* len, uint32_t cap);
bool ufi_cmd_end(UfiSession* ufi);
