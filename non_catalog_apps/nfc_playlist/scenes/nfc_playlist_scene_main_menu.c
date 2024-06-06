#include "../nfc_playlist.h"

typedef enum {
    NfcPlaylistMenuSelection_Start,
    NfcPlaylistMenuSelection_PlaylistSelect,
    NfcPlaylistMenuSelection_FileEdit,
    NfcPlaylistMenuSelection_Settings
} NfcPlaylistMainMenuMenuSelection;

void nfc_playlist_main_menu_menu_callback(void* context, uint32_t index) {
    NfcPlaylist* nfc_playlist = context;
    scene_manager_handle_custom_event(nfc_playlist->scene_manager, index);
}

void nfc_playlist_main_menu_scene_on_enter(void* context) {
    NfcPlaylist* nfc_playlist = context;
    if(!nfc_playlist->settings.playlist_selected) {
        nfc_playlist->settings.playlist_selected = true;
        scene_manager_next_scene(nfc_playlist->scene_manager, NfcPlaylistScene_PlaylistSelect);
        return;
    }

    FuriString* header = furi_string_alloc_printf("NFC Playlist v%s", FAP_VERSION);
    submenu_set_header(nfc_playlist->submenu, furi_string_get_cstr(header));
    furi_string_free(header);

    submenu_add_lockable_item(
        nfc_playlist->submenu,
        "Start",
        NfcPlaylistMenuSelection_Start,
        nfc_playlist_main_menu_menu_callback,
        nfc_playlist,
        furi_string_empty(nfc_playlist->settings.playlist_path),
        "No\nplaylist\nselected");

    submenu_add_item(
        nfc_playlist->submenu,
        "Select playlist",
        NfcPlaylistMenuSelection_PlaylistSelect,
        nfc_playlist_main_menu_menu_callback,
        nfc_playlist);

    submenu_add_item(
        nfc_playlist->submenu,
        "Edit playlist",
        NfcPlaylistMenuSelection_FileEdit,
        nfc_playlist_main_menu_menu_callback,
        nfc_playlist);

    submenu_add_item(
        nfc_playlist->submenu,
        "Settings",
        NfcPlaylistMenuSelection_Settings,
        nfc_playlist_main_menu_menu_callback,
        nfc_playlist);

    view_dispatcher_switch_to_view(nfc_playlist->view_dispatcher, NfcPlaylistView_Submenu);
}

bool nfc_playlist_main_menu_scene_on_event(void* context, SceneManagerEvent event) {
    NfcPlaylist* nfc_playlist = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case NfcPlaylistMenuSelection_Start:
            scene_manager_next_scene(nfc_playlist->scene_manager, NfcPlaylistScene_Emulation);
            consumed = true;
            break;
        case NfcPlaylistMenuSelection_PlaylistSelect:
            scene_manager_next_scene(nfc_playlist->scene_manager, NfcPlaylistScene_PlaylistSelect);
            consumed = true;
            break;
        case NfcPlaylistMenuSelection_FileEdit:
            scene_manager_next_scene(nfc_playlist->scene_manager, NfcPlaylistScene_PlaylistEdit);
            consumed = true;
            break;
        case NfcPlaylistMenuSelection_Settings:
            scene_manager_next_scene(nfc_playlist->scene_manager, NfcPlaylistScene_Settings);
            consumed = true;
            break;
        default:
            break;
        }
    }
    return consumed;
}

void nfc_playlist_main_menu_scene_on_exit(void* context) {
    NfcPlaylist* nfc_playlist = context;
    submenu_reset(nfc_playlist->submenu);
}