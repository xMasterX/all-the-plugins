// pure geo/identity helpers — no Flipper/Furi deps, so host-testable (tests/test_geo.cpp).
// anything touching the model/SDK stays in pwnpal_app.c.
#pragma once
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// cap triangulation samples so centroid sums stay bounded (else float drift)
#define LOC_SAMPLE_CAP 4000

// parse a decimal-degree string without atof (no %f/newlib-nano float dep). 1e9 = unknown.
static inline float parse_deg(const char* s) {
    if(!s || !s[0]) return 1e9f;
    float sign = 1.0f, v = 0.0f;
    const char* p = s;
    if(*p == '-') {
        sign = -1.0f;
        p++;
    } else if(*p == '+') {
        p++;
    }
    while(*p >= '0' && *p <= '9') {
        v = v * 10.0f + (float)(*p - '0');
        p++;
    }
    if(*p == '.') {
        p++;
        float f = 0.1f;
        while(*p >= '0' && *p <= '9') {
            v += (float)(*p - '0') * f;
            f *= 0.1f;
            p++;
        }
    }
    return sign * v;
}

// sanity-check a lat/lon pair: in-range and not null island (~0,0, the no-fix GPS default)
static inline bool coord_ok(const char* lat, const char* lon) {
    float la = parse_deg(lat), lo = parse_deg(lon);
    if(la < -90.0f || la > 90.0f || lo < -180.0f || lo > 180.0f) return false;
    if(la > -0.5f && la < 0.5f && lo > -0.5f && lo < 0.5f) return false;
    return true;
}

// approx great-circle km (equirectangular, single precision — fine for city-scale + outlier guard)
static inline float geo_km(float lat1, float lon1, float lat2, float lon2) {
    float coslat = cosf(lat1 * 3.14159265f / 180.0f);
    float dn = lat2 - lat1, de = (lon2 - lon1) * coslat;
    return sqrtf(dn * dn + de * de) * 111.0f;
}

// a real pwngrid identity is exactly 64 hex (SHA256 fingerprint); reject anything else
// (pwngrid's ^[a-fA-F0-9]{64}$ gate) so a mis-parsed beacon can't spawn a bogus peer
static inline bool identity_is_64hex(const char* s) {
    int n = 0;
    for(; s[n]; n++) {
        if(n >= 64) return false;
        char c = s[n];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if(!hex) return false;
    }
    return n == 64;
}

// fold a geotagged sighting into a running RSSI-weighted centroid: stronger signal -> more
// weight; averaging cancels GPS jitter. O(1). rssi==0 skipped, capped at LOC_SAMPLE_CAP.
static inline void loc_accumulate(
    uint16_t* n, float* w_sum, float* wlat_sum, float* wlon_sum, float lat, float lon, int rssi) {
    if(rssi == 0) return;
    if(*n >= LOC_SAMPLE_CAP) return;
    float w = (float)(rssi + 100); // ~ -100dBm floor -> tiny weight, -30dBm close -> ~70
    if(w < 1.0f) w = 1.0f;
    *w_sum += w;
    *wlat_sum += w * lat;
    *wlon_sum += w * lon;
    if(*n < 0xFFFF) (*n)++;
}

// best position: weighted centroid with >=2 samples + triangulation on, else the strongest
// fix (fix_lat 1e9 = none). false if no loc.
static inline bool loc_estimate(
    bool tri, uint16_t n, float w_sum, float wlat_sum, float wlon_sum, float fix_lat,
    float fix_lon, float* out_lat, float* out_lon) {
    if(tri && n >= 2 && w_sum > 0.0f) {
        *out_lat = wlat_sum / w_sum;
        *out_lon = wlon_sum / w_sum;
        return true;
    }
    if(fix_lat < 1e8f) {
        *out_lat = fix_lat;
        *out_lon = fix_lon;
        return true;
    }
    return false;
}
