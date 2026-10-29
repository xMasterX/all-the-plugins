#include "src/views/cfs_draw.h"

#include <stdio.h>
#include <string.h>

#ifdef CFS_SIM
#include "sim.h"
#else
#include <gui/elements.h>
#include <cfs_tag_writer_plus_icons.h>
#endif

// ---- helpers ----

// Ordered (Bayer 4x4) dither: a monochrome screen can't show a color, but it
// can show how light or dark it is, which is what tells spools apart.
static const uint8_t cfs_bayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

static uint8_t cfs_luma(uint8_t r, uint8_t g, uint8_t b) {
    return (uint8_t)(((uint16_t)r * 77 + (uint16_t)g * 150 + (uint16_t)b * 29) >> 8);
}

static bool cfs_dither_dark(int32_t x, int32_t y, uint8_t luma) {
    // 17 levels: luma 255 -> no dark pixels, luma 0 -> all dark.
    return (uint16_t)cfs_bayer4[y & 3][x & 3] * 16 + 8 > luma;
}

void cfs_draw_str_fit(Canvas* canvas, int32_t x, int32_t y, int32_t max_width, const char* str) {
    if(!str) return;
    if((int32_t)canvas_string_width(canvas, str) <= max_width) {
        canvas_draw_str(canvas, x, y, str);
        return;
    }
    char buf[40];
    size_t len = strlen(str);
    if(len > sizeof(buf) - 3) len = sizeof(buf) - 3;
    while(len > 0) {
        memcpy(buf, str, len);
        // drop a trailing space before the dots
        size_t cut = len;
        while(cut > 0 && buf[cut - 1] == ' ')
            cut--;
        memcpy(buf + cut, "..", 3);
        if((int32_t)canvas_string_width(canvas, buf) <= max_width) break;
        len--;
    }
    canvas_draw_str(canvas, x, y, buf);
}

// cos() of the 12 spoke angles (30 degree steps), scaled by 64.
static const int8_t cfs_cos12[CFS_SPOOL_STEPS] =
    {64, 55, 32, 0, -32, -55, -64, -55, -32, 0, 32, 55};
#define CFS_SIN12(i) (cfs_cos12[((i) + 9) % CFS_SPOOL_STEPS])

// A filament spool seen from the side: flange, filament wound in the spool's
// color (as dither), hub with three spokes. angle turns the spokes (one step =
// 30 degrees); fill below 255 shows a partly wound spool with the strand
// coming in from the right.
void cfs_draw_spool_anim(
    Canvas* canvas,
    int32_t cx,
    int32_t cy,
    int32_t radius,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t angle,
    uint8_t fill) {
    const uint8_t luma = cfs_luma(r, g, b);
    const int32_t wound_full = radius - 2;
    const int32_t wound_in = radius * 9 / 20;
    const int32_t wound_out = wound_in - 1 + ((wound_full - (wound_in - 1)) * fill) / 255;

    canvas_set_color(canvas, ColorBlack);
    for(int32_t dy = -wound_out; dy <= wound_out; dy++) {
        for(int32_t dx = -wound_out; dx <= wound_out; dx++) {
            const int32_t d2 = dx * dx + dy * dy;
            if(d2 > wound_out * wound_out || d2 < (wound_in - 1) * (wound_in - 1)) continue;
            if(cfs_dither_dark(cx + dx, cy + dy, luma)) canvas_draw_dot(canvas, cx + dx, cy + dy);
        }
    }
    canvas_draw_circle(canvas, cx, cy, radius);
    canvas_draw_circle(canvas, cx, cy, wound_in - 1);
    if(fill < 255) {
        // outer edge of the filament so far, and the strand being pulled in
        canvas_draw_circle(canvas, cx, cy, wound_out);
        canvas_draw_line(canvas, cx, cy - wound_out, cx + radius + 3, cy - wound_out);
    }
    const int32_t spoke = wound_in - 2;
    for(uint8_t k = 0; k < 3; k++) {
        const uint8_t i = (angle + k * 4) % CFS_SPOOL_STEPS;
        canvas_draw_line(
            canvas, cx, cy, cx + (cfs_cos12[i] * spoke) / 64, cy + (CFS_SIN12(i) * spoke) / 64);
    }
    canvas_draw_disc(canvas, cx, cy, 1);
}

void cfs_draw_spool(
    Canvas* canvas,
    int32_t cx,
    int32_t cy,
    int32_t radius,
    uint8_t r,
    uint8_t g,
    uint8_t b) {
    cfs_draw_spool_anim(canvas, cx, cy, radius, r, g, b, 0, 255);
}

static void
    cfs_draw_buttons(Canvas* canvas, const char* left, const char* center, const char* right) {
    canvas_set_font(canvas, FontSecondary);
    canvas_set_color(canvas, ColorBlack);
    if(left) elements_button_left(canvas, left);
    if(center) elements_button_center(canvas, center);
    if(right) elements_button_right(canvas, right);
}

// ---- main menu: spool logo on the left, list with icons on the right ----

void cfs_draw_menu(Canvas* canvas, const CfsMenuModel* m) {
    const int32_t item_h = 16;
    const int32_t x = 47;
    const int32_t w = 76;

    canvas_set_color(canvas, ColorBlack);
    cfs_draw_spool_anim(canvas, 21, 21, 18, 0x60, 0x60, 0x60, m->angle, 255);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 21, 42, AlignCenter, AlignTop, "CFS");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 21, 52, AlignCenter, AlignTop, "Tag Writer");

    for(size_t i = 0; i < CFS_MENU_ROWS && m->window + i < m->count; i++) {
        const size_t idx = m->window + i;
        const int32_t y = (int32_t)i * item_h;
        canvas_set_color(canvas, ColorBlack);
        if(idx == m->selected) {
            elements_slightly_rounded_box(canvas, x, y + 1, w, item_h - 2);
            canvas_set_color(canvas, ColorWhite);
        }
        if(m->items[idx].icon) canvas_draw_icon(canvas, x + 3, y + 3, m->items[idx].icon);
        cfs_draw_str_fit(canvas, x + 17, y + 12, w - 19, m->items[idx].label);
    }
    canvas_set_color(canvas, ColorBlack);
    elements_scrollbar_pos(canvas, 128, 0, 64, m->selected, m->count);
}

// ---- color picker: one screen, families left/right, shades up/down ----

void cfs_draw_color_picker(Canvas* canvas, const CfsPickerModel* m) {
    canvas_set_color(canvas, ColorBlack);

    // header: < Family >
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 1, AlignCenter, AlignTop, m->family);
    canvas_draw_triangle(canvas, 4, 5, 7, 4, CanvasDirectionRightToLeft);
    canvas_draw_triangle(canvas, 123, 5, 7, 4, CanvasDirectionLeftToRight);

    cfs_draw_spool_anim(canvas, 19, 32, 17, m->r, m->g, m->b, m->angle, 255);

    canvas_set_font(canvas, FontSecondary);
    if(m->kind == CfsPickerHex) {
        char hex[8];
        snprintf(hex, sizeof(hex), "#%02X%02X%02X", m->r, m->g, m->b);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 44, 30, hex);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 44, 42, "Type any color");
        cfs_draw_buttons(canvas, NULL, "Edit", NULL);
        return;
    }

    // three rows: previous, current (inverted), next
    const int32_t x = 40;
    const int32_t w = 84;
    const int32_t row_h = 12;
    for(int32_t k = -1; k <= 1; k++) {
        const int32_t idx = (int32_t)m->selected + k;
        if(idx < 0 || idx >= (int32_t)m->count) continue;
        const int32_t y = 14 + (k + 1) * row_h;
        canvas_set_color(canvas, ColorBlack);
        if(k == 0) {
            elements_slightly_rounded_box(canvas, x, y, w, row_h);
            canvas_set_color(canvas, ColorWhite);
        }
        cfs_draw_str_fit(canvas, x + 4, y + 9, w - 7, m->shades[idx].name);
    }
    canvas_set_color(canvas, ColorBlack);
    elements_scrollbar_pos(canvas, 128, 14, 36, m->selected, m->count);

    char pos[12];
    snprintf(pos, sizeof(pos), "%u/%u", (unsigned)(m->selected + 1), (unsigned)m->count);
    canvas_draw_str(canvas, 2, 62, pos);
    cfs_draw_buttons(canvas, NULL, "Pick", NULL);
}

// ---- spool card: confirm before writing, and read results ----

void cfs_draw_spool_card(Canvas* canvas, const CfsCardModel* m) {
    canvas_set_color(canvas, ColorBlack);
    cfs_draw_spool(canvas, 19, 26, 18, m->r, m->g, m->b);

    const int32_t x = 42;
    const int32_t w = 128 - x - 1;
    canvas_set_font(canvas, FontPrimary);
    cfs_draw_str_fit(canvas, x, 9, w, m->title);
    canvas_set_font(canvas, FontSecondary);
    cfs_draw_str_fit(canvas, x, 20, w, m->brand);
    cfs_draw_str_fit(canvas, x, 30, w, m->color_name);
    cfs_draw_str_fit(canvas, x, 40, w, m->weight);
    cfs_draw_str_fit(canvas, x, 50, w, m->serial);

    cfs_draw_buttons(canvas, m->button_left, m->button_center, m->button_right);
}

// ---- status: writing / done / failed ----

static void
    cfs_draw_text_lines(Canvas* canvas, int32_t x, int32_t y, int32_t w, const char* text) {
    char line[32];
    while(text && *text) {
        const char* nl = strchr(text, '\n');
        size_t len = nl ? (size_t)(nl - text) : strlen(text);
        if(len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, text, len);
        line[len] = '\0';
        cfs_draw_str_fit(canvas, x, y, w, line);
        y += 10;
        text = nl ? nl + 1 : NULL;
    }
}

void cfs_draw_status(Canvas* canvas, const CfsStatusModel* m) {
    canvas_set_color(canvas, ColorBlack);
    const int32_t cx = 19, cy = 28, rad = 17;

    if(m->kind == CfsStatusWorking) {
        // one turn step every 2 frames
        const uint8_t angle = (m->frame / 2) % CFS_SPOOL_STEPS;
        uint8_t fill = 255;
        if(m->winding) {
            // wind up over 36 frames, stay full for 8, start over
            const uint8_t phase = m->frame % 44;
            fill = phase < 36 ? (uint8_t)(phase * 255 / 35) : 255;
        }
        if(m->has_color) {
            cfs_draw_spool_anim(canvas, cx, cy, rad, m->r, m->g, m->b, angle, fill);
        } else {
            cfs_draw_spool_anim(canvas, cx, cy, rad, 0xFF, 0xFF, 0xFF, angle, fill);
        }
        // waves grow over three steps, then pause for one (a step is 3 frames)
        static const Icon* const waves[] = {&I_Waves1_9x15, &I_Waves2_9x15, &I_Waves3_9x15};
        const uint8_t w = (m->frame / 3) % 4;
        if(w < 3) canvas_draw_icon(canvas, 39, cy - 7, waves[w]);
    } else {
        canvas_draw_disc(canvas, cx, cy, rad);
        canvas_set_color(canvas, ColorWhite);
        if(m->kind == CfsStatusOk) {
            // a 3 px check mark
            for(int32_t t = -1; t <= 1; t++) {
                canvas_draw_line(canvas, cx - 9, cy + t, cx - 3, cy + 6 + t);
                canvas_draw_line(canvas, cx - 3, cy + 6 + t, cx + 9, cy - 6 + t);
            }
        } else {
            for(int32_t t = -1; t <= 1; t++) {
                canvas_draw_line(canvas, cx - 7 + t, cy - 7, cx + 7 + t, cy + 7);
                canvas_draw_line(canvas, cx + 7 + t, cy - 7, cx - 7 + t, cy + 7);
            }
        }
        canvas_set_color(canvas, ColorBlack);
    }

    const int32_t x = m->kind == CfsStatusWorking ? 52 : 42;
    const int32_t w = 128 - x - 1;
    canvas_set_font(canvas, FontPrimary);
    cfs_draw_str_fit(canvas, x, 16, w, m->title);
    canvas_set_font(canvas, FontSecondary);
    cfs_draw_text_lines(canvas, x, 28, w, m->text);

    if(m->button_center) cfs_draw_buttons(canvas, NULL, m->button_center, NULL);
}
