#include "rx.h"
#include "stream/file_stream.h"
#include "notification/notification_messages.h"
#include <furi_hal_rtc.h>

RxBuffer rx_buffer;

static const char* FILE_PATH_RX_LOG = APP_DATA_PATH("rx.log");

Setting rx_defaults[] = {
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
    {.name = "CRC",
     .type = SETTING_TYPE_CRC_LENGHT,
     .value.crc = CRC_2_BYTES,
     .min = CRC_DISABLED,
     .max = CRC_2_BYTES,
     .step = 1},
    {.name = "RPD", .type = SETTING_TYPE_BOOL, .value.b = false, .min = 0, .max = 1, .step = 1},
    {.name = "ACK Payload",
     .type = SETTING_TYPE_PIPE_NUM,
     .value.i8 = -1,
     .min = -1,
     .max = MAX_PIPE,
     .step = 1},
    {.name = "P0: payload",
     .type = SETTING_TYPE_PAYLOAD_SIZE,
     .value.u8 = MAX_PAYLOAD_SIZE + 1,
     .min = MIN_PAYLOAD_SIZE - 1,
     .max = MAX_PAYLOAD_SIZE + 1,
     .step = 1},
    {.name = "P0: auto ack",
     .type = SETTING_TYPE_BOOL,
     .value.b = true,
     .min = 0,
     .max = 1,
     .step = 1},
    {.name = "P0: addr",
     .type = SETTING_TYPE_ADDR,
     .value.addr = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7},
     .step = 1},
    {.name = "P1: payload",
     .type = SETTING_TYPE_PAYLOAD_SIZE,
     .value.u8 = MAX_PAYLOAD_SIZE + 1,
     .min = MIN_PAYLOAD_SIZE - 1,
     .max = MAX_PAYLOAD_SIZE + 1,
     .step = 1},
    {.name = "P1: auto ack",
     .type = SETTING_TYPE_BOOL,
     .value.b = true,
     .min = 0,
     .max = 1,
     .step = 1},
    {.name = "P1: addr",
     .type = SETTING_TYPE_ADDR,
     .value.addr = {0xC2, 0xC2, 0xC2, 0xC2, 0xC2},
     .step = 1},
    {.name = "P2: payload",
     .type = SETTING_TYPE_PAYLOAD_SIZE,
     .value.u8 = MAX_PAYLOAD_SIZE + 1,
     .min = MIN_PAYLOAD_SIZE - 1,
     .max = MAX_PAYLOAD_SIZE + 1,
     .step = 1},
    {.name = "P2: auto ack",
     .type = SETTING_TYPE_BOOL,
     .value.b = true,
     .min = 0,
     .max = 1,
     .step = 1},
    {.name = "P2: addr", .type = SETTING_TYPE_ADDR_1BYTE, .value.addr = {0xC3}, .step = 1},
    {.name = "P3: payload",
     .type = SETTING_TYPE_PAYLOAD_SIZE,
     .value.u8 = MAX_PAYLOAD_SIZE + 1,
     .min = MIN_PAYLOAD_SIZE - 1,
     .max = MAX_PAYLOAD_SIZE + 1,
     .step = 1},
    {.name = "P3: auto ack",
     .type = SETTING_TYPE_BOOL,
     .value.b = true,
     .min = 0,
     .max = 1,
     .step = 1},
    {.name = "P3: addr", .type = SETTING_TYPE_ADDR_1BYTE, .value.addr = {0xC4}, .step = 1},
    {.name = "P4: payload",
     .type = SETTING_TYPE_PAYLOAD_SIZE,
     .value.u8 = MAX_PAYLOAD_SIZE + 1,
     .min = MIN_PAYLOAD_SIZE - 1,
     .max = MAX_PAYLOAD_SIZE + 1,
     .step = 1},
    {.name = "P4: auto ack",
     .type = SETTING_TYPE_BOOL,
     .value.b = true,
     .min = 0,
     .max = 1,
     .step = 1},
    {.name = "P4: addr", .type = SETTING_TYPE_ADDR_1BYTE, .value.addr = {0xC5}, .step = 1},
    {.name = "P5: payload",
     .type = SETTING_TYPE_PAYLOAD_SIZE,
     .value.u8 = MAX_PAYLOAD_SIZE + 1,
     .min = MIN_PAYLOAD_SIZE - 1,
     .max = MAX_PAYLOAD_SIZE + 1,
     .step = 1},
    {.name = "P5: auto ack",
     .type = SETTING_TYPE_BOOL,
     .value.b = true,
     .min = 0,
     .max = 1,
     .step = 1},
    {.name = "P5: addr", .type = SETTING_TYPE_ADDR_1BYTE, .value.addr = {0xC6}, .step = 1},
    {.name = "Logging", .type = SETTING_TYPE_BOOL, .value.b = false, .min = 0, .max = 1, .step = 1},
};

NRF24L01_Config rx_nrf24_config = {
    .channel = MIN_CHANNEL,
    .data_rate = DATA_RATE_2MBPS,
    .tx_power = TX_POWER_0DBM,
    .crc_length = CRC_2_BYTES,
    .mac_len = ADDR_WIDTH_5_BYTES,
    .arc = 15,
    .ard = 1500,
    .auto_ack = {false, false, false, false, false, false},
    .dynamic_payload = {false, false, false, false, false, false},
    .ack_payload = false,
    .tx_no_ack = false,
    .tx_addr = NULL,
    .rx_addr = {NULL, NULL, NULL, NULL, NULL, NULL},
    .payload_size = {0, 0, 0, 0, 0, 0}};

static void add_to_rx_buffer(
    RxBuffer* buffer,
    const char* data_hex,
    const char* data_ascii,
    uint8_t byte_size,
    uint8_t from_pipe,
    const char* from_addr) {
    // Add the data into the buffer at the current index
    strncpy(buffer->data_hex[buffer->index], data_hex, RX_STRING_SIZE);
    strncpy(buffer->data_ascii[buffer->index], data_ascii, MAX_PAYLOAD_SIZE);

    // Add the pipe number + payload size
    buffer->from_pipe[buffer->index] = from_pipe;
    buffer->size[buffer->index] = byte_size;

    // Add the address from which the data came
    strncpy(buffer->from_addr[buffer->index], from_addr, HEX_MAC_LEN);

    // Update the buffer index in a circular fashion
    buffer->index = (buffer->index + 1) % RX_BUFFER_SIZE;

    // If the buffer is not full, increment the fill level
    if(buffer->fill_level < RX_BUFFER_SIZE) {
        buffer->fill_level++;
    }
}

static void add_to_log(Nrf24Tool* app, RxBuffer* buffer) {
    char time_stamp[50];
    DateTime current_time;
    uint8_t index;
    if(buffer->index > 0) {
        index = buffer->index - 1;
    } else {
        index = RX_BUFFER_SIZE;
    }
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
    stream_write_format(
        app->stream,
        "%s From: %-10s (P%u) Size: %02u Hex: %-64s Ascii: %-32s\n",
        time_stamp,
        buffer->from_addr[index],
        buffer->from_pipe[index],
        buffer->size[index],
        buffer->data_hex[index],
        buffer->data_ascii[index]);
}

static void reset_rx_buffer(RxBuffer* buffer) {
    // Reset all fields in the buffer
    for(uint8_t i = 0; i < RX_BUFFER_SIZE; i++) {
        buffer->data_hex[i][0] = '\0'; // Clear the string
        buffer->data_ascii[i][0] = '\0'; // Clear the string
        buffer->from_pipe[i] = 0; // Clear the pipe number
        buffer->from_addr[i][0] = '\0'; // Clear the address
    }

    // Reset the index and fill level
    buffer->index = 0;
    buffer->fill_level = 0;
}

static uint8_t rx_set_ack_payload(Setting* rx_settings) {
    int8_t pipe_num = rx_settings[RX_SETTING_ACK_PAY].value.i8;
    if(pipe_num < MIN_PIPE || pipe_num > MAX_PIPE) {
        return 0;
    }
    char ack_payload[MAX_PAYLOAD_SIZE + 1];
    char rx_addr[HEX_MAC_LEN + 1];
    nrf24_flush_tx();
    hexlify(rx_nrf24_config.rx_addr[pipe_num], rx_nrf24_config.mac_len, rx_addr);

    int ack_payload_lenght =
        snprintf(ack_payload, MAX_PAYLOAD_SIZE + 1, "ACK from: %s (Flipper 0)", rx_addr);

    return nrf24_write_ack_payload(pipe_num, (uint8_t*)ack_payload, ack_payload_lenght);
}

static void rx_configure_nrf24(Setting* rx_settings) {
    // channel
    rx_nrf24_config.channel = rx_settings[RX_SETTING_CHANNEL].value.u8;
    // data rate
    rx_nrf24_config.data_rate = rx_settings[RX_SETTING_DATA_RATE].value.d_r;
    // address width
    rx_nrf24_config.mac_len = rx_settings[RX_SETTING_ADDR_WIDTH].value.a_w;
    // ack payload
    rx_nrf24_config.ack_payload = (rx_settings[RX_SETTING_ACK_PAY].value.i8 >= MIN_PIPE);
    // CRC
    rx_nrf24_config.crc_length = rx_settings[RX_SETTING_CRC].value.crc;
    // RX addresses + payload lenght
    bool p1_addr_set = false;
    for(uint8_t i = 0; i <= MAX_PIPE; i++) {
        uint8_t payload_lenght = rx_settings[RX_SETTING_P0_PAYLOAD + (i * 3)].value.u8;
        bool auto_ack = rx_settings[RX_SETTING_P0_AUTO_ACK + (i * 3)].value.b;
        uint8_t* rx_addr = rx_settings[RX_SETTING_P0_ADDR + (i * 3)].value.addr;

        if(payload_lenght < MIN_PAYLOAD_SIZE) { // pipe disable
            rx_nrf24_config.rx_addr[i] = rx_addr;
            rx_nrf24_config.payload_size[i] = 0;
            rx_nrf24_config.auto_ack[i] = false;
            rx_nrf24_config.dynamic_payload[i] = false;
        } else if(payload_lenght > MAX_PAYLOAD_SIZE) { // dynamic payload enbale
            rx_nrf24_config.rx_addr[i] = rx_addr;
            rx_nrf24_config.auto_ack[i] = auto_ack;
            rx_nrf24_config.payload_size[i] = MAX_PAYLOAD_SIZE;
            rx_nrf24_config.dynamic_payload[i] = true;
            if(i == 1) p1_addr_set = true;
        } else { // static payload
            rx_nrf24_config.rx_addr[i] = rx_addr;
            rx_nrf24_config.auto_ack[i] = auto_ack;
            rx_nrf24_config.payload_size[i] = payload_lenght;
            rx_nrf24_config.dynamic_payload[i] = false;
            if(i == 1) p1_addr_set = true;
        }
        //pipes 2, 3, 4 or 5 enabled and not pipe1 -> set pipe 1 addr for base address
        if(!p1_addr_set && i > 1 && payload_lenght > 0) {
            nrf24_set_rx_mac(
                rx_settings[RX_SETTING_P1_ADDR].value.addr, rx_nrf24_config.mac_len, 1);
            p1_addr_set = true;
        }
    }
    // TX address
    rx_nrf24_config.tx_addr = rx_settings[RX_SETTING_P0_ADDR].value.addr;
    nrf24_configure(&rx_nrf24_config);
    // load ack payload if enabled
    if(rx_nrf24_config.ack_payload) {
        rx_set_ack_payload(rx_settings);
    }
}

static bool open_log_file(Nrf24Tool* app) {
    app->storage = furi_record_open(RECORD_STORAGE);
    app->stream = file_stream_alloc(app->storage);

    if(file_stream_open(app->stream, FILE_PATH_RX_LOG, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        DateTime current_time;
        // Retrieve the current date and time from the RTC
        furi_hal_rtc_get_datetime(&current_time);
        stream_write_format(
            app->stream,
            "%04d-%02d-%02d | %02d:%02d:%02d -> RX Log start\n",
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

int32_t nrf24_rx(void* ctx) {
    Nrf24Tool* app = (Nrf24Tool*)ctx;
    Setting* setting = app->settings->rx_settings;

    uint8_t rx_payload_data[MAX_PAYLOAD_SIZE];
    uint8_t rx_payload_size = 0;
    uint8_t rx_pipe_num = 0;
    char rx_payload_hex[RX_STRING_SIZE + 1];
    char rx_payload_ascii[MAX_PAYLOAD_SIZE + 1];
    uint8_t rx_addr_byte[MAX_MAC_SIZE];
    char rx_addr_hex[HEX_MAC_LEN + 1];
    bool rpd = setting[RX_SETTING_RPD].value.b;

    //RxStatus* status = view_get_model(app->rx_run);
    // open log file if logging enabled
    if(setting[RX_SETTING_LOGGING].value.b) {
        if(!open_log_file(app)) {
            FURI_LOG_E(LOG_TAG, "Error opening RX file log");
            app->tool_running = false;
            return 1;
        }
    }

    // configure nrf24
    rx_configure_nrf24(setting);
    view_commit_model(app->rx_run, true);

    // clear reception buffer
    reset_rx_buffer(&rx_buffer);

    // set RX mode on NFR24
    nrf24_set_mode(NRF24_MODE_RX);

    // main loop
    while(app->tool_running) {
        if(nrf24_rxpacket(rx_payload_data, &rx_payload_size, &rx_pipe_num, rpd)) {
            hexlify(rx_payload_data, rx_payload_size, rx_payload_hex);
            buffer_to_ascii(rx_payload_data, rx_payload_size, rx_payload_ascii);
            nrf24_get_rx_mac(rx_addr_byte, rx_pipe_num);
            hexlify(rx_addr_byte, nrf24_get_maclen(), rx_addr_hex);
            add_to_rx_buffer(
                &rx_buffer,
                rx_payload_hex,
                rx_payload_ascii,
                rx_payload_size,
                rx_pipe_num,
                rx_addr_hex);
            if(setting[RX_SETTING_LOGGING].value.b) {
                add_to_log(app, &rx_buffer);
            }
            notification_message(app->notification, &sequence_blink_blue_10);
            notification_message(app->notification, &sequence_display_backlight_on);
            view_commit_model(app->rx_run, true);

            // load ack payload
            if(rx_nrf24_config.ack_payload) {
                rx_set_ack_payload(setting);
            }
        }
        furi_delay_ms(100);
    }

    // NRF24 idle mode
    nrf24_set_mode(NRF24_MODE_POWER_DOWN);

    // close log file if logging enabled
    if(setting[RX_SETTING_LOGGING].value.b) {
        file_stream_close(app->stream);
        stream_free(app->stream);
        furi_record_close(RECORD_STORAGE);
    }

    return 0;
}
