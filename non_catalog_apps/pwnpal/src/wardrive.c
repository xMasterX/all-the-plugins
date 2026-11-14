#include "../include/wardrive.h"

#include <furi.h>
#include <datetime/datetime.h>
#include <furi_hal_rtc.h>
#include <stdio.h>
#include <string.h>

// WiGLE 1.4 pre-header + column header, written once on file creation
static const char* WARDRIVE_PREHEADER =
    "WigleWifi-1.4,appRelease=pwnpal,model=Flipper Zero,release=1.0,"
    "device=esp32-marauder,display=,board=Feberis,brand=pwnagotchi-flipper\n"
    "MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,"
    "AltitudeMeters,AccuracyMeters,Type\n";

// FirstSeen as WiGLE "yyyy-MM-dd HH:mm:ss" from the RTC
static void wardrive_now_str(char* out, size_t out_sz) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    snprintf(
        out,
        out_sz,
        "%04u-%02u-%02u %02u:%02u:%02u",
        (unsigned)dt.year,
        (unsigned)dt.month,
        (unsigned)dt.day,
        (unsigned)dt.hour,
        (unsigned)dt.minute,
        (unsigned)dt.second);
}

// CSV-quote `in`, doubling embedded quotes, so a comma/quote in an ESSID can't shift columns
static void wardrive_quote(char* out, size_t out_sz, const char* in) {
    size_t n = 0;
    if(out_sz < 3) {
        if(out_sz) out[0] = '\0';
        return;
    }
    out[n++] = '"';
    for(const char* c = in; *c && n < out_sz - 2; c++) {
        if(*c == '"') {
            if(n >= out_sz - 3) break; // no room for the doubled quote
            out[n++] = '"';
        }
        out[n++] = *c;
    }
    out[n++] = '"';
    out[n] = '\0';
}

bool wardrive_log(
    Storage* storage,
    const char* mac,
    const char* ssid,
    const char* auth,
    int channel,
    int rssi,
    const char* lat,
    const char* lon) {
    if(!lat || !lat[0] || !lon || !lon[0]) return false; // only geotagged rows
    if(!mac) mac = "";
    if(!ssid) ssid = "";
    if(!auth) auth = "";

    storage_common_mkdir(storage, PWNPAL_WARDRIVE_DIR); // no-op if it exists

    File* f = storage_file_alloc(storage);
    bool ok = storage_file_open(f, PWNPAL_WARDRIVE_PATH, FSAM_WRITE, FSOM_OPEN_APPEND);
    if(ok && storage_file_size(f) == 0) {
        size_t hlen = strlen(WARDRIVE_PREHEADER);
        ok = storage_file_write(f, WARDRIVE_PREHEADER, hlen) == hlen;
    }
    if(ok) {
        char ts[32];
        wardrive_now_str(ts, sizeof(ts));

        char qssid[70];
        wardrive_quote(qssid, sizeof(qssid), ssid);

        char row[256];
        int rn = snprintf(
            row,
            sizeof(row),
            "%s,%s,%s,%s,%d,%d,%s,%s,0,0,WIFI\n",
            mac,
            qssid,
            auth,
            ts,
            channel,
            rssi,
            lat,
            lon);
        if(rn > 0) {
            size_t rlen = (rn < (int)sizeof(row)) ? (size_t)rn : sizeof(row) - 1;
            ok = storage_file_write(f, row, rlen) == rlen;
        } else {
            ok = false;
        }
    }
    storage_file_close(f);
    storage_file_free(f);
    return ok;
}
