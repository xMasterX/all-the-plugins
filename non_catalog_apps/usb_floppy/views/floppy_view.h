#pragma once

#include <gui/view.h>

typedef struct FloppyView FloppyView;

FloppyView* floppy_view_alloc(void);

void floppy_view_free(FloppyView* floppy_view);

View* floppy_view_get_view(FloppyView* floppy_view);

void floppy_view_set_file_name(FloppyView* floppy_view, FuriString* name);

void floppy_view_set_info(FloppyView* floppy_view, FuriString* info);

void floppy_view_set_stats(FloppyView* floppy_view, uint32_t read, uint32_t written);

void floppy_view_set_connection_error(FloppyView* floppy_view);
void floppy_view_clear_connection_error(FloppyView* floppy_view);
