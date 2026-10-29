#pragma once

#include <gui/view.h>

typedef struct CdView CdView;
typedef void (*CdViewButtonCallback)(InputType type, void* context);

CdView* cd_view_alloc(void);

void cd_view_free(CdView* cd_view);

View* cd_view_get_view(CdView* cd_view);

// OK button presses, short to eject or insert, long to force eject
void cd_view_set_button_callback(CdView* cd_view, CdViewButtonCallback cb, void* context);

void cd_view_set_disc(CdView* cd_view, bool loaded, FuriString* name, FuriString* info);

void cd_view_set_stats(CdView* cd_view, uint32_t read);

void cd_view_set_connection_error(CdView* cd_view);
void cd_view_clear_connection_error(CdView* cd_view);
