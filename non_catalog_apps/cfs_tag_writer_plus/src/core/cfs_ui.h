#pragma once

// Glue between scenes and the app's own screens (src/views/cfs_screen.h):
// filling the spool card from tag data and the shared working / finished
// status screens of the read, write and erase flows.

#include "src/core/cfs_app_i.h"

// CfsScreen callback: forward the event id to the scene manager.
void cfs_ui_screen_callback(void* context, uint32_t event);

// Spool card for a tag record. weight_index picks the weight label shown
// next to the length; buttons are left unset (NULL).
void cfs_ui_fill_card(const CfsTagData* data, size_t weight_index, CfsCardModel* card);

// Product name for a tag record: catalog name, legacy type name, or raw id.
void cfs_ui_material_name(const CfsTagData* data, char* out, size_t out_size);

// "Hold the tag" screen while the NFC worker runs. A NULL color draws an
// empty spool (read, erase); winding makes the spool wind up filament (write)
// instead of just turning.
void cfs_ui_show_working(
    CfsApp* app,
    const char* title,
    const char* text,
    const CfsTagData* color,
    bool winding);

// Finished operation: success buzzes, shows a check and returns to the start
// menu after a moment (CfsCustomEventScreenDone); failure buzzes, shows a
// cross and offers Retry (CfsCustomEventButtonRetry).
void cfs_ui_finish_op(CfsApp* app, bool success, const char* title, const char* text);
