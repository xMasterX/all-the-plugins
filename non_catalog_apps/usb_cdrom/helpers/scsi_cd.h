#pragma once

#include <furi.h>

#define CD_BLOCK_SIZE (0x800UL)
#define CD_CDB_LEN    (16)

// the disc in the tray, as the drive sees it
typedef struct {
    uint32_t blocks; // 0 when the tray is open
    bool dvd;
    uint32_t id; // changes every time a disc goes in or comes out
    bool eject_request; // eject button pressed while the host has the tray locked
} CdMedia;

typedef struct {
    void* ctx;
    // fills in the current disc, and clears a pending eject request
    void (*media)(void* ctx, CdMedia* media);
    bool (*read)(void* ctx, uint32_t lba, uint32_t count, uint8_t* out);
    void (*eject)(void* ctx);
    void (*lock)(void* ctx, bool locked);
} CdDevice;

typedef enum {
    CdDataNone,
    CdDataIn, // device to host
    CdDataOut, // host to device
} CdDataDir;

typedef struct {
    CdDevice dev;
    CdMedia media; // read at the start of each command

    uint32_t media_id; // last disc the host was told about
    bool unit_attention;
    uint8_t media_event; // GET EVENT STATUS NOTIFICATION media event code
    bool locked;

    uint8_t cmd[CD_CDB_LEN];

    // data stage the command expects, worked out from the CDB
    // valid from cmd_start to cmd_end
    CdDataDir data_dir;
    uint32_t data_len;
    bool rx_done;
    bool tx_done;

    uint8_t sk; // sense key
    uint8_t asc; // additional sense code
    uint8_t ascq; // additional sense code qualifier

    // small responses are built in cmd_start and copied out by cmd_tx_data
    uint8_t resp[128];
    uint32_t resp_len;
    uint32_t resp_sent;

    struct {
        uint32_t lba;
        uint32_t count;
    } rw; // READ (10), READ (12) and READ CD
} CdSession;

void scsi_cd_init(CdSession* cd, CdDevice dev);

bool scsi_cd_cmd_start(CdSession* cd, const uint8_t* cmd, uint8_t len);
bool scsi_cd_cmd_rx_data(CdSession* cd, const uint8_t* data, uint32_t len);
bool scsi_cd_cmd_tx_data(CdSession* cd, uint8_t* data, uint32_t* len, uint32_t cap);
bool scsi_cd_cmd_end(CdSession* cd);
