#pragma once

#include <furi_hal.h>
#include <gps/gps.h>
#include <notification/notification.h>

// Define GPS_UART_CH per firmware origin
#ifdef FW_ORIGIN_Momentum
#include <momentum/momentum.h>
#define GPS_UART_CH (momentum_settings.uart_nmea_channel)
#else
#define GPS_UART_CH (FuriHalSerialIdUsart)
#endif

#define GPS_RX_BUF_SIZE 512

// GPS data source.
typedef enum {
    GpsProtocolNmea, // Local UART GPS module parsed with minmea
    GpsProtocolRpc, // Companion location over USB/BLE via RECORD_GPS
} GpsProtocol;

typedef struct {
    bool valid;
    float latitude;
    float longitude;
    bool module_detected; // NMEA: any sentence received; RPC: companion responded
    int satellite_count; // Number of satellites in view
} GpsCoordinates;

typedef struct {
    FuriMutex* mutex;
    GpsProtocol protocol;

    // NMEA UART transport (unused when protocol is RPC)
    FuriThread* thread;
    FuriStreamBuffer* rx_stream;
    uint8_t rx_buf[GPS_RX_BUF_SIZE];
    FuriHalSerialHandle* serial_handle;
    uint32_t baudrate;

    // RPC transport over RECORD_GPS (unused when protocol is NMEA)
    Gps* rpc;
    FuriTimer* rpc_timer;
    NotificationApp* notifications;
    uint32_t rpc_last_rx;
    uint32_t last_blink_tick;

    GpsCoordinates coordinates;
} GpsReader;

GpsReader* gps_reader_alloc(GpsProtocol protocol, uint32_t initial_baudrate);

// Free GPS reader
void gps_reader_free(GpsReader* gps_reader);

// Get current coordinates (thread-safe)
GpsCoordinates gps_reader_get_coordinates(GpsReader* gps_reader);

// Attach a NotificationApp for LED feedback (green on fix, red on error).
// Pass NULL to disable.
void gps_reader_set_notification(GpsReader* gps_reader, NotificationApp* notifications);

// Blink the feedback LED: green when ok is true, red otherwise.
void gps_reader_blink(GpsReader* gps_reader, bool ok);

// Get current baudrate (thread-safe)
uint32_t gps_reader_get_baudrate(GpsReader* gps_reader);

// Set GPS module baudrate at runtime.
// Internally reconfigures UART by deinit/init cycle.
bool gps_reader_set_baudrate(GpsReader* gps_reader, uint32_t baudrate);

// Get current GPS source (thread-safe)
GpsProtocol gps_reader_get_protocol(GpsReader* gps_reader);

// Switch GPS source at runtime, tearing down the previous transport and
// starting the new one. Returns true if the source is (now) the requested one.
bool gps_reader_set_protocol(GpsReader* gps_reader, GpsProtocol protocol);

// Internal transport helpers shared between gps_reader.c and gps_reader_rpc.c.
void gps_reader_start_nmea(GpsReader* gps_reader);
void gps_reader_stop_nmea(GpsReader* gps_reader);
void gps_reader_start_rpc(GpsReader* gps_reader);
void gps_reader_stop_rpc(GpsReader* gps_reader);
