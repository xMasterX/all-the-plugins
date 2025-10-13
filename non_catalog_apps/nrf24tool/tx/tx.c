#include "tx.h"
#include "stream/file_stream.h"
#include "notification/notification_messages.h"
#include <furi_hal_rtc.h>

static const char* FILE_PATH_TX_LOG = APP_DATA_PATH("tx.log");
char tx_payload_ascii[TX_PAYLOAD_SIZE_ASCII];
static uint8_t default_tx_addr[ADDR_WIDTH_5_BYTES] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};
uint8_t tx_payload_hex[MAX_PAYLOAD_SIZE];
FuriString* tx_payload_path;
TxPayloadType tx_payload_type;

Setting tx_defaults[] = {
    {.name = "Channel",
     .type = SETTING_TYPE_UINT8,
     .value.u8 = 0,
     .min = MIN_CHANNEL,
     .max = MAX_CHANNEL,
     .step = 1},
    {.name = "Data rate",
     .type = SETTING_TYPE_DATA_RATE,
     .value.d_r = DATA_RATE_2MBPS,
     .min = DATA_RATE_1MBPS,
     .max = DATA_RATE_250KBPS,
     .step = 1},
    {.name = "Addr width",
     .type = SETTING_TYPE_ADDR_WIDTH,
     .value.a_w = ADDR_WIDTH_5_BYTES,
     .min = ADDR_WIDTH_2_BYTES,
     .max = ADDR_WIDTH_5_BYTES,
     .step = 1},
    {.name = "TX addr",
     .type = SETTING_TYPE_ADDR,
     .value.addr = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7},
     .step = 1},
    {.name = "Payload size",
     .type = SETTING_TYPE_PAYLOAD_SIZE,
     .value.u8 = MAX_PAYLOAD_SIZE + 1,
     .min = MIN_PAYLOAD_SIZE,
     .max = MAX_PAYLOAD_SIZE + 1,
     .step = 1},
    {.name = "Load from file",
     .type = SETTING_TYPE_BOOL,
     .value.b = false,
     .min = 0,
     .max = 1,
     .step = 1},
    {.name = "Auto ack", .type = SETTING_TYPE_BOOL, .value.b = true, .min = 0, .max = 1, .step = 1},
    {.name = "Send count",
     .type = SETTING_TYPE_UINT8,
     .value.u8 = 1,
     .min = 0,
     .max = TX_MAX_REPEAT,
     .step = 1},
    {.name = "TX interval (ms)",
     .type = SETTING_TYPE_UINT16,
     .value.u16 = 50,
     .min = 50,
     .max = 2000,
     .step = 50},
    {.name = "ACK Payload",
     .type = SETTING_TYPE_BOOL,
     .value.b = false,
     .min = 0,
     .max = 1,
     .step = 1},
    {.name = "CRC",
     .type = SETTING_TYPE_CRC_LENGHT,
     .value.crc = CRC_2_BYTES,
     .min = CRC_DISABLED,
     .max = CRC_2_BYTES,
     .step = 1},
    {.name = "Tx Power",
     .type = SETTING_TYPE_TX_POWER,
     .value.t_p = TX_POWER_0DBM,
     .min = TX_POWER_M18DBM,
     .max = TX_POWER_0DBM,
     .step = 1},
    {.name = "ARC",
     .type = SETTING_TYPE_UINT8,
     .value.u8 = MAX_ARC_SIZE,
     .min = 0,
     .max = MAX_ARC_SIZE,
     .step = 1},
    {.name = "ARD",
     .type = SETTING_TYPE_UINT16,
     .value.u16 = 1500,
     .min = MIN_ARD_SIZE,
     .max = MAX_ARD_SIZE,
     .step = ARD_STEP},
    {.name = "Logging", .type = SETTING_TYPE_BOOL, .value.b = false, .min = 0, .max = 1, .step = 1},
};

NRF24L01_Config tx_nrf24_config = {
    .channel = MIN_CHANNEL,
    .data_rate = DATA_RATE_2MBPS,
    .tx_power = TX_POWER_0DBM,
    .crc_length = CRC_2_BYTES,
    .mac_len = ADDR_WIDTH_5_BYTES,
    .arc = 15,
    .ard = 1500,
    .auto_ack = {true, false, false, false, false, false},
    .dynamic_payload = {false, false, false, false, false, false},
    .ack_payload = false,
    .tx_no_ack = false,
    .tx_addr = default_tx_addr,
    .rx_addr = {NULL, NULL, NULL, NULL, NULL, NULL},
    .payload_size = {MAX_PAYLOAD_SIZE, 0, 0, 0, 0, 0}};

static void tx_configure_nrf24(Setting* tx_settings) {
    // channel
    tx_nrf24_config.channel = tx_settings[TX_SETTING_CHANNEL].value.u8;
    // data rate
    tx_nrf24_config.data_rate = tx_settings[TX_SETTING_DATA_RATE].value.d_r;
    // address width
    tx_nrf24_config.mac_len = tx_settings[TX_SETTING_ADDR_WIDTH].value.a_w;
    // TX address
    tx_nrf24_config.tx_addr = tx_settings[TX_SETTING_TX_ADDR].value.addr;
    // ack payload
    if(tx_settings[TX_SETTING_ACK_PAY].value.b) {
        tx_settings[TX_SETTING_AUTO_ACK].value.b = true;
        tx_settings[TX_SETTING_PAYLOAD_SIZE].value.u8 = MAX_PAYLOAD_SIZE + 1;
        tx_nrf24_config.ack_payload = true;
    } else {
        tx_nrf24_config.ack_payload = false;
    }
    // payload size
    if(tx_settings[TX_SETTING_PAYLOAD_SIZE].value.u8 > MAX_PAYLOAD_SIZE) {
        tx_nrf24_config.payload_size[0] = MAX_PAYLOAD_SIZE;
        tx_nrf24_config.dynamic_payload[0] = true;
        tx_settings[TX_SETTING_AUTO_ACK].value.b = true;
    } else {
        tx_nrf24_config.payload_size[0] = tx_settings[TX_SETTING_PAYLOAD_SIZE].value.u8;
        tx_nrf24_config.dynamic_payload[0] = false;
    }
    // auto ack
    if(tx_settings[TX_SETTING_AUTO_ACK].value.b) {
        tx_nrf24_config.auto_ack[0] = true;
        tx_nrf24_config.tx_no_ack = false;
        tx_nrf24_config.rx_addr[0] = tx_nrf24_config.tx_addr;
    } else {
        tx_nrf24_config.auto_ack[0] = false;
        tx_nrf24_config.tx_no_ack = true;
        tx_nrf24_config.rx_addr[0] = NULL;
    }
    // CRC
    tx_nrf24_config.crc_length = tx_settings[TX_SETTING_CRC].value.crc;
    // TX power
    tx_nrf24_config.tx_power = tx_settings[TX_SETTING_TX_POWER].value.t_p;
    // Auto Retransmit Count
    tx_nrf24_config.arc = tx_settings[TX_SETTING_ARC].value.u8;
    // Auto Retransmit Delay
    tx_nrf24_config.ard = tx_settings[TX_SETTING_ARD].value.u16;
    // send config to NRF24
    nrf24_configure(&tx_nrf24_config);
    nrf24_log_all_registers();
}

static bool open_log_file(Nrf24Tool* app) {
    app->storage = furi_record_open(RECORD_STORAGE);
    app->stream = file_stream_alloc(app->storage);

    if(file_stream_open(app->stream, FILE_PATH_TX_LOG, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        DateTime current_time;
        // Retrieve the current date and time from the RTC
        furi_hal_rtc_get_datetime(&current_time);
        stream_write_format(
            app->stream,
            "%04d-%02d-%02d | %02d:%02d:%02d -> TX Log start\n",
            current_time.year,
            current_time.month,
            current_time.day,
            current_time.hour,
            current_time.minute,
            current_time.second);

    } else {
        file_stream_close(app->stream);
        stream_free(app->stream);
        furi_record_close(RECORD_STORAGE);
        return false;
    }

    return true;
}

static void add_to_log(Nrf24Tool* app, TxStatus* status) {
    char time_stamp[50];
    char tx_addr[HEX_MAC_LEN + 1];
    char pl_hex[TX_PAYLOAD_SIZE_HEX];
    char pl_ascii[TX_PAYLOAD_SIZE_ASCII];
    DateTime current_time;

    // Retrieve the current date and time from the RTC
    furi_hal_rtc_get_datetime(&current_time);
    snprintf(
        time_stamp,
        sizeof(time_stamp),
        "%04d-%02d-%02d | %02d:%02d:%02d",
        current_time.year,
        current_time.month,
        current_time.day,
        current_time.hour,
        current_time.minute,
        current_time.second);
    hexlify(tx_nrf24_config.tx_addr, MAX_MAC_SIZE, tx_addr);
    hexlify(status->payload, status->payload_size, pl_hex);
    buffer_to_ascii(status->payload, status->payload_size, pl_ascii);
    stream_write_format(
        app->stream,
        "%s To: %-10s Size: %02u Hex: %-64s Ascii: %-32s Result: %s\n",
        time_stamp,
        tx_addr,
        status->payload_size,
        pl_hex,
        pl_ascii,
        status->send_ok ? "SUCCES" : "FAILED");
    if(app->settings->tx_settings[TX_SETTING_ACK_PAY].value.b && status->send_ok) {
        hexlify(status->ack_payload, status->ack_payload_size, pl_hex);
        buffer_to_ascii(status->ack_payload, status->ack_payload_size, pl_ascii);
        stream_write_format(
            app->stream,
            "%s ACK payload received -> Size: %02u Hex: %-64s Ascii: %-32s\n",
            time_stamp,
            status->ack_payload_size,
            pl_hex,
            pl_ascii);
    }
}

static int8_t
    read_payload_file(Stream* stream, FuriString* line, uint8_t* payload, int8_t payload_size) {
    while(true) {
        size_t line_length = furi_string_size(line);

        if(line_length > 0) {
            size_t payload_line_length;
            const char* line_cstr = furi_string_get_cstr(line);

            size_t max_length = (payload_size < 0) ? MAX_PAYLOAD_SIZE :
                                                     MIN((size_t)payload_size, MAX_PAYLOAD_SIZE);

            if(is_hex_line_furi(line)) {
                payload_line_length = MIN(line_length / 2, max_length);
                unhexlify(line_cstr, payload_line_length, payload);
                furi_string_right(line, payload_line_length * 2);
            } else {
                payload_line_length = MIN(line_length, max_length);
                memcpy(payload, line_cstr, payload_line_length);
                furi_string_right(line, payload_line_length);
            }

            return payload_line_length;
        }
        if(stream_eof(stream)) {
            stream_seek(stream, 0, StreamOffsetFromStart);
            return -1;
        }
        if(!stream_read_line(stream, line)) {
            return 0;
        } else {
            furi_string_trim(line);
        }
    }
}

int32_t nrf24_tx(void* ctx) {
    Nrf24Tool* app = (Nrf24Tool*)ctx;
    Setting* setting = app->settings->tx_settings;
    TxStatus* status = (TxStatus*)view_get_model(app->tx_run);
    Stream* payload_stream = NULL;

    bool send_result = false;
    uint8_t tx_payload[MAX_PAYLOAD_SIZE];
    int8_t tx_payload_size = 0;
    int8_t payload_size = (setting[TX_SETTING_PAYLOAD_SIZE].value.u8 <= MAX_PAYLOAD_SIZE) ?
                              setting[TX_SETTING_PAYLOAD_SIZE].value.u8 :
                              -1;
    int8_t repeat = (setting[TX_SETTING_SEND_COUNT].value.u8 > 0) ?
                        setting[TX_SETTING_SEND_COUNT].value.u8 :
                        -1;
    uint16_t delay = setting[TX_SETTING_TX_INTERVAL].value.u16;

    // test payload size
    if(payload_size == 0 || payload_size > MAX_PAYLOAD_SIZE) {
        app->tool_running = false;
        return 1;
    }

    // open log file if logging enabled
    if(setting[TX_SETTING_LOGGING].value.b) {
        if(!open_log_file(app)) {
            FURI_LOG_E(LOG_TAG, "Error opening TX file log");
            app->tool_running = false;
            return 1;
        }
    }

    // open payload file if enabled
    if(tx_payload_type == TX_PAYLOAD_TYPE_FILE) {
        if(!setting[TX_SETTING_LOGGING].value.b) {
            app->storage = furi_record_open(RECORD_STORAGE);
        }
        payload_stream = file_stream_alloc(app->storage);
        if(!file_stream_open(
               payload_stream,
               furi_string_get_cstr(tx_payload_path),
               FSAM_READ,
               FSOM_OPEN_EXISTING)) {
            FURI_LOG_E(LOG_TAG, "Error on TX payload file");

            file_stream_close(payload_stream);
            stream_free(payload_stream);
            if(setting[TX_SETTING_LOGGING].value.b) {
                file_stream_close(app->stream);
                stream_free(app->stream);
            }
            furi_record_close(RECORD_STORAGE);
            app->tool_running = false;
            return 1;
        } else {
            stream_seek(payload_stream, 0, StreamOffsetFromStart);
        }
    }

    FuriString* payload_furi_line = furi_string_alloc();

    // configure nrf24
    tx_configure_nrf24(setting);
    view_commit_model(app->tx_run, true);

    // set payload data if not from file
    memset(tx_payload, 0x00, sizeof(tx_payload));
    if(tx_payload_type == TX_PAYLOAD_TYPE_ASCII) {
        tx_payload_size = strlen(tx_payload_ascii);
        memcpy(tx_payload, tx_payload_ascii, tx_payload_size);
    } else if(tx_payload_type == TX_PAYLOAD_TYPE_HEX) {
        tx_payload_size = MAX_PAYLOAD_SIZE;
        memcpy(tx_payload, tx_payload_hex, tx_payload_size);
    }
    // empty payload => nothing to do
    if((tx_payload_type == TX_PAYLOAD_TYPE_ASCII || tx_payload_type == TX_PAYLOAD_TYPE_HEX) &&
       tx_payload_size <= 0) {
        app->tool_running = false;
    }

    // MAIN LOOP
    while(app->tool_running) {
        // get payload if from file
        if(tx_payload_type == TX_PAYLOAD_TYPE_FILE) {
            tx_payload_size =
                read_payload_file(payload_stream, payload_furi_line, tx_payload, payload_size);
            if(tx_payload_size < 0 && repeat > 0) {
                repeat--;
            }
        }

        // send payload
        if(tx_payload_size > 0) {
            tx_payload_size = (payload_size < 0) ? MIN(tx_payload_size, MAX_PAYLOAD_SIZE) :
                                                   payload_size;

            // update view
            memcpy(status->payload, tx_payload, MAX_PAYLOAD_SIZE);
            status->payload_size = tx_payload_size;
            view_commit_model(app->tx_run, true);

            // Send command
            send_result = nrf24_txpacket(
                tx_payload,
                tx_payload_size,
                !setting[TX_SETTING_AUTO_ACK].value.b,
                (setting[TX_SETTING_ACK_PAY].value.b) ? status->ack_payload : NULL,
                (setting[TX_SETTING_ACK_PAY].value.b) ? &status->ack_payload_size : NULL);

            // LED Flash
            notification_message(app->notification, &sequence_blink_red_100);

            // update view
            status->send_ok = send_result;
            status->send_failed = !send_result;
            status->ack_payload_enabled = setting[TX_SETTING_ACK_PAY].value.b;
            view_commit_model(app->tx_run, true);

            // logging
            if(setting[TX_SETTING_LOGGING].value.b) {
                add_to_log(app, status);
            }
            char buffer[MAX_PAYLOAD_SIZE * 2 + 1];
            hexlify(tx_payload, tx_payload_size, buffer);
            for(int8_t i = 0; i < tx_payload_size; i++) {
                buffer[i] = (char)tx_payload[i];
            }
            buffer[tx_payload_size] = '\0';
            // decrease repeat counter
            if(tx_payload_type != TX_PAYLOAD_TYPE_FILE && repeat > 0) {
                repeat--;
            }
        }

        // exit loop
        if(repeat == 0) {
            app->tool_running = false;
            break;
        }

        // delay before next transmission
        furi_delay_ms(delay);

        // test nrf24 connection
        nrf24_test_connection(app, VIEW_TX_RUN, VIEW_TX_RUN);
    }

    // stop nrf24
    nrf24_set_mode(NRF24_MODE_POWER_DOWN);

    // close payload file if enabled
    if(setting[TX_SETTING_FROM_FILE].value.b) {
        file_stream_close(payload_stream);
        stream_free(payload_stream);
        if(!setting[TX_SETTING_LOGGING].value.b) {
            furi_record_close(RECORD_STORAGE);
        }
    }

    furi_string_free(payload_furi_line);

    // close log file if logging enabled
    if(setting[TX_SETTING_LOGGING].value.b) {
        file_stream_close(app->stream);
        stream_free(app->stream);
        furi_record_close(RECORD_STORAGE);
    }

    return 0;
}
