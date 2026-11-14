#pragma once

// One custom view for the app's own screens: the main menu, the color
// picker, the spool card and the status screen (working / done / failed).
// Each show_* call switches what it draws; input is turned into the event
// ids the scene passed in and handed to the callback.

#include <gui/view.h>
#include "src/views/cfs_draw.h"
#include "src/data/cfs_shades.h"

typedef struct CfsScreen CfsScreen;

// Called on the GUI thread (input) or a timer thread (timeout); scenes send
// the id on with view_dispatcher_send_custom_event.
typedef void (*CfsScreenCallback)(void* context, uint32_t event);

CfsScreen* cfs_screen_alloc(void);
void cfs_screen_free(CfsScreen* screen);
View* cfs_screen_get_view(CfsScreen* screen);
void cfs_screen_set_callback(CfsScreen* screen, CfsScreenCallback callback, void* context);

// Stop timers and clear the screen; call from scene on_exit.
void cfs_screen_reset(CfsScreen* screen);

// Menu: OK sends item_events[selected].
void cfs_screen_show_menu(
    CfsScreen* screen,
    const CfsMenuItem* items,
    const uint32_t* item_events,
    size_t count,
    size_t selected);
size_t cfs_screen_get_menu_selected(CfsScreen* screen);

// Color picker pages: 0..CFS_SHADE_FAMILY_COUNT-1 are the shade families,
// then the basic palette (basic_count colors), then the hex page.
#define CFS_PICKER_PAGE_BASIC (CFS_SHADE_FAMILY_COUNT)
#define CFS_PICKER_PAGE_HEX   (CFS_SHADE_FAMILY_COUNT + 1)
#define CFS_PICKER_PAGE_COUNT (CFS_SHADE_FAMILY_COUNT + 2)

// OK sends pick_event on a list page, hex_event on the hex page.
void cfs_screen_show_picker(
    CfsScreen* screen,
    size_t page,
    size_t selected,
    size_t basic_count,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint32_t pick_event,
    uint32_t hex_event);
void cfs_screen_get_picker(CfsScreen* screen, size_t* page, size_t* selected);
// The color under the cursor (shade, basic color, or the hex page's color).
const CfsColorPreset* cfs_picker_color_at(size_t page, size_t selected, size_t basic_count);

// Spool card: Left / OK / Right send the matching event (buttons with a NULL
// label in the model are ignored).
void cfs_screen_show_card(
    CfsScreen* screen,
    const CfsCardModel* card,
    uint32_t left_event,
    uint32_t center_event,
    uint32_t right_event);

// Status: OK sends ok_event when the model has a center button. Working
// screens animate. timeout_ms > 0 sends timeout_event once after that long.
void cfs_screen_show_status(
    CfsScreen* screen,
    const CfsStatusModel* status,
    uint32_t ok_event,
    uint32_t timeout_ms,
    uint32_t timeout_event);
