#pragma once

// Drawing for the app's own screens (menu, color picker, spool card, status).
// Pure canvas code with no app state, so the same file also builds in the
// desktop preview used to design these screens.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <gui/canvas.h>
#include "src/data/cfs_data.h"

typedef struct {
    const Icon* icon;
    const char* label;
} CfsMenuItem;

#define CFS_MENU_ROWS (4)

typedef struct {
    const CfsMenuItem* items;
    size_t count;
    size_t selected;
    size_t window; // first visible item
    uint8_t angle; // logo spool rotation, 0..CFS_SPOOL_STEPS-1
} CfsMenuModel;

void cfs_draw_menu(Canvas* canvas, const CfsMenuModel* m);

typedef enum {
    CfsPickerList, // a family (or the basic palette) to scroll through
    CfsPickerHex, // last page: type a hex value
} CfsPickerKind;

typedef struct {
    CfsPickerKind kind;
    const char* family; // page title
    const CfsColorPreset* shades;
    size_t count;
    size_t selected;
    uint8_t r, g, b; // color shown on the spool (hex page: the pending color)
    uint8_t angle; // spool rotation, 0..CFS_SPOOL_STEPS-1
} CfsPickerModel;

void cfs_draw_color_picker(Canvas* canvas, const CfsPickerModel* m);

typedef struct {
    char title[32]; // material / product name
    char brand[24];
    char color_name[32];
    char weight[24]; // "250 g  82 m"
    char serial[16]; // "SN 070674"
    uint8_t r, g, b;
    const char* button_left; // NULL = no button
    const char* button_center;
    const char* button_right;
} CfsCardModel;

void cfs_draw_spool_card(Canvas* canvas, const CfsCardModel* m);

typedef enum {
    CfsStatusWorking, // waiting for a tag, waves animate
    CfsStatusOk,
    CfsStatusFail,
} CfsStatusKind;

typedef struct {
    CfsStatusKind kind;
    char title[24];
    char text[64]; // up to 3 lines, '\n' separated
    uint16_t frame; // animation frame while working
    bool winding; // working spool winds up filament (write) instead of just turning
    bool has_color; // draw the spool in this color (else an outline spool)
    uint8_t r, g, b;
    const char* button_center; // NULL = none
} CfsStatusModel;

void cfs_draw_status(Canvas* canvas, const CfsStatusModel* m);

// Building blocks, shared by the screens above.
#define CFS_SPOOL_STEPS (12)
void cfs_draw_spool_anim(
    Canvas* canvas,
    int32_t cx,
    int32_t cy,
    int32_t radius,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t angle,
    uint8_t fill);
void cfs_draw_spool(
    Canvas* canvas,
    int32_t cx,
    int32_t cy,
    int32_t radius,
    uint8_t r,
    uint8_t g,
    uint8_t b);
void cfs_draw_str_fit(Canvas* canvas, int32_t x, int32_t y, int32_t max_width, const char* str);
