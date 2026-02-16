#include "ir_helper.h"

#include <flipper_format/flipper_format.h>
#include <furi.h>
#include <infrared/worker/infrared_worker.h>
#include <lib/infrared/signal/infrared_error_code.h>
#include <storage/storage.h>

#define IR_FILE_HEADER "IR signals file"
#define IR_FILE_VERSION 1

/* ========== Signal List ========== */

IrSignalList *ir_signal_list_alloc(void) {
  IrSignalList *list = malloc(sizeof(IrSignalList));
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
  return list;
}

void ir_signal_list_free(IrSignalList *list) {
  if (!list)
    return;

  for (size_t i = 0; i < list->count; i++) {
    if (list->items[i].signal) {
      infrared_signal_free(list->items[i].signal);
    }
    if (list->items[i].name) {
      furi_string_free(list->items[i].name);
    }
  }
  if (list->items) {
    free(list->items);
  }
  free(list);
}

static void ir_signal_list_add(IrSignalList *list, InfraredSignal *signal,
                               const char *name) {
  if (list->count >= list->capacity) {
    size_t new_capacity = list->capacity == 0 ? 8 : list->capacity * 2;
    list->items = realloc(list->items, new_capacity * sizeof(IrSignalItem));
    list->capacity = new_capacity;
  }

  list->items[list->count].signal = signal;
  list->items[list->count].name = furi_string_alloc_set(name);
  list->count++;
}

/* ========== File I/O ========== */

bool ir_helper_load_file(const char *path, IrSignalList *list) {
  Storage *storage = furi_record_open(RECORD_STORAGE);
  FlipperFormat *ff = flipper_format_file_alloc(storage);
  bool success = false;

  do {
    if (!flipper_format_file_open_existing(ff, path))
      break;

    /* Verify header */
    FuriString *filetype = furi_string_alloc();
    uint32_t version;
    bool header_ok = flipper_format_read_header(ff, filetype, &version);
    furi_string_free(filetype);
    if (!header_ok)
      break;

    /* Read all signals */
    FuriString *signal_name = furi_string_alloc();
    while (true) {
      InfraredSignal *signal = infrared_signal_alloc();
      InfraredErrorCode err = infrared_signal_read(signal, ff, signal_name);
      if (err == InfraredErrorCodeNone) {
        ir_signal_list_add(list, signal, furi_string_get_cstr(signal_name));
      } else {
        infrared_signal_free(signal);
        break;
      }
    }
    furi_string_free(signal_name);

    success = true;
  } while (false);

  flipper_format_free(ff);
  furi_record_close(RECORD_STORAGE);
  return success;
}

/* ========== Transmit ========== */

void ir_helper_transmit(InfraredSignal *signal) {
  infrared_signal_transmit(signal);
}

/* ========== File Browser ========== */

bool ir_helper_list_files(const char *dir_path, FuriString ***files,
                          size_t *count) {
  Storage *storage = furi_record_open(RECORD_STORAGE);
  File *dir = storage_file_alloc(storage);

  *files = NULL;
  *count = 0;

  if (!storage_dir_open(dir, dir_path)) {
    storage_file_free(dir);
    furi_record_close(RECORD_STORAGE);
    return false;
  }

  /* First pass: count .ir files */
  FileInfo file_info;
  char name_buf[256];
  size_t file_count = 0;

  while (storage_dir_read(dir, &file_info, name_buf, sizeof(name_buf))) {
    if (!(file_info.flags & FSF_DIRECTORY)) {
      size_t len = strlen(name_buf);
      if (len > 3 && strcmp(name_buf + len - 3, ".ir") == 0) {
        file_count++;
      }
    }
  }

  if (file_count == 0) {
    storage_dir_close(dir);
    storage_file_free(dir);
    furi_record_close(RECORD_STORAGE);
    return true; /* Success, but no files */
  }

  /* Allocate array */
  *files = malloc(file_count * sizeof(FuriString *));
  *count = file_count;

  /* Second pass: collect filenames - close and reopen directory */
  storage_dir_close(dir);
  if (!storage_dir_open(dir, dir_path)) {
    free(*files);
    *files = NULL;
    *count = 0;
    storage_file_free(dir);
    furi_record_close(RECORD_STORAGE);
    return false;
  }

  size_t idx = 0;

  while (storage_dir_read(dir, &file_info, name_buf, sizeof(name_buf)) &&
         idx < file_count) {
    if (!(file_info.flags & FSF_DIRECTORY)) {
      size_t len = strlen(name_buf);
      if (len > 3 && strcmp(name_buf + len - 3, ".ir") == 0) {
        (*files)[idx] = furi_string_alloc_set(name_buf);
        idx++;
      }
    }
  }

  storage_dir_close(dir);
  storage_file_free(dir);
  furi_record_close(RECORD_STORAGE);
  return true;
}

void ir_helper_free_file_list(FuriString **files, size_t count) {
  if (!files)
    return;
  for (size_t i = 0; i < count; i++) {
    if (files[i]) {
      furi_string_free(files[i]);
    }
  }
  free(files);
}
