#include "src/data/cfs_shades.h"

// "Redmean" weighted RGB distance (squared): cheap, integer-only, and much
// closer to perceived difference than plain Euclidean RGB.
static uint32_t cfs_color_dist(const CfsColorPreset* c, uint8_t r, uint8_t g, uint8_t b) {
    int32_t rmean = ((int32_t)c->r + r) / 2;
    int32_t dr = (int32_t)c->r - r;
    int32_t dg = (int32_t)c->g - g;
    int32_t db = (int32_t)c->b - b;
    return (uint32_t)((((512 + rmean) * dr * dr) >> 8) + 4 * dg * dg +
                      (((767 - rmean) * db * db) >> 8));
}

const char* cfs_color_nearest_name(uint8_t r, uint8_t g, uint8_t b) {
    const char* best = cfs_color_table[0].name;
    uint32_t best_d = UINT32_MAX;

    for(size_t i = 0; i < CFS_COLOR_TABLE_MAX; i++) {
        uint32_t d = cfs_color_dist(&cfs_color_table[i], r, g, b);
        if(d < best_d) {
            best_d = d;
            best = cfs_color_table[i].name;
        }
    }
    for(size_t f = 0; f < CFS_SHADE_FAMILY_COUNT && best_d; f++) {
        const CfsShadeFamily* fam = &cfs_shade_families[f];
        for(size_t i = 0; i < fam->count; i++) {
            uint32_t d = cfs_color_dist(&fam->shades[i], r, g, b);
            if(d < best_d) {
                best_d = d;
                best = fam->shades[i].name;
            }
        }
    }
    return best;
}
