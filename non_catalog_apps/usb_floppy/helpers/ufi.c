#include "ufi.h"

#include <core/log.h>

#define TAG "UsbFloppyUfi"

// USB Mass Storage Class UFI Command Specification, rev 1.0
#define UFI_TEST_UNIT_READY        (0x00)
#define UFI_REZERO_UNIT            (0x01)
#define UFI_REQUEST_SENSE          (0x03)
#define UFI_FORMAT_UNIT            (0x04)
#define UFI_INQUIRY                (0x12)
#define UFI_MODE_SENSE_6           (0x1A)
#define UFI_START_STOP_UNIT        (0x1B)
#define UFI_SEND_DIAGNOSTIC        (0x1D)
#define UFI_PREVENT_MEDIUM_REMOVAL (0x1E)
#define UFI_READ_FORMAT_CAPACITIES (0x23)
#define UFI_READ_CAPACITY          (0x25)
#define UFI_READ_10                (0x28)
#define UFI_WRITE_10               (0x2A)
#define UFI_SEEK_10                (0x2B)
#define UFI_WRITE_AND_VERIFY       (0x2E)
#define UFI_VERIFY                 (0x2F)
#define UFI_MODE_SELECT_10         (0x55)
#define UFI_MODE_SENSE_10          (0x5A)
#define UFI_READ_12                (0xA8)
#define UFI_WRITE_12               (0xAA)

#define UFI_SK_MEDIUM_ERROR    (0x03)
#define UFI_SK_ILLEGAL_REQUEST (0x05)
#define UFI_SK_DATA_PROTECT    (0x07)

#define UFI_ASC_WRITE_FAULT               (0x03)
#define UFI_ASC_UNRECOVERED_READ_ERROR    (0x11)
#define UFI_ASC_PARAM_LIST_LENGTH_ERROR   (0x1A)
#define UFI_ASC_INVALID_COMMAND           (0x20)
#define UFI_ASC_LBA_OUT_OF_RANGE          (0x21)
#define UFI_ASC_INVALID_FIELD_IN_CDB      (0x24)
#define UFI_ASC_INVALID_FIELD_IN_PARAMS   (0x26)
#define UFI_ASC_WRITE_PROTECTED           (0x27)
#define UFI_ASC_FORMAT_FAILED             (0x31)
#define UFI_ASC_SAVING_PARAMS_UNSUPPORTED (0x39)

#define UFI_MODE_PAGE_FLEXIBLE_DISK (0x05)
#define UFI_MODE_PAGE_REMOVABLE     (0x1B)
#define UFI_MODE_PAGE_TIMER         (0x1C)
#define UFI_MODE_PAGE_ALL           (0x3F)

#define UFI_FORMAT_FILL_BYTE (0xF6)

static const UfiGeometry ufi_geometries[] = {
    {
        .name = "720K",
        .heads = 2,
        .sectors = 9,
        .cylinders = 80,
        .transfer_rate = 250,
        .medium_type = 0x1E,
    },
    {
        .name = "1.44M",
        .heads = 2,
        .sectors = 18,
        .cylinders = 80,
        .transfer_rate = 500,
        .medium_type = 0x94,
    },
};

static uint8_t ufi_format_fill[UFI_BLOCK_SIZE];

uint32_t ufi_geometry_blocks(const UfiGeometry* geometry) {
    return geometry->cylinders * geometry->heads * geometry->sectors;
}

const UfiGeometry* ufi_geometry_find(uint64_t image_size) {
    for(size_t i = 0; i < COUNT_OF(ufi_geometries); i++) {
        const UfiGeometry* geometry = &ufi_geometries[i];
        if(image_size == (uint64_t)ufi_geometry_blocks(geometry) * UFI_BLOCK_SIZE) {
            return geometry;
        }
    }
    return NULL;
}

static uint16_t get_be16(const uint8_t* p) {
    return p[0] << 8 | p[1];
}

static uint32_t get_be24(const uint8_t* p) {
    return p[0] << 16 | p[1] << 8 | p[2];
}

static uint32_t get_be32(const uint8_t* p) {
    return p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static void put_be16(uint8_t* p, uint16_t v) {
    p[0] = v >> 8;
    p[1] = v & 0xFF;
}

static void put_be24(uint8_t* p, uint32_t v) {
    p[0] = v >> 16;
    p[1] = v >> 8;
    p[2] = v & 0xFF;
}

static void put_be32(uint8_t* p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v & 0xFF;
}

static bool ufi_fail(UfiSession* ufi, uint8_t sk, uint8_t asc) {
    ufi->sk = sk;
    ufi->asc = asc;
    ufi->ascq = 0;
    return false;
}

// Record the data stage the host will run, even if the command is then rejected.
// CBI has no wrapper, so the transport stalls this stage to fail it.
static void ufi_expect(UfiSession* ufi, UfiDataDir dir, uint32_t len) {
    ufi->data_dir = dir;
    ufi->data_len = len;
}

// queue a response built in ufi->resp, cut to the host's allocation length
static bool ufi_respond(UfiSession* ufi, uint32_t len, uint32_t alloc_len) {
    ufi->resp_len = MIN(len, alloc_len);
    ufi->resp_sent = 0;
    ufi_expect(ufi, UfiDataIn, ufi->resp_len);
    return true;
}

static bool ufi_check_range(UfiSession* ufi, uint32_t lba, uint32_t count) {
    uint32_t blocks = ufi_geometry_blocks(ufi->dev.geometry);
    if(lba > blocks || count > blocks - lba) {
        FURI_LOG_W(TAG, "out of range lba=%08lX count=%08lX", lba, count);
        return ufi_fail(ufi, UFI_SK_ILLEGAL_REQUEST, UFI_ASC_LBA_OUT_OF_RANGE);
    }
    return true;
}

static bool ufi_start_rw(UfiSession* ufi, uint32_t lba, uint32_t count, UfiDataDir dir) {
    ufi_expect(ufi, dir, count * UFI_BLOCK_SIZE);
    if(dir == UfiDataOut && ufi->dev.write_protect) {
        return ufi_fail(ufi, UFI_SK_DATA_PROTECT, UFI_ASC_WRITE_PROTECTED);
    }
    if(!ufi_check_range(ufi, lba, count)) return false;
    ufi->rw.lba = lba;
    ufi->rw.count = count;
    return true;
}

static uint32_t ufi_mode_pages(UfiSession* ufi, uint8_t* out, uint8_t page, bool changeable) {
    const UfiGeometry* geometry = ufi->dev.geometry;
    uint32_t len = 0;
    // changeable values are reported as zero, nothing here can be changed
    if(page == UFI_MODE_PAGE_FLEXIBLE_DISK || page == UFI_MODE_PAGE_ALL) {
        uint8_t* p = out + len;
        memset(p, 0, 32);
        p[0] = UFI_MODE_PAGE_FLEXIBLE_DISK;
        p[1] = 0x1E; // page length
        if(!changeable) {
            put_be16(p + 2, geometry->transfer_rate);
            p[4] = geometry->heads;
            p[5] = geometry->sectors;
            put_be16(p + 6, UFI_BLOCK_SIZE);
            put_be16(p + 8, geometry->cylinders);
            p[19] = 0x05; // motor on delay, tenths of a second
            p[20] = 0x1E; // motor off delay, tenths of a second
            put_be16(p + 28, 300); // medium rotation rate, rpm
        }
        len += 32;
    }
    if(page == UFI_MODE_PAGE_REMOVABLE || page == UFI_MODE_PAGE_ALL) {
        uint8_t* p = out + len;
        memset(p, 0, 12);
        p[0] = UFI_MODE_PAGE_REMOVABLE;
        p[1] = 0x0A; // page length
        if(!changeable) {
            p[2] = 0x80; // system floppy
            p[3] = 0x01; // total logical units
        }
        len += 12;
    }
    if(page == UFI_MODE_PAGE_TIMER || page == UFI_MODE_PAGE_ALL) {
        uint8_t* p = out + len;
        memset(p, 0, 8);
        p[0] = UFI_MODE_PAGE_TIMER;
        p[1] = 0x06; // page length
        if(!changeable) {
            p[3] = 0x05; // inactivity time multiplier
        }
        len += 8;
    }
    return len;
}

static bool ufi_mode_sense(UfiSession* ufi, bool ten) {
    uint8_t page_control = ufi->cmd[2] >> 6;
    uint8_t page = ufi->cmd[2] & 0x3F;
    FURI_LOG_D(TAG, "UFI_MODE_SENSE_%d pc=%u page=%02X", ten ? 10 : 6, page_control, page);
    ufi_expect(ufi, UfiDataIn, ten ? get_be16(ufi->cmd + 7) : ufi->cmd[4]);
    if(page_control == 3) {
        return ufi_fail(ufi, UFI_SK_ILLEGAL_REQUEST, UFI_ASC_SAVING_PARAMS_UNSUPPORTED);
    }
    if(page != UFI_MODE_PAGE_FLEXIBLE_DISK && page != UFI_MODE_PAGE_REMOVABLE &&
       page != UFI_MODE_PAGE_TIMER && page != UFI_MODE_PAGE_ALL) {
        return ufi_fail(ufi, UFI_SK_ILLEGAL_REQUEST, UFI_ASC_INVALID_FIELD_IN_CDB);
    }

    uint8_t* resp = ufi->resp;
    uint32_t header_len = ten ? 8 : 4;
    memset(resp, 0, header_len);
    uint32_t len = header_len + ufi_mode_pages(ufi, resp + header_len, page, page_control == 1);
    uint8_t wp = ufi->dev.write_protect ? 0x80 : 0;
    if(ten) {
        put_be16(resp, len - 2); // mode data length
        resp[2] = ufi->dev.geometry->medium_type;
        resp[3] = wp;
        return ufi_respond(ufi, len, get_be16(ufi->cmd + 7));
    } else {
        resp[0] = len - 1; // mode data length
        resp[1] = ufi->dev.geometry->medium_type;
        resp[2] = wp;
        return ufi_respond(ufi, len, ufi->cmd[4]);
    }
}

static bool ufi_format(UfiSession* ufi, const uint8_t* params, uint32_t len) {
    const UfiGeometry* geometry = ufi->dev.geometry;
    uint32_t blocks = ufi_geometry_blocks(geometry);
    if(len < 12) {
        return ufi_fail(ufi, UFI_SK_ILLEGAL_REQUEST, UFI_ASC_PARAM_LIST_LENGTH_ERROR);
    }
    // the image has a fixed size, so only its own format is accepted
    if(get_be32(params + 4) != blocks || get_be24(params + 9) != UFI_BLOCK_SIZE) {
        FURI_LOG_W(
            TAG, "format to %lu x %lu not supported", get_be32(params + 4), get_be24(params + 9));
        return ufi_fail(ufi, UFI_SK_ILLEGAL_REQUEST, UFI_ASC_INVALID_FIELD_IN_PARAMS);
    }

    uint8_t flags = params[1];
    bool single_track = flags & 0x10;
    uint32_t first = 0;
    uint32_t count = blocks;
    if(single_track) {
        uint8_t track = ufi->cmd[2];
        uint8_t side = flags & 0x01;
        if(track >= geometry->cylinders) {
            return ufi_fail(ufi, UFI_SK_ILLEGAL_REQUEST, UFI_ASC_INVALID_FIELD_IN_CDB);
        }
        first = (track * geometry->heads + side) * geometry->sectors;
        count = geometry->sectors;
    }
    FURI_LOG_D(TAG, "UFI_FORMAT_UNIT lba=%08lX count=%lu", first, count);

    memset(ufi_format_fill, UFI_FORMAT_FILL_BYTE, sizeof(ufi_format_fill));
    for(uint32_t i = 0; i < count; i++) {
        if(!ufi->dev.write(ufi->dev.ctx, first + i, 1, ufi_format_fill)) {
            return ufi_fail(ufi, UFI_SK_MEDIUM_ERROR, UFI_ASC_FORMAT_FAILED);
        }
    }
    return true;
}

bool ufi_cmd_start(UfiSession* ufi, const uint8_t* cmd, uint8_t len) {
    // UFI CDBs are 12 bytes. Pad shorter ones so fields can be read without length checks.
    memset(ufi->cmd, 0, sizeof(ufi->cmd));
    memcpy(ufi->cmd, cmd, MIN(len, sizeof(ufi->cmd)));
    ufi->data_dir = UfiDataNone;
    ufi->data_len = 0;
    ufi->rx_done = false;
    ufi->tx_done = false;
    ufi->resp_len = 0;
    ufi->resp_sent = 0;
    if(!len) {
        return ufi_fail(ufi, UFI_SK_ILLEGAL_REQUEST, UFI_ASC_INVALID_COMMAND);
    }

    cmd = ufi->cmd;
    uint8_t* resp = ufi->resp;
    FURI_LOG_T(TAG, "START %02X", cmd[0]);
    switch(cmd[0]) {
    case UFI_REQUEST_SENSE: {
        FURI_LOG_D(TAG, "UFI_REQUEST_SENSE sk=%02X asc=%02X", ufi->sk, ufi->asc);
        memset(resp, 0, 18);
        resp[0] = 0x70; // current error
        resp[2] = ufi->sk;
        resp[7] = 10; // additional sense length (len - 8)
        resp[12] = ufi->asc;
        resp[13] = ufi->ascq;
        ufi->sk = 0;
        ufi->asc = 0;
        ufi->ascq = 0;
        return ufi_respond(ufi, 18, cmd[4]);
    }; break;
    case UFI_INQUIRY: {
        FURI_LOG_D(TAG, "UFI_INQUIRY");
        ufi_expect(ufi, UfiDataIn, cmd[4]);
        // UFI devices have no vital product data pages
        if(cmd[1] & 1) {
            return ufi_fail(ufi, UFI_SK_ILLEGAL_REQUEST, UFI_ASC_INVALID_FIELD_IN_CDB);
        }
        memset(resp, 0, 36);
        resp[0] = 0x00; // direct access block device
        resp[1] = 0x80; // removable
        resp[2] = 0x00; // ANSI version, 0 for UFI
        resp[3] = 0x01; // response data format, 1 for UFI
        resp[4] = 31; // additional length (len - 5)
        memcpy(resp + 8, "Flipper ", 8); // vendor id
        memcpy(resp + 16, "USB Floppy      ", 16); // product id
        memcpy(resp + 32, "0100", 4); // product revision level
        return ufi_respond(ufi, 36, cmd[4]);
    }; break;
    case UFI_READ_FORMAT_CAPACITIES: {
        FURI_LOG_D(TAG, "UFI_READ_FORMAT_CAPACITIES");
        uint32_t blocks = ufi_geometry_blocks(ufi->dev.geometry);
        memset(resp, 0, 20);
        resp[3] = 16; // capacity list length
        // current capacity descriptor
        put_be32(resp + 4, blocks);
        resp[8] = 0x02; // formatted media
        put_be24(resp + 9, UFI_BLOCK_SIZE);
        // formattable capacity descriptor
        put_be32(resp + 12, blocks);
        put_be24(resp + 17, UFI_BLOCK_SIZE);
        return ufi_respond(ufi, 20, get_be16(cmd + 7));
    }; break;
    case UFI_READ_CAPACITY: {
        FURI_LOG_D(TAG, "UFI_READ_CAPACITY");
        put_be32(resp, ufi_geometry_blocks(ufi->dev.geometry) - 1); // last LBA
        put_be32(resp + 4, UFI_BLOCK_SIZE);
        return ufi_respond(ufi, 8, 8);
    }; break;
    case UFI_MODE_SENSE_6:
        return ufi_mode_sense(ufi, false);
    case UFI_MODE_SENSE_10:
        return ufi_mode_sense(ufi, true);
    case UFI_READ_10: {
        FURI_LOG_D(TAG, "UFI_READ_10 %08lX %04X", get_be32(cmd + 2), get_be16(cmd + 7));
        return ufi_start_rw(ufi, get_be32(cmd + 2), get_be16(cmd + 7), UfiDataIn);
    }; break;
    case UFI_READ_12: {
        FURI_LOG_D(TAG, "UFI_READ_12 %08lX %08lX", get_be32(cmd + 2), get_be32(cmd + 6));
        return ufi_start_rw(ufi, get_be32(cmd + 2), get_be32(cmd + 6), UfiDataIn);
    }; break;
    case UFI_WRITE_10:
    case UFI_WRITE_AND_VERIFY: {
        FURI_LOG_D(TAG, "UFI_WRITE_10 %08lX %04X", get_be32(cmd + 2), get_be16(cmd + 7));
        return ufi_start_rw(ufi, get_be32(cmd + 2), get_be16(cmd + 7), UfiDataOut);
    }; break;
    case UFI_WRITE_12: {
        FURI_LOG_D(TAG, "UFI_WRITE_12 %08lX %08lX", get_be32(cmd + 2), get_be32(cmd + 6));
        return ufi_start_rw(ufi, get_be32(cmd + 2), get_be32(cmd + 6), UfiDataOut);
    }; break;
    case UFI_VERIFY: {
        FURI_LOG_D(TAG, "UFI_VERIFY");
        return ufi_check_range(ufi, get_be32(cmd + 2), get_be16(cmd + 7));
    }; break;
    case UFI_SEEK_10: {
        FURI_LOG_D(TAG, "UFI_SEEK_10");
        return ufi_check_range(ufi, get_be32(cmd + 2), 0);
    }; break;
    case UFI_FORMAT_UNIT: {
        FURI_LOG_D(TAG, "UFI_FORMAT_UNIT track=%u", cmd[2]);
        uint16_t param_len = get_be16(cmd + 7);
        ufi_expect(ufi, UfiDataOut, param_len);
        if(ufi->dev.write_protect) {
            return ufi_fail(ufi, UFI_SK_DATA_PROTECT, UFI_ASC_WRITE_PROTECTED);
        }
        if(param_len < 12 || param_len > sizeof(ufi->resp)) {
            return ufi_fail(ufi, UFI_SK_ILLEGAL_REQUEST, UFI_ASC_PARAM_LIST_LENGTH_ERROR);
        }
        return true;
    }; break;
    case UFI_MODE_SELECT_10: {
        // nothing is changeable, the parameter list is read and dropped
        FURI_LOG_D(TAG, "UFI_MODE_SELECT_10");
        ufi_expect(ufi, UfiDataOut, get_be16(cmd + 7));
        return true;
    }; break;
    case UFI_TEST_UNIT_READY:
    case UFI_REZERO_UNIT:
    case UFI_SEND_DIAGNOSTIC:
    case UFI_START_STOP_UNIT:
    case UFI_PREVENT_MEDIUM_REMOVAL:
        return true;
    }
    FURI_LOG_W(TAG, "unsupported ufi cmd=%02X", cmd[0]);
    return ufi_fail(ufi, UFI_SK_ILLEGAL_REQUEST, UFI_ASC_INVALID_COMMAND);
}

bool ufi_cmd_rx_data(UfiSession* ufi, const uint8_t* data, uint32_t len) {
    FURI_LOG_T(TAG, "RX %02X len %lu", ufi->cmd[0], len);
    if(ufi->rx_done) return false;
    switch(ufi->cmd[0]) {
    case UFI_WRITE_10:
    case UFI_WRITE_12:
    case UFI_WRITE_AND_VERIFY: {
        uint32_t blocks = MIN(len / UFI_BLOCK_SIZE, ufi->rw.count);
        if(!ufi->dev.write(ufi->dev.ctx, ufi->rw.lba, blocks, data)) {
            return ufi_fail(ufi, UFI_SK_MEDIUM_ERROR, UFI_ASC_WRITE_FAULT);
        }
        ufi->rw.lba += blocks;
        ufi->rw.count -= blocks;
        if(!ufi->rw.count) {
            ufi->rx_done = true;
        }
        return true;
    }; break;
    case UFI_FORMAT_UNIT: {
        ufi->rx_done = true;
        return ufi_format(ufi, data, len);
    }; break;
    case UFI_MODE_SELECT_10: {
        ufi->rx_done = true;
        return true;
    }; break;
    }
    FURI_LOG_W(TAG, "unexpected ufi rx data cmd=%02X", ufi->cmd[0]);
    return ufi_fail(ufi, UFI_SK_ILLEGAL_REQUEST, UFI_ASC_INVALID_COMMAND);
}

bool ufi_cmd_tx_data(UfiSession* ufi, uint8_t* data, uint32_t* len, uint32_t cap) {
    FURI_LOG_T(TAG, "TX %02X cap %lu", ufi->cmd[0], cap);
    if(ufi->tx_done) return false;
    switch(ufi->cmd[0]) {
    case UFI_READ_10:
    case UFI_READ_12: {
        uint32_t blocks = MIN(cap / UFI_BLOCK_SIZE, ufi->rw.count);
        if(!blocks || !ufi->dev.read(ufi->dev.ctx, ufi->rw.lba, blocks, data)) {
            return ufi_fail(ufi, UFI_SK_MEDIUM_ERROR, UFI_ASC_UNRECOVERED_READ_ERROR);
        }
        *len = blocks * UFI_BLOCK_SIZE;
        ufi->rw.lba += blocks;
        ufi->rw.count -= blocks;
        if(!ufi->rw.count) {
            ufi->tx_done = true;
        }
        return true;
    }; break;
    }
    if(ufi->data_dir != UfiDataIn) {
        FURI_LOG_W(TAG, "unexpected ufi tx data cmd=%02X", ufi->cmd[0]);
        return ufi_fail(ufi, UFI_SK_ILLEGAL_REQUEST, UFI_ASC_INVALID_COMMAND);
    }
    uint32_t n = MIN(ufi->resp_len - ufi->resp_sent, cap);
    memcpy(data, ufi->resp + ufi->resp_sent, n);
    ufi->resp_sent += n;
    *len = n;
    if(ufi->resp_sent == ufi->resp_len) {
        ufi->tx_done = true;
    }
    return true;
}

bool ufi_cmd_end(UfiSession* ufi) {
    uint8_t* cmd = ufi->cmd;
    FURI_LOG_T(TAG, "END %02X", cmd[0]);
    switch(cmd[0]) {
    case UFI_TEST_UNIT_READY: {
        FURI_LOG_T(TAG, "UFI_TEST_UNIT_READY");
        return true;
    }; break;
    case UFI_PREVENT_MEDIUM_REMOVAL: {
        bool prevent = cmd[4] & 1;
        FURI_LOG_D(TAG, "UFI_PREVENT_MEDIUM_REMOVAL prevent=%d", prevent);
        // floppy drives can't lock the disk in
        if(prevent) {
            return ufi_fail(ufi, UFI_SK_ILLEGAL_REQUEST, UFI_ASC_INVALID_FIELD_IN_CDB);
        }
        return true;
    }; break;
    case UFI_START_STOP_UNIT: {
        bool eject = (cmd[4] & 2) != 0;
        bool start = (cmd[4] & 1) != 0;
        FURI_LOG_D(TAG, "UFI_START_STOP_UNIT eject=%d start=%d", eject, start);
        if(eject && !start) {
            ufi->dev.eject(ufi->dev.ctx);
        }
        return true;
    }; break;
    }
    switch(ufi->data_dir) {
    case UfiDataIn:
        return ufi->tx_done || !ufi->data_len;
    case UfiDataOut:
        return ufi->rx_done || !ufi->data_len;
    default:
        return true;
    }
}
