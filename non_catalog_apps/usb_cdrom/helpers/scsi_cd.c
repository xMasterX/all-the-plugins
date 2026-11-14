#include "scsi_cd.h"

#include <core/log.h>

#define TAG "UsbCdromScsi"

// SPC-3 and MMC-5 commands a read only CD/DVD drive needs
#define SCSI_TEST_UNIT_READY        (0x00)
#define SCSI_REZERO_UNIT            (0x01)
#define SCSI_REQUEST_SENSE          (0x03)
#define SCSI_INQUIRY                (0x12)
#define SCSI_MODE_SELECT_6          (0x15)
#define SCSI_MODE_SENSE_6           (0x1A)
#define SCSI_START_STOP_UNIT        (0x1B)
#define SCSI_SEND_DIAGNOSTIC        (0x1D)
#define SCSI_PREVENT_MEDIUM_REMOVAL (0x1E)
#define SCSI_READ_CAPACITY          (0x25)
#define SCSI_READ_10                (0x28)
#define SCSI_SEEK_10                (0x2B)
#define SCSI_VERIFY_10              (0x2F)
#define SCSI_SYNCHRONIZE_CACHE      (0x35)
#define SCSI_READ_TOC               (0x43)
#define SCSI_GET_CONFIGURATION      (0x46)
#define SCSI_GET_EVENT_STATUS       (0x4A)
#define SCSI_READ_DISC_INFORMATION  (0x51)
#define SCSI_READ_TRACK_INFORMATION (0x52)
#define SCSI_MODE_SELECT_10         (0x55)
#define SCSI_MODE_SENSE_10          (0x5A)
#define SCSI_READ_12                (0xA8)
#define SCSI_READ_DISC_STRUCTURE    (0xAD)
#define SCSI_SET_STREAMING          (0xB6)
#define SCSI_SET_CD_SPEED           (0xBB)
#define SCSI_MECHANISM_STATUS       (0xBD)
#define SCSI_READ_CD                (0xBE)

#define SCSI_SK_NOT_READY       (0x02)
#define SCSI_SK_MEDIUM_ERROR    (0x03)
#define SCSI_SK_ILLEGAL_REQUEST (0x05)
#define SCSI_SK_UNIT_ATTENTION  (0x06)

#define SCSI_ASC_UNRECOVERED_READ_ERROR    (0x11)
#define SCSI_ASC_INVALID_COMMAND           (0x20)
#define SCSI_ASC_LBA_OUT_OF_RANGE          (0x21)
#define SCSI_ASC_INVALID_FIELD_IN_CDB      (0x24)
#define SCSI_ASC_MEDIUM_CHANGED            (0x28) // not ready to ready change
#define SCSI_ASC_INCOMPATIBLE_MEDIUM       (0x30)
#define SCSI_ASC_SAVING_PARAMS_UNSUPPORTED (0x39)
#define SCSI_ASC_MEDIUM_NOT_PRESENT        (0x3A)
#define SCSI_ASC_MEDIUM_REMOVAL_PREVENTED  (0x53)

#define CD_MODE_PAGE_ERROR_RECOVERY (0x01)
#define CD_MODE_PAGE_CAPABILITIES   (0x2A)
#define CD_MODE_PAGE_ALL            (0x3F)

#define CD_PROFILE_NONE    (0x0000)
#define CD_PROFILE_CD_ROM  (0x0008)
#define CD_PROFILE_DVD_ROM (0x0010)

#define CD_FEATURE_PROFILE_LIST    (0x0000)
#define CD_FEATURE_CORE            (0x0001)
#define CD_FEATURE_MORPHING        (0x0002)
#define CD_FEATURE_REMOVABLE       (0x0003)
#define CD_FEATURE_RANDOM_READABLE (0x0010)
#define CD_FEATURE_CD_READ         (0x001E)
#define CD_FEATURE_DVD_READ        (0x001F)
#define CD_FEATURE_POWER           (0x0100)

#define CD_FEATURE_CURRENT    (0x01)
#define CD_FEATURE_PERSISTENT (0x03) // always current

#define CD_EVENT_CLASS_MEDIA   (0x04)
#define CD_EVENT_MASK_MEDIA    (1 << CD_EVENT_CLASS_MEDIA)
#define CD_EVENT_NONE          (0x00)
#define CD_EVENT_EJECT_REQUEST (0x01)
#define CD_EVENT_NEW_MEDIA     (0x02)
#define CD_EVENT_MEDIA_REMOVAL (0x03)

#define CD_ADR_CONTROL_DATA  (0x14) // ADR 1 (position), data track
#define CD_TRACK_LEAD_OUT    (0xAA)
#define CD_MSF_LBA_OFFSET    (150) // the 2 second pregap before LBA 0
#define CD_DVD_DATA_START    (0x030000) // first physical sector of a DVD data area
#define CD_READ_CD_USER_DATA (0x10)

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

static void put_be32(uint8_t* p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v & 0xFF;
}

// minute, second, frame, as READ TOC gives them
static void put_msf(uint8_t* p, uint32_t lba) {
    lba += CD_MSF_LBA_OFFSET;
    p[0] = lba / (75 * 60);
    p[1] = (lba / 75) % 60;
    p[2] = lba % 75;
}

// a 4 byte READ TOC address, as LBA or as 0, M, S, F
static void put_toc_address(uint8_t* p, uint32_t lba, bool msf) {
    if(msf) {
        p[0] = 0;
        put_msf(p + 1, lba);
    } else {
        put_be32(p, lba);
    }
}

static bool cd_fail(CdSession* cd, uint8_t sk, uint8_t asc, uint8_t ascq) {
    cd->sk = sk;
    cd->asc = asc;
    cd->ascq = ascq;
    return false;
}

static bool cd_invalid_field(CdSession* cd) {
    return cd_fail(cd, SCSI_SK_ILLEGAL_REQUEST, SCSI_ASC_INVALID_FIELD_IN_CDB, 0);
}

static void cd_expect(CdSession* cd, CdDataDir dir, uint32_t len) {
    cd->data_dir = dir;
    cd->data_len = len;
}

// queue a response built in cd->resp, cut to the host's allocation length
static bool cd_respond(CdSession* cd, uint32_t len, uint32_t alloc_len) {
    cd->resp_len = MIN(len, alloc_len);
    cd->resp_sent = 0;
    cd_expect(cd, CdDataIn, cd->resp_len);
    return true;
}

static bool cd_check_range(CdSession* cd, uint32_t lba, uint32_t count) {
    uint32_t blocks = cd->media.blocks;
    if(lba > blocks || count > blocks - lba) {
        FURI_LOG_W(TAG, "out of range lba=%08lX count=%08lX", lba, count);
        return cd_fail(cd, SCSI_SK_ILLEGAL_REQUEST, SCSI_ASC_LBA_OUT_OF_RANGE, 0);
    }
    return true;
}

static bool cd_start_read(CdSession* cd, uint32_t lba, uint32_t count) {
    if(!cd_check_range(cd, lba, count)) return false;
    cd->rw.lba = lba;
    cd->rw.count = count;
    cd_expect(cd, CdDataIn, count * CD_BLOCK_SIZE);
    return true;
}

// Picks up a disc change since the last command. The host hears about it twice:
// a UNIT ATTENTION on the next command, and a GET EVENT STATUS NOTIFICATION media event.
static void cd_update_media(CdSession* cd) {
    cd->dev.media(cd->dev.ctx, &cd->media);
    if(cd->media.eject_request) {
        cd->media_event = CD_EVENT_EJECT_REQUEST;
    }
    if(cd->media.id != cd->media_id) {
        cd->media_id = cd->media.id;
        if(cd->media.blocks) {
            FURI_LOG_D(TAG, "new disc, %lu blocks", cd->media.blocks);
            cd->unit_attention = true;
            cd->media_event = CD_EVENT_NEW_MEDIA;
        } else {
            FURI_LOG_D(TAG, "tray open");
            cd->unit_attention = false;
            cd->media_event = CD_EVENT_MEDIA_REMOVAL;
        }
    }
}

static uint16_t cd_current_profile(CdSession* cd) {
    if(!cd->media.blocks) return CD_PROFILE_NONE;
    return cd->media.dvd ? CD_PROFILE_DVD_ROM : CD_PROFILE_CD_ROM;
}

static uint32_t cd_mode_pages(uint8_t* out, uint8_t page, bool changeable, bool locked) {
    uint32_t len = 0;
    // changeable values are reported as zero, nothing here can be changed
    if(page == CD_MODE_PAGE_ERROR_RECOVERY || page == CD_MODE_PAGE_ALL) {
        uint8_t* p = out + len;
        memset(p, 0, 12);
        p[0] = CD_MODE_PAGE_ERROR_RECOVERY;
        p[1] = 0x0A; // page length
        len += 12;
    }
    if(page == CD_MODE_PAGE_CAPABILITIES || page == CD_MODE_PAGE_ALL) {
        uint8_t* p = out + len;
        memset(p, 0, 22);
        p[0] = CD_MODE_PAGE_CAPABILITIES;
        p[1] = 0x14; // page length
        if(!changeable) {
            p[2] = 0x08; // reads DVD-ROM, and CD-ROM which needs no bit
            // tray loader, eject, no prevent jumper, lock
            p[6] = 0x20 | 0x08 | 0x01 | (locked ? 0x02 : 0);
            put_be16(p + 8, 1385); // max read speed, kB/s. 1x DVD, 9x CD.
            put_be16(p + 12, 16); // buffer size, KB
            put_be16(p + 14, 1385); // current read speed, kB/s
        }
        len += 22;
    }
    return len;
}

static bool cd_mode_sense(CdSession* cd, bool ten) {
    uint8_t page_control = cd->cmd[2] >> 6;
    uint8_t page = cd->cmd[2] & 0x3F;
    FURI_LOG_D(TAG, "MODE_SENSE_%d pc=%u page=%02X", ten ? 10 : 6, page_control, page);
    if(page_control == 3) {
        return cd_fail(cd, SCSI_SK_ILLEGAL_REQUEST, SCSI_ASC_SAVING_PARAMS_UNSUPPORTED, 0);
    }
    if(page != CD_MODE_PAGE_ERROR_RECOVERY && page != CD_MODE_PAGE_CAPABILITIES &&
       page != CD_MODE_PAGE_ALL) {
        return cd_invalid_field(cd);
    }

    uint8_t* resp = cd->resp;
    uint32_t header_len = ten ? 8 : 4;
    memset(resp, 0, header_len);
    uint32_t len =
        header_len + cd_mode_pages(resp + header_len, page, page_control == 1, cd->locked);
    if(ten) {
        put_be16(resp, len - 2); // mode data length
        return cd_respond(cd, len, get_be16(cd->cmd + 7));
    } else {
        resp[0] = len - 1; // mode data length
        return cd_respond(cd, len, cd->cmd[4]);
    }
}

static bool cd_read_toc(CdSession* cd) {
    uint8_t* cmd = cd->cmd;
    bool msf = cmd[1] & 0x02;
    uint8_t format = cmd[2] & 0x0F;
    if(!format) {
        format = cmd[9] >> 6; // older drives took the format from the control byte
    }
    uint8_t track = cmd[6];
    uint32_t blocks = cd->media.blocks;
    uint8_t* resp = cd->resp;
    uint32_t len = 4;
    FURI_LOG_D(TAG, "READ_TOC format=%u msf=%d track=%02X", format, msf, track);

    // one session holding one data track that starts at LBA 0
    switch(format) {
    case 0: { // TOC
        if(track > 1 && track != CD_TRACK_LEAD_OUT) {
            return cd_invalid_field(cd);
        }
        if(track <= 1) {
            memset(resp + len, 0, 8);
            resp[len + 1] = CD_ADR_CONTROL_DATA;
            resp[len + 2] = 1;
            put_toc_address(resp + len + 4, 0, msf);
            len += 8;
        }
        memset(resp + len, 0, 8);
        resp[len + 1] = CD_ADR_CONTROL_DATA;
        resp[len + 2] = CD_TRACK_LEAD_OUT;
        put_toc_address(resp + len + 4, blocks, msf);
        len += 8;
        resp[2] = 1; // first track
        resp[3] = 1; // last track
    }; break;
    case 1: { // session info
        memset(resp + len, 0, 8);
        resp[len + 1] = CD_ADR_CONTROL_DATA;
        resp[len + 2] = 1; // first track in the last session
        put_toc_address(resp + len + 4, 0, msf);
        len += 8;
        resp[2] = 1; // first session
        resp[3] = 1; // last session
    }; break;
    case 2: { // full TOC, the raw Q subchannel entries of the lead-in
        static const uint8_t points[] = {0xA0, 0xA1, 0xA2, 1};
        for(size_t i = 0; i < COUNT_OF(points); i++) {
            uint8_t* p = resp + len;
            memset(p, 0, 11);
            p[0] = 1; // session
            p[1] = CD_ADR_CONTROL_DATA;
            p[3] = points[i];
            switch(points[i]) {
            case 0xA0:
                p[8] = 1; // first track
                p[9] = 0x00; // disc type: CD-ROM
                break;
            case 0xA1:
                p[8] = 1; // last track
                break;
            case 0xA2:
                put_msf(p + 8, blocks); // lead-out
                break;
            default:
                put_msf(p + 8, 0); // track start
                break;
            }
            len += 11;
        }
        resp[2] = 1; // first session
        resp[3] = 1; // last session
    }; break;
    default:
        return cd_invalid_field(cd);
    }
    put_be16(resp, len - 2); // data length
    return cd_respond(cd, len, get_be16(cmd + 7));
}

// Adds a feature descriptor to a GET CONFIGURATION response, if the request asks for it.
// Returns the descriptor so the caller can fill in its data, or NULL if it was left out.
static uint8_t* cd_feature(
    CdSession* cd,
    uint32_t* len,
    uint16_t code,
    uint8_t version,
    uint8_t flags,
    uint8_t data_len) {
    uint8_t rt = cd->cmd[1] & 0x03;
    uint16_t start = get_be16(cd->cmd + 2);
    if(code < start) return NULL;
    if(rt == 1 && !(flags & CD_FEATURE_CURRENT)) return NULL; // current features only
    if(rt == 2 && code != start) return NULL; // one feature only
    uint8_t* p = cd->resp + *len;
    memset(p, 0, 4 + data_len);
    put_be16(p, code);
    p[2] = version << 2 | flags;
    p[3] = data_len;
    *len += 4 + data_len;
    return p;
}

static bool cd_get_configuration(CdSession* cd) {
    bool media = cd->media.blocks;
    bool dvd = media && cd->media.dvd;
    bool cd_media = media && !cd->media.dvd;
    uint8_t* resp = cd->resp;
    uint32_t len = 8;
    uint8_t* p;
    FURI_LOG_D(
        TAG, "GET_CONFIGURATION rt=%u start=%04X", cd->cmd[1] & 0x03, get_be16(cd->cmd + 2));

    memset(resp, 0, len);
    put_be16(resp + 6, cd_current_profile(cd));

    p = cd_feature(cd, &len, CD_FEATURE_PROFILE_LIST, 0, CD_FEATURE_PERSISTENT, 8);
    if(p) {
        put_be16(p + 4, CD_PROFILE_DVD_ROM);
        p[6] = dvd;
        put_be16(p + 8, CD_PROFILE_CD_ROM);
        p[10] = cd_media;
    }
    p = cd_feature(cd, &len, CD_FEATURE_CORE, 1, CD_FEATURE_PERSISTENT, 8);
    if(p) {
        put_be32(p + 4, 0x08); // physical interface: USB
        p[8] = 0x01; // DBE, device busy events
    }
    cd_feature(cd, &len, CD_FEATURE_MORPHING, 1, CD_FEATURE_PERSISTENT, 4);
    p = cd_feature(cd, &len, CD_FEATURE_REMOVABLE, 0, CD_FEATURE_PERSISTENT, 4);
    if(p) {
        p[4] = 0x20 | 0x08 | 0x04 | 0x01; // tray loader, eject, no prevent jumper, lock
    }
    p = cd_feature(cd, &len, CD_FEATURE_RANDOM_READABLE, 0, media ? CD_FEATURE_CURRENT : 0, 8);
    if(p) {
        put_be32(p + 4, CD_BLOCK_SIZE);
        put_be16(p + 8, dvd ? 16 : 1); // blocking, sectors per ECC block
    }
    cd_feature(cd, &len, CD_FEATURE_CD_READ, 0, cd_media ? CD_FEATURE_CURRENT : 0, 4);
    cd_feature(cd, &len, CD_FEATURE_DVD_READ, 0, dvd ? CD_FEATURE_CURRENT : 0, 0);
    cd_feature(cd, &len, CD_FEATURE_POWER, 0, CD_FEATURE_PERSISTENT, 0);

    put_be32(resp, len - 4); // data length
    return cd_respond(cd, len, get_be16(cd->cmd + 7));
}

static bool cd_get_event_status(CdSession* cd) {
    uint8_t* cmd = cd->cmd;
    uint8_t* resp = cd->resp;
    FURI_LOG_T(TAG, "GET_EVENT_STATUS classes=%02X", cmd[4]);
    // only polled requests are supported
    if(!(cmd[1] & 0x01)) {
        return cd_invalid_field(cd);
    }
    memset(resp, 0, 8);
    resp[3] = CD_EVENT_MASK_MEDIA; // supported event classes
    if(!(cmd[4] & CD_EVENT_MASK_MEDIA)) {
        put_be16(resp, 2); // event data length
        resp[2] = 0x80; // no event available
        return cd_respond(cd, 4, get_be16(cmd + 7));
    }
    put_be16(resp, 6); // event data length
    resp[2] = CD_EVENT_CLASS_MEDIA;
    resp[4] = cd->media_event;
    resp[5] = cd->media.blocks ? 0x02 : 0x01; // media present, or tray open
    if(cd->media_event != CD_EVENT_NONE) {
        FURI_LOG_D(TAG, "media event %u", cd->media_event);
    }
    cd->media_event = CD_EVENT_NONE;
    return cd_respond(cd, 8, get_be16(cmd + 7));
}

static bool cd_read_disc_information(CdSession* cd) {
    uint8_t* resp = cd->resp;
    FURI_LOG_D(TAG, "READ_DISC_INFORMATION");
    // only standard disc information
    if(cd->cmd[1] & 0x07) {
        return cd_invalid_field(cd);
    }
    memset(resp, 0, 34);
    put_be16(resp, 32); // data length
    resp[2] = 0x0E; // last session complete, disc complete, not erasable
    resp[3] = 1; // first track
    resp[4] = 1; // number of sessions
    resp[5] = 1; // first track in last session
    resp[6] = 1; // last track in last session
    resp[7] = 0x20; // unrestricted use
    put_be32(resp + 16, 0xFFFFFFFF); // last session lead-in start, none on a complete disc
    put_be32(resp + 20, 0xFFFFFFFF); // last possible lead-out start
    return cd_respond(cd, 34, get_be16(cd->cmd + 7));
}

static bool cd_read_track_information(CdSession* cd) {
    uint8_t* resp = cd->resp;
    uint8_t type = cd->cmd[1] & 0x03;
    uint32_t number = get_be32(cd->cmd + 2);
    uint32_t blocks = cd->media.blocks;
    FURI_LOG_D(TAG, "READ_TRACK_INFORMATION type=%u number=%08lX", type, number);
    // type 0 is an LBA, 1 a track number, 2 a session number
    bool found = (type == 0 && number < blocks) || (type == 1 && number == 1) ||
                 (type == 2 && number == 1);
    if(!found) {
        return cd_invalid_field(cd);
    }
    memset(resp, 0, 36);
    put_be16(resp, 34); // data length
    resp[2] = 1; // track number
    resp[3] = 1; // session number
    resp[5] = 0x04; // track mode: data, recorded uninterrupted
    resp[6] = 0x01; // data mode 1
    put_be32(resp + 24, blocks); // track size
    put_be32(resp + 28, blocks - 1); // last recorded address
    return cd_respond(cd, 36, get_be16(cd->cmd + 7));
}

static bool cd_read_disc_structure(CdSession* cd) {
    uint8_t* cmd = cd->cmd;
    uint8_t* resp = cd->resp;
    uint8_t layer = cmd[6];
    uint8_t format = cmd[7];
    FURI_LOG_D(TAG, "READ_DISC_STRUCTURE format=%02X layer=%u", format, layer);
    if(!cd->media.dvd) {
        return cd_fail(cd, SCSI_SK_ILLEGAL_REQUEST, SCSI_ASC_INCOMPATIBLE_MEDIUM, 0x02);
    }
    if((cmd[1] & 0x0F) != 0 || layer != 0) {
        return cd_invalid_field(cd);
    }
    switch(format) {
    case 0x00: { // physical format, the first 17 bytes of the layer descriptor
        memset(resp, 0, 21);
        put_be16(resp, 19); // data length
        resp[4] = 0x01; // DVD-ROM, part version 1
        resp[5] = 0x02; // 120mm, 10.08 Mbit/s
        resp[6] = 0x01; // one layer, read only
        put_be32(resp + 8, CD_DVD_DATA_START);
        put_be32(resp + 12, CD_DVD_DATA_START + cd->media.blocks - 1);
        return cd_respond(cd, 21, get_be16(cmd + 8));
    }; break;
    case 0x01: { // copyright
        memset(resp, 0, 8);
        put_be16(resp, 6); // data length
        resp[4] = 0x00; // no copy protection
        resp[5] = 0x00; // plays in every region
        return cd_respond(cd, 8, get_be16(cmd + 8));
    }; break;
    }
    return cd_invalid_field(cd);
}

static bool cd_read_cd(CdSession* cd) {
    uint8_t* cmd = cd->cmd;
    uint8_t sector_type = (cmd[1] >> 2) & 0x07;
    uint32_t lba = get_be32(cmd + 2);
    uint32_t count = get_be24(cmd + 6);
    uint8_t fields = cmd[9];
    uint8_t subchannel = cmd[10] & 0x07;
    FURI_LOG_D(TAG, "READ_CD %08lX %06lX type=%u fields=%02X", lba, count, sector_type, fields);
    // an ISO only holds the 2048 byte user data of mode 1 sectors
    if((sector_type != 0 && sector_type != 2) || subchannel) {
        return cd_invalid_field(cd);
    }
    if(!fields) {
        return cd_check_range(cd, lba, count);
    }
    if(fields != CD_READ_CD_USER_DATA) {
        return cd_invalid_field(cd);
    }
    return cd_start_read(cd, lba, count);
}

void scsi_cd_init(CdSession* cd, CdDevice dev) {
    memset(cd, 0, sizeof(*cd));
    cd->dev = dev;
    // the disc in the tray at power on is not a change
    cd->dev.media(cd->dev.ctx, &cd->media);
    cd->media_id = cd->media.id;
}

bool scsi_cd_cmd_start(CdSession* cd, const uint8_t* cmd, uint8_t len) {
    // Pad short CDBs so fields can be read without length checks
    memset(cd->cmd, 0, sizeof(cd->cmd));
    memcpy(cd->cmd, cmd, MIN(len, sizeof(cd->cmd)));
    cd->data_dir = CdDataNone;
    cd->data_len = 0;
    cd->rx_done = false;
    cd->tx_done = false;
    cd->resp_len = 0;
    cd->resp_sent = 0;
    if(!len) {
        return cd_fail(cd, SCSI_SK_ILLEGAL_REQUEST, SCSI_ASC_INVALID_COMMAND, 0);
    }

    cd_update_media(cd);

    cmd = cd->cmd;
    uint8_t* resp = cd->resp;
    FURI_LOG_T(TAG, "START %02X", cmd[0]);

    // commands that work through a pending unit attention, with or without a disc
    switch(cmd[0]) {
    case SCSI_REQUEST_SENSE: {
        FURI_LOG_D(TAG, "REQUEST_SENSE sk=%02X asc=%02X", cd->sk, cd->asc);
        memset(resp, 0, 18);
        resp[0] = 0x70; // current error
        resp[2] = cd->sk;
        resp[7] = 10; // additional sense length (len - 8)
        resp[12] = cd->asc;
        resp[13] = cd->ascq;
        cd->sk = 0;
        cd->asc = 0;
        cd->ascq = 0;
        return cd_respond(cd, 18, cmd[4]);
    }; break;
    case SCSI_INQUIRY: {
        FURI_LOG_D(TAG, "INQUIRY");
        // no vital product data pages
        if(cmd[1] & 1) {
            return cd_invalid_field(cd);
        }
        memset(resp, 0, 36);
        resp[0] = 0x05; // CD/DVD device
        resp[1] = 0x80; // removable
        resp[2] = 0x00; // no version claimed, as most ATAPI drives report
        resp[3] = 0x02; // response data format
        resp[4] = 31; // additional length (len - 5)
        memcpy(resp + 8, "Flipper ", 8); // vendor id
        memcpy(resp + 16, "USB CD-ROM      ", 16); // product id
        memcpy(resp + 32, "0100", 4); // product revision level
        return cd_respond(cd, 36, get_be16(cmd + 3));
    }; break;
    case SCSI_GET_CONFIGURATION:
        return cd_get_configuration(cd);
    case SCSI_GET_EVENT_STATUS:
        return cd_get_event_status(cd);
    }

    if(cd->unit_attention) {
        FURI_LOG_D(TAG, "unit attention on cmd=%02X", cmd[0]);
        cd->unit_attention = false;
        return cd_fail(cd, SCSI_SK_UNIT_ATTENTION, SCSI_ASC_MEDIUM_CHANGED, 0);
    }

    // commands that work with the tray open
    switch(cmd[0]) {
    case SCSI_MODE_SENSE_6:
        return cd_mode_sense(cd, false);
    case SCSI_MODE_SENSE_10:
        return cd_mode_sense(cd, true);
    case SCSI_MECHANISM_STATUS: {
        FURI_LOG_D(TAG, "MECHANISM_STATUS");
        memset(resp, 0, 8);
        resp[1] = cd->media.blocks ? 0 : 0x10; // door open
        return cd_respond(cd, 8, get_be16(cmd + 8));
    }; break;
    case SCSI_MODE_SELECT_6: {
        // nothing is changeable, the parameter list is read and dropped
        FURI_LOG_D(TAG, "MODE_SELECT_6");
        cd_expect(cd, CdDataOut, cmd[4]);
        return true;
    }; break;
    case SCSI_MODE_SELECT_10: {
        FURI_LOG_D(TAG, "MODE_SELECT_10");
        cd_expect(cd, CdDataOut, get_be16(cmd + 7));
        return true;
    }; break;
    case SCSI_SET_STREAMING: {
        // the drive runs as fast as the SD card does, the descriptor is read and dropped
        FURI_LOG_D(TAG, "SET_STREAMING");
        cd_expect(cd, CdDataOut, get_be16(cmd + 9));
        return true;
    }; break;
    case SCSI_START_STOP_UNIT:
    case SCSI_PREVENT_MEDIUM_REMOVAL:
    case SCSI_SET_CD_SPEED:
    case SCSI_REZERO_UNIT:
    case SCSI_SEND_DIAGNOSTIC:
    case SCSI_SYNCHRONIZE_CACHE:
        return true;
    }

    if(!cd->media.blocks) {
        switch(cmd[0]) {
        case SCSI_TEST_UNIT_READY:
        case SCSI_READ_CAPACITY:
        case SCSI_READ_10:
        case SCSI_READ_12:
        case SCSI_READ_CD:
        case SCSI_SEEK_10:
        case SCSI_VERIFY_10:
        case SCSI_READ_TOC:
        case SCSI_READ_DISC_INFORMATION:
        case SCSI_READ_TRACK_INFORMATION:
        case SCSI_READ_DISC_STRUCTURE:
            FURI_LOG_T(TAG, "no disc for cmd=%02X", cmd[0]);
            return cd_fail(cd, SCSI_SK_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT, 0);
        }
    }

    // commands that need a disc
    switch(cmd[0]) {
    case SCSI_TEST_UNIT_READY:
        return true;
    case SCSI_READ_CAPACITY: {
        FURI_LOG_D(TAG, "READ_CAPACITY");
        put_be32(resp, cd->media.blocks - 1); // last LBA
        put_be32(resp + 4, CD_BLOCK_SIZE);
        return cd_respond(cd, 8, 8);
    }; break;
    case SCSI_READ_10: {
        FURI_LOG_D(TAG, "READ_10 %08lX %04X", get_be32(cmd + 2), get_be16(cmd + 7));
        return cd_start_read(cd, get_be32(cmd + 2), get_be16(cmd + 7));
    }; break;
    case SCSI_READ_12: {
        FURI_LOG_D(TAG, "READ_12 %08lX %08lX", get_be32(cmd + 2), get_be32(cmd + 6));
        return cd_start_read(cd, get_be32(cmd + 2), get_be32(cmd + 6));
    }; break;
    case SCSI_READ_CD:
        return cd_read_cd(cd);
    case SCSI_SEEK_10: {
        FURI_LOG_D(TAG, "SEEK_10");
        return cd_check_range(cd, get_be32(cmd + 2), 0);
    }; break;
    case SCSI_VERIFY_10: {
        FURI_LOG_D(TAG, "VERIFY_10");
        return cd_check_range(cd, get_be32(cmd + 2), get_be16(cmd + 7));
    }; break;
    case SCSI_READ_TOC:
        return cd_read_toc(cd);
    case SCSI_READ_DISC_INFORMATION:
        return cd_read_disc_information(cd);
    case SCSI_READ_TRACK_INFORMATION:
        return cd_read_track_information(cd);
    case SCSI_READ_DISC_STRUCTURE:
        return cd_read_disc_structure(cd);
    }
    FURI_LOG_W(TAG, "unsupported scsi cmd=%02X", cmd[0]);
    return cd_fail(cd, SCSI_SK_ILLEGAL_REQUEST, SCSI_ASC_INVALID_COMMAND, 0);
}

bool scsi_cd_cmd_rx_data(CdSession* cd, const uint8_t* data, uint32_t len) {
    UNUSED(data);
    FURI_LOG_T(TAG, "RX %02X len %lu", cd->cmd[0], len);
    if(cd->rx_done) return false;
    switch(cd->cmd[0]) {
    case SCSI_MODE_SELECT_6:
    case SCSI_MODE_SELECT_10:
    case SCSI_SET_STREAMING: {
        cd->rx_done = true;
        return true;
    }; break;
    }
    FURI_LOG_W(TAG, "unexpected scsi rx data cmd=%02X", cd->cmd[0]);
    return cd_fail(cd, SCSI_SK_ILLEGAL_REQUEST, SCSI_ASC_INVALID_COMMAND, 0);
}

bool scsi_cd_cmd_tx_data(CdSession* cd, uint8_t* data, uint32_t* len, uint32_t cap) {
    FURI_LOG_T(TAG, "TX %02X cap %lu", cd->cmd[0], cap);
    if(cd->tx_done) return false;
    switch(cd->cmd[0]) {
    case SCSI_READ_10:
    case SCSI_READ_12:
    case SCSI_READ_CD: {
        uint32_t blocks = MIN(cap / CD_BLOCK_SIZE, cd->rw.count);
        if(!blocks || !cd->dev.read(cd->dev.ctx, cd->rw.lba, blocks, data)) {
            return cd_fail(cd, SCSI_SK_MEDIUM_ERROR, SCSI_ASC_UNRECOVERED_READ_ERROR, 0);
        }
        *len = blocks * CD_BLOCK_SIZE;
        cd->rw.lba += blocks;
        cd->rw.count -= blocks;
        if(!cd->rw.count) {
            cd->tx_done = true;
        }
        return true;
    }; break;
    }
    if(cd->data_dir != CdDataIn) {
        FURI_LOG_W(TAG, "unexpected scsi tx data cmd=%02X", cd->cmd[0]);
        return cd_fail(cd, SCSI_SK_ILLEGAL_REQUEST, SCSI_ASC_INVALID_COMMAND, 0);
    }
    uint32_t n = MIN(cd->resp_len - cd->resp_sent, cap);
    memcpy(data, cd->resp + cd->resp_sent, n);
    cd->resp_sent += n;
    *len = n;
    if(cd->resp_sent == cd->resp_len) {
        cd->tx_done = true;
    }
    return true;
}

bool scsi_cd_cmd_end(CdSession* cd) {
    uint8_t* cmd = cd->cmd;
    FURI_LOG_T(TAG, "END %02X", cmd[0]);
    switch(cmd[0]) {
    case SCSI_PREVENT_MEDIUM_REMOVAL: {
        cd->locked = cmd[4] & 1;
        FURI_LOG_D(TAG, "PREVENT_MEDIUM_REMOVAL prevent=%d", cd->locked);
        cd->dev.lock(cd->dev.ctx, cd->locked);
        return true;
    }; break;
    case SCSI_START_STOP_UNIT: {
        bool load_eject = (cmd[4] & 2) != 0;
        bool start = (cmd[4] & 1) != 0;
        FURI_LOG_D(TAG, "START_STOP_UNIT loej=%d start=%d", load_eject, start);
        if(load_eject && !start && cd->media.blocks) {
            if(cd->locked) {
                return cd_fail(
                    cd, SCSI_SK_ILLEGAL_REQUEST, SCSI_ASC_MEDIUM_REMOVAL_PREVENTED, 0x02);
            }
            cd->dev.eject(cd->dev.ctx);
        }
        return true;
    }; break;
    }
    switch(cd->data_dir) {
    case CdDataIn:
        return cd->tx_done || !cd->data_len;
    case CdDataOut:
        return cd->rx_done || !cd->data_len;
    default:
        return true;
    }
}
