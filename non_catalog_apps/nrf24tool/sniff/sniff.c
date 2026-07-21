#include "sniff.h"
#include "../helper.h"
#include "../libnrf24/nrf24.h"
#include "notification/notification_messages.h"
#include "stream/file_stream.h"

//#define LOG_TAG "NRF24_sniff"

uint8_t preamble1[] = {0xAA, 0x00};
uint8_t preamble2[] = {0x55, 0x00};

uint8_t candidates[MAX_ADDRS][MAX_MAC_SIZE] = {0}; // last 100 sniffed addresses
uint32_t counts[MAX_ADDRS];
uint8_t confirmed[MAX_CONFIRMED][MAX_MAC_SIZE] = {0}; // first 32 confirmed addresses
uint8_t confirmed_idx = 0;
static const char* FILE_PATH_ADDR = APP_DATA_PATH("addresses.txt");
uint32_t total_candidates = 0;
uint32_t candidate_idx = 0;
char top_address[HEX_MAC_LEN + 1];
nrf24_data_rate target_rate = DATA_RATE_2MBPS;

Setting sniff_defaults[] = {
    {.name = "Min. Channel",
     .type = SETTING_TYPE_UINT8,
     .value.u8 = DEFAULT_MIN_CHANNEL,
     .min = DEFAULT_MIN_CHANNEL,
     .max = DEFAULT_MAX_CHANNEL,
     .step = 1},
    {.name = "Max. Channel",
     .type = SETTING_TYPE_UINT8,
     .value.u8 = DEFAULT_MAX_CHANNEL,
     .min = DEFAULT_MIN_CHANNEL,
     .max = DEFAULT_MAX_CHANNEL,
     .step = 1},
    {.name = "Scan Time (µs)",
     .type = SETTING_TYPE_UINT16,
     .value.u16 = DEFAULT_SCANTIME,
     .min = 500,
     .max = 10000,
     .step = 500},
    {.name = "Data Rate",
     .type = SETTING_TYPE_DATA_RATE,
     .value.d_r = DATA_RATE_2MBPS,
     .min = DATA_RATE_1MBPS,
     .max = DATA_RATE_250KBPS,
     .step = 1},
    {.name = "RPD", .type = SETTING_TYPE_BOOL, .value.b = true, .min = 0, .max = 1, .step = 1}};

NRF24L01_Config sniff_nrf24_config = {
    .channel = DEFAULT_MIN_CHANNEL,
    .data_rate = DATA_RATE_2MBPS,
    .tx_power = TX_POWER_0DBM,
    .crc_length = CRC_DISABLED,
    .mac_len = ADDR_WIDTH_2_BYTES,
    .arc = 15,
    .ard = 250,
    .auto_ack = {false, false, false, false, false, false},
    .dynamic_payload = {false, false, false, false, false, false},
    .ack_payload = false,
    .tx_no_ack = false,
    .tx_addr = NULL,
    .rx_addr = {preamble1, preamble2, NULL, NULL, NULL, NULL},
    .payload_size = {MAX_PAYLOAD_SIZE, MAX_PAYLOAD_SIZE, 0, 0, 0, 0}};

static int get_addr_index(uint8_t* addr, uint8_t addr_size) {
    for(uint32_t i = 0; i < total_candidates; i++) {
        if(!memcmp(candidates[i], addr, addr_size)) return i;
    }
    return -1;
}

static int get_highest_idx() {
    uint32_t highest = 0;
    int highest_idx = 0;
    for(uint32_t i = 0; i < total_candidates; i++) {
        if(counts[i] > highest) {
            highest = counts[i];
            highest_idx = i;
        }
    }
    return highest_idx;
}

static void insert_addr(uint8_t* addr, uint8_t addr_size) {
    if(candidate_idx >= MAX_ADDRS) candidate_idx = 0;

    memcpy(candidates[candidate_idx], addr, addr_size);
    counts[candidate_idx] = 1;
    if(total_candidates < MAX_ADDRS) total_candidates++;
    candidate_idx++;
}

static bool previously_confirmed(uint8_t* addr) {
    for(int i = 0; i < MAX_CONFIRMED; i++) {
        if(memcmp(confirmed[i], addr, MAX_MAC_SIZE) == 0) return true;
    }
    return false;
}

static void alt_address(uint8_t* addr, uint8_t* altaddr) {
    uint8_t macmess_hi_b[4];
    uint32_t macmess_hi;
    uint8_t macmess_lo;
    uint8_t preserved;
    uint8_t tmpaddr[5];

    // swap bytes
    for(int i = 0; i < 5; i++)
        tmpaddr[i] = addr[4 - i];

    // get address into 32-bit and 8-bit variables
    memcpy(macmess_hi_b, tmpaddr, 4);
    macmess_lo = tmpaddr[4];

    macmess_hi = bytes_to_int32(macmess_hi_b, true);

    //preserve lowest bit from hi to shift to low
    preserved = macmess_hi & 1;
    macmess_hi >>= 1;
    macmess_lo >>= 1;
    macmess_lo = (preserved << 7) | macmess_lo;
    int32_to_bytes(macmess_hi, macmess_hi_b, true);
    memcpy(tmpaddr, macmess_hi_b, 4);
    tmpaddr[4] = macmess_lo;

    // swap bytes back
    for(int i = 0; i < 5; i++)
        altaddr[i] = tmpaddr[4 - i];
}

static bool validate_address(uint8_t* addr) {
    uint16_t bad[] = {0x5555, 0xAAAA, 0x0000, 0xFFFF};
    uint16_t* addr16 = (uint16_t*)addr;

    for(int i = 0; i < 4; i++) {
        if(addr16[0] == bad[i] || addr16[1] == bad[i]) {
            return false;
        }
    }
    return true;
}

static bool nrf24_sniff_address(uint8_t* address, bool rpd) {
    uint8_t packet[32] = {0};
    uint8_t packetsize;
    uint8_t pipe_num;

    if(nrf24_rxpacket(packet, &packetsize, &pipe_num, rpd)) {
        if(validate_address(packet)) {
            uint8_t j = MAX_MAC_SIZE - 1;
            for(uint8_t i = 0; i < MAX_MAC_SIZE; i++) {
                address[i] = packet[j - i];
            }
            return true;
        }
    }
    return false;
}

static void wrap_up(Nrf24Tool* app) {
    SniffStatus* status = view_get_model(app->sniff_run);
    uint8_t ch;
    uint8_t addr[5];
    uint8_t altaddr[5];
    char trying[12];
    int idx;
    Setting* setting = app->settings->sniff_settings;
    uint8_t min_channel = setting[SNIFF_SETTING_MIN_CHANNEL].value.u8;
    uint8_t max_channel = setting[SNIFF_SETTING_MAX_CHANNEL].value.u8;

    while(app->tool_running) {
        idx = get_highest_idx();
        if(counts[idx] < COUNT_THRESHOLD) break;

        status->current_channel = 0;
        counts[idx] = 0;
        memcpy(addr, candidates[idx], 5);
        hexlify(addr, 5, trying);
        memcpy(status->tested_addr, trying, HEX_MAC_LEN + 1);
        view_commit_model(app->sniff_run, true);
        ch = nrf24_find_channel(
            addr, addr, ADDR_WIDTH_5_BYTES, target_rate, min_channel, max_channel);
        if(ch > max_channel) {
            alt_address(addr, altaddr);
            if(previously_confirmed(altaddr)) continue;
            hexlify(altaddr, 5, trying);
            memcpy(status->tested_addr, trying, HEX_MAC_LEN + 1);
            view_commit_model(app->sniff_run, true);
            ch = nrf24_find_channel(
                altaddr, altaddr, ADDR_WIDTH_5_BYTES, target_rate, min_channel, max_channel);
            memcpy(addr, altaddr, 5);
        }

        if(ch <= max_channel) {
            hexlify(addr, 5, top_address);
            if(confirmed_idx < MAX_CONFIRMED) {
                memcpy(confirmed[confirmed_idx++], addr, ADDR_WIDTH_5_BYTES);
                stream_write_format(app->stream, "%s\n", top_address);
                notification_message(app->notification, &sequence_double_vibro);
                notification_message(app->notification, &sequence_display_backlight_on);
            }
            status->addr_find_count = confirmed_idx;
            status->addr_new_count++;
            view_commit_model(app->sniff_run, true);
        }
    }
}

static bool load_file(Nrf24Tool* app) {
    SniffStatus* status = view_get_model(app->sniff_run);

    size_t file_size = 0;

    app->storage = furi_record_open(RECORD_STORAGE);
    app->stream = file_stream_alloc(app->storage);

    if(file_stream_open(app->stream, FILE_PATH_ADDR, FSAM_READ_WRITE, FSOM_OPEN_ALWAYS)) {
        file_size = stream_size(app->stream);
        // file empty
        if(file_size == 0) {
            confirmed_idx = 0;
        } else {
            FuriString* line = furi_string_alloc();
            stream_rewind(app->stream);
            while(stream_read_line(app->stream, line)) {
                // copy previously find adresse
                furi_string_trim(line);
                size_t line_lenght = furi_string_size(line);
                if(line_lenght > HEX_MAC_LEN) continue;
                if(confirmed_idx < MAX_CONFIRMED) {
                    uint8_t addr[MAX_MAC_SIZE];
                    unhexlify(furi_string_get_cstr(line), MAX_MAC_SIZE, addr);
                    if(!previously_confirmed(addr))
                        memcpy(confirmed[confirmed_idx++], addr, MAX_MAC_SIZE);
                } else {
                    FURI_LOG_E(LOG_TAG, "Sniffing : confirmed buffer overflow!");
                    return false;
                }
            }
            furi_string_free(line);
        }
        status->addr_find_count = confirmed_idx;
        status->addr_new_count = 0;
        view_commit_model(app->sniff_run, true);
        return true;
    }
    return false;
}

int32_t nrf24_sniff(void* ctx) {
    Nrf24Tool* app = (Nrf24Tool*)ctx;
    SniffStatus* status = view_get_model(app->sniff_run);

    uint8_t address[MAX_MAC_SIZE];
    uint32_t start = 0;
    Setting* setting = app->settings->sniff_settings;
    uint8_t min_channel = setting[SNIFF_SETTING_MIN_CHANNEL].value.u8;
    uint8_t max_channel = setting[SNIFF_SETTING_MAX_CHANNEL].value.u8;
    uint8_t target_channel = min_channel;

    memcpy(status->tested_addr, EMPTY_HEX, HEX_MAC_LEN + 1);
    memset(candidates, 0, sizeof(candidates));
    memset(counts, 0, sizeof(counts));
    memset(confirmed, 0, sizeof(confirmed));
    confirmed_idx = 0;
    total_candidates = 0;
    candidate_idx = 0;

    if(!load_file(app)) {
        FURI_LOG_E(LOG_TAG, "Sniffing : error opening adresses file !");
        app->tool_running = false;
        file_stream_close(app->stream);
        stream_free(app->stream);
        furi_record_close(RECORD_STORAGE);
        return 1;
    }

    sniff_nrf24_config.channel = target_channel;
    sniff_nrf24_config.data_rate =
        app->settings->sniff_settings[SNIFF_SETTING_DATA_RATE].value.d_r;
    nrf24_configure(&sniff_nrf24_config);
    nrf24_set_mode(NRF24_MODE_RX);
    status->current_channel = target_channel;
    view_commit_model(app->sniff_run, true);

    start = furi_get_tick();

    while(app->tool_running) {
        if(nrf24_sniff_address(address, setting[SNIFF_SETTING_RPD].value.b)) {
            int idx;
            uint8_t* top_addr;
            if(!previously_confirmed(address)) {
                idx = get_addr_index(address, MAX_MAC_SIZE);
                hexlify(address, MAX_MAC_SIZE, top_address);
                notification_message(app->notification, &sequence_blink_blue_10);
                if(idx == -1)
                    insert_addr(address, MAX_MAC_SIZE);
                else {
                    counts[idx]++;
                }

                top_addr = candidates[get_highest_idx()];
                hexlify(top_addr, MAX_MAC_SIZE, top_address);
            }
        }

        if(furi_get_tick() - start >= setting[SNIFF_SETTING_SCAN_TIME].value.u16) {
            target_channel++;
            if(target_channel > max_channel) target_channel = min_channel;

            wrap_up(app);
            sniff_nrf24_config.channel = target_channel;
            memcpy(status->tested_addr, EMPTY_HEX, HEX_MAC_LEN + 1);
            nrf24_configure(&sniff_nrf24_config);
            nrf24_set_mode(NRF24_MODE_RX);
            status->current_channel = target_channel;
            view_commit_model(app->sniff_run, true);
            start = furi_get_tick();
        }

        // delay for main thread
        furi_delay_ms(100);
    }
    // stop NRF24
    nrf24_set_mode(NRF24_MODE_POWER_DOWN);

    // close addresses file
    file_stream_close(app->stream);
    stream_free(app->stream);
    furi_record_close(RECORD_STORAGE);

    // update view
    view_commit_model(app->sniff_run, true);

    return 0;
}
