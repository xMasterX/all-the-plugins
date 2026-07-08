#pragma once

#include <furi_hal.h>
#include <flipper_application/flipper_application.h>
#include <gps/gps.h>
#include "ubox.h"

#define RX_BUF_SIZE 1024

// Order matches the config menu.
typedef enum {
    SubGhzGpsProtocolOff,
    SubGhzGpsProtocolRpc,
    SubGhzGpsProtocolNmea,
    SubGhzGpsProtocolUbox,
} SubGhzGpsProtocol;

typedef struct SubGhzGPS SubGhzGPS;

struct SubGhzGPS {
    FlipperApplication* plugin_app; // set for the UART plugin, NULL for inline RPC
    FuriThread* thread;
    FuriStreamBuffer* rx_stream;
    uint8_t rx_buf[RX_BUF_SIZE];
    FuriHalSerialHandle* serial_handle;
    SubGhzGpsProtocol protocol;
    uint32_t baudrate;
    UboxRx ubox;
    FuriTimer* timer;

    // inline RPC transport (RECORD_GPS), unused by the UART plugin
    Gps* rpc;
    FuriTimer* rpc_timer;
    uint32_t rpc_last_rx;

    float latitude;
    float longitude;
    int satellites;
    uint8_t fix_second;
    uint8_t fix_minute;
    uint8_t fix_hour;

    void (*deinit)(SubGhzGPS* subghz_gps);
};

// Realtime info string (distance/direction/sats/time), shared by all sources.
void subghz_gps_cat_realtime(
    SubGhzGPS* subghz_gps,
    FuriString* descr,
    float latitude,
    float longitude);

/**
 * Load the UART GPS plugin (.fal) for NMEA or Ubox.
 *
 * @return SubGhzGPS* object, or NULL on load failure
*/
SubGhzGPS* subghz_gps_plugin_init(SubGhzGpsProtocol protocol, uint32_t baudrate);

/**
 * Unload the UART GPS plugin.
*/
void subghz_gps_plugin_deinit(SubGhzGPS* subghz_gps);

/**
 * Start the inline RPC GPS source (RECORD_GPS, companion over USB/BLE).
 * No plugin is loaded. Returns a SubGhzGPS with the same data contract.
*/
SubGhzGPS* subghz_gps_rpc_start(void);

/**
 * Stop the inline RPC GPS source.
*/
void subghz_gps_rpc_stop(SubGhzGPS* subghz_gps);

/**
 * Stop and free whatever GPS source is active (UART plugin or inline RPC).
 * NULL-safe.
*/
void subghz_gps_stop(SubGhzGPS* subghz_gps);

/**
 * Reconcile the active GPS source with the current settings.
 *
 * Loads, reloads (on protocol or baudrate change) or unloads the source so it
 * matches @p protocol / @p baudrate. A no-op when the running source already
 * matches, so it is cheap to call before every Read. Call as:
 *     subghz->gps = subghz_gps_apply(subghz->gps, protocol, baudrate);
 *
 * @param current  currently active source, or NULL if none
 * @return the source that now matches the settings, or NULL if protocol is off
 *         (or a UART plugin failed to load)
*/
SubGhzGPS* subghz_gps_apply(SubGhzGPS* current, SubGhzGpsProtocol protocol, uint32_t baudrate);
