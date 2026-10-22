#include "scheduler_playlist.h"

#include <flipper_format/flipper_format.h>

int scheduler_playlist_count(Storage* storage, const char* playlist_path) {
    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* data = furi_string_alloc();
    int count = -1;

    if(flipper_format_file_open_existing(format, playlist_path)) {
        count = 0;
        while(flipper_format_read_string(format, "sub", data)) {
            ++count;
        }
    }

    flipper_format_file_close(format);
    flipper_format_free(format);
    furi_string_free(data);
    return count;
}
