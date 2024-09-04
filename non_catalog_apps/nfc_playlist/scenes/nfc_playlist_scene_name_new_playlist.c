#include "../nfc_playlist.h"

int32_t nfc_playlist_name_new_playlist_thread_task(void* context) {
    NfcPlaylist* nfc_playlist = context;

    FuriString* file_name =
        furi_string_alloc_printf("%s%s.txt", PLAYLIST_LOCATION, nfc_playlist->text_input_output);
    char const* file_name_cstr = furi_string_get_cstr(file_name);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    if(!storage_file_exists(storage, file_name_cstr)) {
        if(storage_file_open(file, file_name_cstr, FSAM_READ_WRITE, FSOM_CREATE_NEW)) {
            storage_file_close(file);
            furi_string_swap(nfc_playlist->settings.playlist_path, file_name);
            nfc_playlist->settings.playlist_length = 0;
        }
    }

    furi_string_free(file_name);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    return 0;
}

void nfc_playlist_name_new_playlist_thread_state_callback(FuriThreadState state, void* context) {
    NfcPlaylist* nfc_playlist = context;
    if(state == FuriThreadStateStopped) {
        furi_thread_yield();
        nfc_playlist->thread = NULL;
        scene_manager_search_and_switch_to_previous_scene(
            nfc_playlist->scene_manager, NfcPlaylistScene_MainMenu);
    }
}

void nfc_playlist_name_new_playlist_menu_callback(void* context) {
    NfcPlaylist* nfc_playlist = context;
    nfc_playlist->thread = furi_thread_alloc_ex(
        "NfcPlaylistCreator", 1024, nfc_playlist_name_new_playlist_thread_task, nfc_playlist);
    furi_thread_set_state_context(nfc_playlist->thread, nfc_playlist);
    furi_thread_set_state_callback(
        nfc_playlist->thread, nfc_playlist_name_new_playlist_thread_state_callback);
    furi_thread_start(nfc_playlist->thread);
}

void nfc_playlist_name_new_playlist_scene_on_enter(void* context) {
    NfcPlaylist* nfc_playlist = context;

    nfc_playlist->text_input_output = malloc(MAX_PLAYLIST_NAME_LEN + 1);
    text_input_set_header_text(nfc_playlist->text_input, "Enter file name");
    text_input_set_minimum_length(nfc_playlist->text_input, 1);
    text_input_set_result_callback(
        nfc_playlist->text_input,
        nfc_playlist_name_new_playlist_menu_callback,
        nfc_playlist,
        nfc_playlist->text_input_output,
        MAX_PLAYLIST_NAME_LEN,
        true);

    view_dispatcher_switch_to_view(nfc_playlist->view_dispatcher, NfcPlaylistView_TextInput);
}

bool nfc_playlist_name_new_playlist_scene_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void nfc_playlist_name_new_playlist_scene_on_exit(void* context) {
    NfcPlaylist* nfc_playlist = context;
    free(nfc_playlist->text_input_output);
    text_input_reset(nfc_playlist->text_input);
}
