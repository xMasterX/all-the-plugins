#include "subghz_wardriving_gps.h"

//#include <expansion/expansion.h>
#include <loader/firmware_api/firmware_api.h>
#include <inttypes.h>
#include <math.h>

#define TAG "SubGhzWarDrivingGPS"

// Single-precision Haversine so the always-loaded FAP avoids the heavy double
// soft-float (sqrtf is the M4F hardware VSQRT.F32).
static float subghz_gps_deg2rad(float deg) {
    return deg * (float)M_PI / 180.0f;
}

static float subghz_gps_calc_distance(float lat1d, float lon1d, float lat2d, float lon2d) {
    float lat1r = subghz_gps_deg2rad(lat1d);
    float lon1r = subghz_gps_deg2rad(lon1d);
    float lat2r = subghz_gps_deg2rad(lat2d);
    float lon2r = subghz_gps_deg2rad(lon2d);
    float u = sinf((lat2r - lat1r) / 2.0f);
    float v = sinf((lon2r - lon1r) / 2.0f);
    return 2.0f * 6371.0f * asinf(sqrtf(u * u + cosf(lat1r) * cosf(lat2r) * v * v));
}

static float subghz_gps_calc_angle(float lat1, float lon1, float lat2, float lon2) {
    return atan2f(lat1 - lat2, lon1 - lon2) * 180.0f / (float)M_PI;
}

void subghz_gps_cat_realtime(
    SubGhzGPS* subghz_gps,
    FuriString* descr,
    float latitude,
    float longitude) {
    float distance =
        subghz_gps_calc_distance(latitude, longitude, subghz_gps->latitude, subghz_gps->longitude);
    float angle =
        subghz_gps_calc_angle(latitude, longitude, subghz_gps->latitude, subghz_gps->longitude);

    char* angle_str = "?";
    if(angle > -22.5f && angle <= 22.5f) {
        angle_str = "E";
    } else if(angle > 22.5f && angle <= 67.5f) {
        angle_str = "NE";
    } else if(angle > 67.5f && angle <= 112.5f) {
        angle_str = "N";
    } else if(angle > 112.5f && angle <= 157.5f) {
        angle_str = "NW";
    } else if(angle < -22.5f && angle >= -67.5f) {
        angle_str = "SE";
    } else if(angle < -67.5f && angle >= -112.5f) {
        angle_str = "S";
    } else if(angle < -112.5f && angle >= -157.5f) {
        angle_str = "SW";
    } else if(angle < -157.5f || angle >= 157.5f) {
        angle_str = "W";
    }

    furi_string_cat_printf(
        descr,
        "Realtime:  Sats: %d\r\n"
        "Distance: %.2f%s Dir: %s\r\n"
        "GPS time: %02d:%02d:%02d UTC",
        subghz_gps->satellites,
        (double)(subghz_gps->satellites > 0 ? distance > 1 ? distance : distance * 1000 : 0),
        distance > 1 ? "km" : "m",
        angle_str,
        subghz_gps->fix_hour,
        subghz_gps->fix_minute,
        subghz_gps->fix_second);
}

SubGhzGPS* subghz_gps_plugin_init(SubGhzGpsProtocol protocol, uint32_t baudrate) {
    //bool connected = expansion_is_connected(furi_record_open(RECORD_EXPANSION));
    //furi_record_close(RECORD_EXPANSION);
    //if(connected) return NULL;

    //expansion_disable(furi_record_open(RECORD_EXPANSION));
    //furi_record_close(RECORD_EXPANSION);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperApplication* plugin_app = flipper_application_alloc(storage, firmware_api_interface);
    do {
        FlipperApplicationPreloadStatus preload_res = flipper_application_preload(
            plugin_app, APP_ASSETS_PATH("plugins/subghz_plugin_gps.fal"));

        if(preload_res != FlipperApplicationPreloadStatusSuccess) {
            FURI_LOG_E(TAG, "Failed to preload GPS plugin. Code: %d\r\n", preload_res);
            break;
        }

        if(!flipper_application_is_plugin(plugin_app)) {
            FURI_LOG_E(TAG, "GPS plugin file is not a library\r\n");
            break;
        }

        FlipperApplicationLoadStatus load_status = flipper_application_map_to_memory(plugin_app);
        if(load_status != FlipperApplicationLoadStatusSuccess) {
            FURI_LOG_E(TAG, "Failed to load GPS plugin file. Code %d\r\n", load_status);
            break;
        }

        const FlipperAppPluginDescriptor* app_descriptor =
            flipper_application_plugin_get_descriptor(plugin_app);

        if(strcmp(app_descriptor->appid, "subghz_plugin_gps") != 0) {
            FURI_LOG_E(TAG, "GPS plugin type doesn't match\r\n");
            break;
        }

        if(app_descriptor->ep_api_version != 2) {
            FURI_LOG_E(
                TAG,
                "GPS plugin version %" PRIu32 " doesn't match\r\n",
                app_descriptor->ep_api_version);
            break;
        }

        void (*subghz_gps_init)(
            SubGhzGPS* subghz_gps, SubGhzGpsProtocol protocol, uint32_t baudrate) =
            app_descriptor->entry_point;

        SubGhzGPS* subghz_gps = malloc(sizeof(SubGhzGPS));
        subghz_gps->plugin_app = plugin_app;
        subghz_gps->baudrate = baudrate;
        subghz_gps_init(subghz_gps, protocol, baudrate);
        return subghz_gps;

    } while(false);
    flipper_application_free(plugin_app);
    furi_record_close(RECORD_STORAGE);

    //expansion_enable(furi_record_open(RECORD_EXPANSION));
    //furi_record_close(RECORD_EXPANSION);
    return NULL;
}

void subghz_gps_plugin_deinit(SubGhzGPS* subghz_gps) {
    subghz_gps->deinit(subghz_gps);
    flipper_application_free(subghz_gps->plugin_app);
    free(subghz_gps);
    furi_record_close(RECORD_STORAGE);

    //expansion_enable(furi_record_open(RECORD_EXPANSION));
    //furi_record_close(RECORD_EXPANSION);
}

void subghz_gps_stop(SubGhzGPS* subghz_gps) {
    if(!subghz_gps) return;
    if(subghz_gps->plugin_app) {
        subghz_gps_plugin_deinit(subghz_gps);
    } else {
        subghz_gps_rpc_stop(subghz_gps);
    }
}

SubGhzGPS* subghz_gps_apply(SubGhzGPS* current, SubGhzGpsProtocol protocol, uint32_t baudrate) {
    // The UART plugin defaults an unset baud so the running source and the
    // requested one are compared on the same resolved value.
    uint32_t resolved_baud = baudrate ? baudrate : 9600;

    if(current) {
        bool matches = current->protocol == protocol;
        if(matches &&
           (protocol == SubGhzGpsProtocolNmea || protocol == SubGhzGpsProtocolUbox)) {
            matches = current->baudrate == resolved_baud;
        }
        if(matches) return current;

        subghz_gps_stop(current);
    }

    switch(protocol) {
    case SubGhzGpsProtocolNmea:
    case SubGhzGpsProtocolUbox:
        return subghz_gps_plugin_init(protocol, resolved_baud);
    case SubGhzGpsProtocolRpc:
        return subghz_gps_rpc_start();
    default:
        return NULL;
    }
}
