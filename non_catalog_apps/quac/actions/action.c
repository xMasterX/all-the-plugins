
#include "quac.h"
#include "item.h"
#include "action_i.h"

#define MAX_FILE_LEN (size_t)256

void action_ql_resolve(
    void* context,
    const FuriString* action_path,
    FuriString* new_path,
    FuriString* error) {
    UNUSED(error);
    App* app = (App*)context;
    File* file_link = storage_file_alloc(app->storage);
    do {
        if(!storage_file_open(
               file_link, furi_string_get_cstr(action_path), FSAM_READ, FSOM_OPEN_EXISTING)) {
            ACTION_SET_ERROR(
                "Quac Link: Failed to open file %s", furi_string_get_cstr(action_path));
            break;
        }
        char buffer[MAX_FILE_LEN]; // long enough?
        size_t bytes_read = storage_file_read(file_link, buffer, MAX_FILE_LEN);
        if(bytes_read == 0) {
            ACTION_SET_ERROR(
                "Quac Link: Error reading link file %s", furi_string_get_cstr(action_path));
            break;
        }
        if(!storage_file_exists(app->storage, buffer)) {
            ACTION_SET_ERROR("Quac Link: Linked file does not exist! %s", buffer);
            break;
        }
        furi_string_set_strn(new_path, buffer, bytes_read);
    } while(false);
    storage_file_close(file_link);
    storage_file_free(file_link);
}

void action_tx(void* context, Item* item, FuriString* error) {
    // FURI_LOG_I(TAG, "action_run: %s : %s", furi_string_get_cstr(item->name), item->ext);

    FuriString* path = furi_string_alloc_set(item->path);
    if(item->is_link) {
        // This is a Quac link, open the file and pull the real filename
        action_ql_resolve(context, item->path, path, error);
        if(furi_string_size(error)) {
            furi_string_free(path);
            return;
        }
        FURI_LOG_I(TAG, "Resolved Quac link file to: %s", furi_string_get_cstr(path));
    }

    if(!strcmp(item->ext, ".sub")) {
        action_subghz_tx(context, path, error);
    } else if(!strcmp(item->ext, ".ir")) {
        action_ir_tx(context, path, error);
    } else if(!strcmp(item->ext, ".rfid")) {
        action_rfid_tx(context, path, error);
    } else if(!strcmp(item->ext, ".nfc")) {
        action_nfc_tx(context, path, error);
    } else if(!strcmp(item->ext, ".ibtn")) {
        action_ibutton_tx(context, path, error);
    } else if(!strcmp(item->ext, ".qpl")) {
        action_qpl_tx(context, path, error);
    } else {
        FURI_LOG_E(TAG, "Unknown item file type! %s", item->ext);
    }
    furi_string_free(path);
}
