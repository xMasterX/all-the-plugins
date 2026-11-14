#pragma once

#include <furi.h>
#include <storage/storage.h>

// Number of `sub:` entries in a playlist, or -1 if the file can't be opened
int scheduler_playlist_count(Storage* storage, const char* playlist_path);
