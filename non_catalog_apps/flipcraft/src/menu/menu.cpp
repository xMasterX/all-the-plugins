// Copyright (c) 2026 ApertureFox Technology. MIT License.
#include "menu.h"
#include "../plugin_api.h"

#include <furi.h>
#include <furi_hal_random.h>
#include <flipper_application/flipper_application.h>
#include <gui/canvas.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/text_input.h>
#include <gui/modules/text_box.h>
#include <gui/modules/dialog_ex.h>

#include <new>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace flipcraft {
namespace menu {

namespace {

constexpr const char* DATA_DIR = STORAGE_APP_DATA_PATH_PREFIX; // "/data"
constexpr const char* ASSETS_DIR = STORAGE_APP_ASSETS_PATH_PREFIX; // "/assets"
constexpr const char* ABOUT_PATH = APP_ASSETS_PATH("about.txt");

constexpr int MAX_ITEMS = 32;
constexpr int NAME_LEN = 64;

// Menu tree:
//   Main: Worlds / About
//   Worlds: Create / Templates (only when assets ship any) / <saves>
//   Create: one VariableItemList holding the whole creation form
//   <save>: Play / Info / Settings / Rename / Delete
// Settings edits the flags byte of an existing save in place; Create hands
// them to the generator, which stamps them into the fresh header.

constexpr uint32_t IDX_WORLDS = 0;
constexpr uint32_t IDX_ABOUT = 1;
constexpr uint32_t IDX_CREATE = 0xF000;
constexpr uint32_t IDX_TEMPLATES = 0xE000;

constexpr uint32_t ACT_PLAY = 0, ACT_INFO = 1, ACT_SETTINGS = 2, ACT_RENAME = 3, ACT_DELETE = 4;

constexpr uint8_t SIZE_CHUNKS[4] = {16, 32, 64, 128};
static const char* const SIZE_LABELS[4] = {"128x128", "256x256", "512x512", "1024x1024"};

static const char* const MODE_LABELS[3] = {"Survival", "Hardmode", "Creative"};
// Index == FlipcraftWorldType, stored in header byte 37.
static const char* const TYPE_LABELS[FlipcraftWorldCount] =
    {"Normal", "Flat", "Superflat", "Woods"};
static const char* const ON_OFF[2] = {"Off", "On"};
static const char* const DIST_LABELS[2] = {"Near", "Far"};

enum ViewId : uint32_t {
    VIEW_MAIN = 0,
    VIEW_LIST, // worlds / templates / world actions
    VIEW_FORM, // creation form and per-world settings
    VIEW_TEXT, // world name or seed keyboard
    VIEW_ABOUT, // scrollable text: about.txt or world info
    VIEW_CONFIRM, // delete dialog
};

enum ListMode {
    LIST_WORLDS,
    LIST_TEMPLATES,
    LIST_ACTIONS
};
enum FormMode {
    FORM_CREATE,
    FORM_SETTINGS
};
enum TextMode {
    TEXT_NAME,
    TEXT_SEED,
    TEXT_TPL_NAME,
    TEXT_RENAME
};

// Row order of the two forms. FORM_SETTINGS reuses the same widget with a
// shorter list, so the indices are kept apart.
enum CreateRow : uint32_t {
    ROW_NAME = 0,
    ROW_SEED,
    ROW_MODE,
    ROW_TYPE,
    ROW_MOBS,
    ROW_DIST,
    ROW_SIZE,
    ROW_CREATE,
    ROW_EXIT,
    ROW_CREATE_COUNT
};
enum SettingsRow : uint32_t {
    SROW_MODE = 0,
    SROW_TYPE,
    SROW_MOBS,
    SROW_DIST,
    SROW_SAVE,
    SROW_EXIT,
    SROW_COUNT
};

struct MenuApp {
    Gui* gui = nullptr;
    Storage* storage = nullptr;

    ViewDispatcher* vd = nullptr;
    Submenu* main_menu = nullptr;
    Submenu* list = nullptr;
    VariableItemList* form = nullptr;
    TextInput* text_input = nullptr;
    TextBox* text_box = nullptr;
    DialogEx* dialog = nullptr;

    uint32_t current = VIEW_MAIN;
    ListMode list_mode = LIST_WORLDS;
    FormMode form_mode = FORM_CREATE;
    TextMode text_mode = TEXT_NAME;

    // Rows the keyboard comes back to; only the creation form has them.
    VariableItem* it_name = nullptr;
    VariableItem* it_seed = nullptr;
    bool about_from_actions = false; // Back target for the text box

    Action result_action = Action::Quit;
    char result_path[256] = {0};

    // Live state of the creation form.
    char name_buf[NAME_LEN] = {0};
    char seed_text[16] = {0};
    uint32_t seed = 0;
    uint8_t mode_idx = 0, type_idx = 0, mobs_idx = 0, dist_idx = 1, size_idx = 0;

    char text_buf[NAME_LEN] = {0};
    char chosen_template[256] = {0};
    char sel_name[NAME_LEN] = {0}; // save selected in the Worlds list
    char info_text[512] = {0}; // shared text box content (info / about)

    char saves[MAX_ITEMS][NAME_LEN] = {{0}};
    int saves_count = 0;
    char templates[MAX_ITEMS][NAME_LEN] = {{0}};
    int templates_count = 0;
};

void main_callback(void* context, uint32_t index);
void list_callback(void* context, uint32_t index);
void open_worlds(MenuApp* app);
void open_actions(MenuApp* app);
void open_form(MenuApp* app, FormMode mode);
void text_callback(void* context);

void join_path(char* dst, size_t size, const char* dir, const char* name) {
    snprintf(dst, size, "%s/%s", dir, name);
}

void strip_ext(char* dst, size_t size, const char* name) {
    snprintf(dst, size, "%s", name);
    size_t len = strlen(dst);
    if(len > 4 && strcmp(dst + len - 4, ".fcw") == 0) dst[len - 4] = '\0';
}

int scan_dir(Storage* storage, const char* dir, char names[][NAME_LEN], int max) {
    File* f = storage_file_alloc(storage);
    int n = 0;
    if(storage_dir_open(f, dir)) {
        FileInfo info;
        char nm[NAME_LEN];
        while(n < max && storage_dir_read(f, &info, nm, sizeof(nm))) {
            if(file_info_is_dir(&info)) continue;
            size_t len = strlen(nm);
            if(len > 4 && strcmp(nm + len - 4, ".fcw") == 0) {
                strncpy(names[n], nm, NAME_LEN - 1);
                names[n][NAME_LEN - 1] = '\0';
                n++;
            }
        }
    }
    storage_dir_close(f);
    storage_file_free(f);
    return n;
}

bool copy_file(Storage* storage, const char* src, const char* dst) {
    File* in = storage_file_alloc(storage);
    File* out = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(in, src, FSAM_READ, FSOM_OPEN_EXISTING) &&
       storage_file_open(out, dst, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = true;
        uint8_t buf[512];
        while(true) {
            size_t r = storage_file_read(in, buf, sizeof(buf));
            if(r > 0 && storage_file_write(out, buf, r) != r) {
                ok = false;
                break;
            }
            if(r < sizeof(buf)) break; // short read -> EOF
        }
    }
    storage_file_close(in);
    storage_file_close(out);
    storage_file_free(in);
    storage_file_free(out);
    return ok;
}

// Reads the 64-byte header of a save, checking the magic. Callers only ever
// want the flags byte and the size fields, so one helper serves both.
bool read_header(Storage* storage, const char* name, uint8_t hdr[64], uint32_t* bytes) {
    char path[256];
    join_path(path, sizeof(path), DATA_DIR, name);
    File* f = storage_file_alloc(storage);
    bool ok = storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
              storage_file_read(f, hdr, 64) == 64;
    if(ok && bytes) *bytes = storage_file_size(f);
    storage_file_close(f);
    storage_file_free(f);
    if(!ok) return false;
    uint32_t magic = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) | ((uint32_t)hdr[2] << 16) |
                     ((uint32_t)hdr[3] << 24);
    return magic == 0x31574346; // 'FCW1'
}

bool write_world_flags(Storage* storage, const char* name, uint8_t flags) {
    char path[256];
    join_path(path, sizeof(path), DATA_DIR, name);
    File* f = storage_file_alloc(storage);
    bool ok = storage_file_open(f, path, FSAM_READ_WRITE, FSOM_OPEN_EXISTING) &&
              storage_file_seek(f, FLIPCRAFT_HDR_FLAGS_OFFSET, true) &&
              storage_file_write(f, &flags, 1) == 1;
    storage_file_close(f);
    storage_file_free(f);
    return ok;
}

// A seed is whatever the player typed: a decimal number that fits u32 is used
// as-is, anything else is FNV-1a hashed (so word seeds work).
uint32_t parse_seed(const char* s) {
    size_t len = strlen(s);
    bool digits = len > 0 && len <= 10;
    for(size_t i = 0; i < len && digits; i++)
        digits = s[i] >= '0' && s[i] <= '9';
    if(digits) {
        unsigned long long v = strtoull(s, nullptr, 10);
        if(v <= 0xFFFFFFFFull) return (uint32_t)v;
    }
    uint32_t h = 2166136261u;
    for(size_t i = 0; i < len; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

void switch_view(MenuApp* app, uint32_t id) {
    app->current = id;
    view_dispatcher_switch_to_view(app->vd, id);
}

void open_text(MenuApp* app, TextMode mode, const char* header, void (*cb)(void*)) {
    app->text_mode = mode;
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, header);
    text_input_set_minimum_length(app->text_input, 1);
    text_input_set_result_callback(
        app->text_input, cb, app, app->text_buf, sizeof(app->text_buf), true);
    switch_view(app, VIEW_TEXT);
}

void open_text_box(MenuApp* app, bool from_actions) {
    app->about_from_actions = from_actions;
    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_text(app->text_box, app->info_text);
    switch_view(app, VIEW_ABOUT);
}

void open_main(MenuApp* app) {
    Submenu* m = app->main_menu;
    submenu_reset(m);
    submenu_set_header(m, "Flipcraft");
    submenu_add_item(m, "Worlds", IDX_WORLDS, main_callback, app);
    submenu_add_item(m, "About", IDX_ABOUT, main_callback, app);
    switch_view(app, VIEW_MAIN);
}

void open_worlds(MenuApp* app) {
    app->saves_count = scan_dir(app->storage, DATA_DIR, app->saves, MAX_ITEMS);
    app->templates_count = scan_dir(app->storage, ASSETS_DIR, app->templates, MAX_ITEMS);
    app->list_mode = LIST_WORLDS;

    Submenu* m = app->list;
    submenu_reset(m);
    submenu_set_header(m, "Worlds");
    submenu_add_item(m, "Create", IDX_CREATE, list_callback, app);
    if(app->templates_count > 0)
        submenu_add_item(m, "Templates", IDX_TEMPLATES, list_callback, app);
    for(int i = 0; i < app->saves_count; i++)
        submenu_add_item(m, app->saves[i], (uint32_t)i, list_callback, app);
    switch_view(app, VIEW_LIST);
}

void open_templates(MenuApp* app) {
    app->list_mode = LIST_TEMPLATES;

    Submenu* m = app->list;
    submenu_reset(m);
    submenu_set_header(m, "Templates");
    for(int i = 0; i < app->templates_count; i++)
        submenu_add_item(m, app->templates[i], (uint32_t)i, list_callback, app);
    switch_view(app, VIEW_LIST);
}

void open_actions(MenuApp* app) {
    app->list_mode = LIST_ACTIONS;

    Submenu* m = app->list;
    submenu_reset(m);
    submenu_set_header(m, app->sel_name);
    submenu_add_item(m, "Play", ACT_PLAY, list_callback, app);
    submenu_add_item(m, "Info", ACT_INFO, list_callback, app);
    submenu_add_item(m, "Settings", ACT_SETTINGS, list_callback, app);
    submenu_add_item(m, "Rename", ACT_RENAME, list_callback, app);
    submenu_add_item(m, "Delete", ACT_DELETE, list_callback, app);
    switch_view(app, VIEW_LIST);
}

void open_about(MenuApp* app) {
    File* f = storage_file_alloc(app->storage);
    size_t n = 0;
    if(storage_file_open(f, ABOUT_PATH, FSAM_READ, FSOM_OPEN_EXISTING))
        n = storage_file_read(f, app->info_text, sizeof(app->info_text) - 1);
    storage_file_close(f);
    storage_file_free(f);
    if(n == 0) snprintf(app->info_text, sizeof(app->info_text), "about.txt missing");
    app->info_text[n ? n : strlen(app->info_text)] = '\0';
    open_text_box(app, false);
}

void open_info(MenuApp* app) {
    uint8_t hdr[64];
    uint32_t bytes = 0;
    bool ok = read_header(app->storage, app->sel_name, hdr, &bytes);

    auto u16 = [&](int o) { return (uint32_t)hdr[o] | ((uint32_t)hdr[o + 1] << 8); };
    auto u32 = [&](int o) { return u16(o) | (u16(o + 2) << 16); };

    if(ok) {
        const uint8_t flags = hdr[FLIPCRAFT_HDR_FLAGS_OFFSET];
        uint8_t mode = (uint8_t)(flags & FlipcraftFlagModeMask);
        if(mode > FlipcraftModeCreative) mode = FlipcraftModeSurvival;
        uint8_t type = hdr[FLIPCRAFT_HDR_TYPE_OFFSET];
        if(type >= FlipcraftWorldCount) type = FlipcraftWorldNormal;
        uint32_t cx = u16(6), cz = u16(8);
        snprintf(
            app->info_text,
            sizeof(app->info_text),
            "%s\n"
            "World: %lu x %lu blocks\n"
            "Mode: %s\n"
            "Terrain: %s\n"
            "Mobs: %s\n"
            "Draw: %s\n"
            "Format: v%lu, %lu KB\n"
            "Seed/rng: %lu\n"
            "Player: %ld, %ld, %ld",
            app->sel_name,
            (unsigned long)(cx * 8),
            (unsigned long)(cz * 8),
            MODE_LABELS[mode],
            TYPE_LABELS[type],
            ON_OFF[(flags & FlipcraftFlagMobsOff) ? 0 : 1],
            DIST_LABELS[(flags & FlipcraftFlagNearOnly) ? 0 : 1],
            (unsigned long)u16(4),
            (unsigned long)(bytes / 1024),
            (unsigned long)u32(32),
            (long)((int32_t)u32(18) / 16),
            (long)((int32_t)u32(22) / 16),
            (long)((int32_t)u32(26) / 16));
    } else {
        snprintf(
            app->info_text, sizeof(app->info_text), "%s\nNot a Flipcraft world", app->sel_name);
    }
    open_text_box(app, true);
}

void main_callback(void* context, uint32_t index) {
    MenuApp* app = static_cast<MenuApp*>(context);
    if(index == IDX_WORLDS)
        open_worlds(app);
    else if(index == IDX_ABOUT)
        open_about(app);
}

void build_result_path(MenuApp* app, const char* stem) {
    char fname[NAME_LEN + 8];
    snprintf(fname, sizeof(fname), "%s.fcw", stem);
    join_path(app->result_path, sizeof(app->result_path), DATA_DIR, fname);
}

uint8_t form_flags(const MenuApp* app) {
    uint8_t f = (uint8_t)(app->mode_idx & FlipcraftFlagModeMask);
    if(!app->mobs_idx) f |= FlipcraftFlagMobsOff;
    if(!app->dist_idx) f |= FlipcraftFlagNearOnly;
    return f;
}

void form_changed(VariableItem* item) {
    MenuApp* app = static_cast<MenuApp*>(variable_item_get_context(item));
    const uint8_t idx = variable_item_get_current_value_index(item);
    const uint32_t row = variable_item_list_get_selected_item_index(app->form);

    if(app->form_mode == FORM_CREATE) {
        switch(row) {
        case ROW_MODE:
            app->mode_idx = idx;
            variable_item_set_current_value_text(item, MODE_LABELS[idx]);
            return;
        case ROW_TYPE:
            app->type_idx = idx;
            variable_item_set_current_value_text(item, TYPE_LABELS[idx]);
            return;
        case ROW_SIZE:
            app->size_idx = idx;
            variable_item_set_current_value_text(item, SIZE_LABELS[idx]);
            return;
        case ROW_MOBS:
            app->mobs_idx = idx;
            break;
        case ROW_DIST:
            app->dist_idx = idx;
            variable_item_set_current_value_text(item, DIST_LABELS[idx]);
            return;
        default:
            return;
        }
    } else {
        switch(row) {
        case SROW_MOBS:
            app->mobs_idx = idx;
            break;
        case SROW_DIST:
            app->dist_idx = idx;
            variable_item_set_current_value_text(item, DIST_LABELS[idx]);
            return;
        default:
            return;
        }
    }
    variable_item_set_current_value_text(item, ON_OFF[idx]);
}

void form_enter(void* context, uint32_t index);

// Builds the form from scratch. Returning from the keyboard must not go
// through here, or every value would snap back to its default.
void open_form(MenuApp* app, FormMode mode) {
    app->form_mode = mode;
    VariableItemList* l = app->form;
    variable_item_list_reset(l);
    variable_item_list_set_enter_callback(l, form_enter, app);

    app->it_name = nullptr;
    app->it_seed = nullptr;

    VariableItem* it;
    if(mode == FORM_CREATE) {
        it = app->it_name = variable_item_list_add(l, "Name", 1, nullptr, app);
        variable_item_set_current_value_text(it, app->name_buf);
        it = app->it_seed = variable_item_list_add(l, "Seed", 1, nullptr, app);
        variable_item_set_current_value_text(it, app->seed_text);
        it = variable_item_list_add(l, "Gamemode", 3, form_changed, app);
        variable_item_set_current_value_index(it, app->mode_idx);
        variable_item_set_current_value_text(it, MODE_LABELS[app->mode_idx]);
        it = variable_item_list_add(l, "Terrain", FlipcraftWorldCount, form_changed, app);
        variable_item_set_current_value_index(it, app->type_idx);
        variable_item_set_current_value_text(it, TYPE_LABELS[app->type_idx]);
    } else {
        // The mode, the terrain and the size are baked into the world and the
        // header at creation time, so here they are shown as plain text: one
        // value, no arrows, no padlock.
        it = variable_item_list_add(l, "Gamemode", 1, nullptr, app);
        variable_item_set_current_value_text(it, MODE_LABELS[app->mode_idx]);
        it = variable_item_list_add(l, "Terrain", 1, nullptr, app);
        variable_item_set_current_value_text(it, TYPE_LABELS[app->type_idx]);
    }

    it = variable_item_list_add(l, "Mobs", 2, form_changed, app);
    variable_item_set_current_value_index(it, app->mobs_idx);
    variable_item_set_current_value_text(it, ON_OFF[app->mobs_idx]);

    it = variable_item_list_add(l, "Draw dist", 2, form_changed, app);
    variable_item_set_current_value_index(it, app->dist_idx);
    variable_item_set_current_value_text(it, DIST_LABELS[app->dist_idx]);

    if(mode == FORM_CREATE) {
        it = variable_item_list_add(l, "Size", 4, form_changed, app);
        variable_item_set_current_value_index(it, app->size_idx);
        variable_item_set_current_value_text(it, SIZE_LABELS[app->size_idx]);
        variable_item_list_add(l, "Create", 1, nullptr, app);
    } else {
        variable_item_list_add(l, "Save", 1, nullptr, app);
    }
    variable_item_list_add(l, "Exit", 1, nullptr, app);

    variable_item_list_set_selected_item(l, 0);
    switch_view(app, VIEW_FORM);
}

// Fresh creation form: random seed, everything else at its documented default
// (survival, normal terrain, no mobs, full draw distance, smallest world).
void open_create(MenuApp* app) {
    snprintf(app->name_buf, sizeof(app->name_buf), "New world");
    app->seed = furi_hal_random_get();
    snprintf(app->seed_text, sizeof(app->seed_text), "%lu", (unsigned long)app->seed);
    app->mode_idx = 0;
    app->type_idx = 0;
    app->mobs_idx = 0;
    app->dist_idx = 1;
    app->size_idx = 0;
    open_form(app, FORM_CREATE);
}

void open_settings(MenuApp* app) {
    uint8_t hdr[64];
    if(!read_header(app->storage, app->sel_name, hdr, nullptr)) {
        snprintf(
            app->info_text, sizeof(app->info_text), "%s\nNot a Flipcraft world", app->sel_name);
        open_text_box(app, true);
        return;
    }
    const uint8_t flags = hdr[FLIPCRAFT_HDR_FLAGS_OFFSET];
    app->mode_idx = (uint8_t)(flags & FlipcraftFlagModeMask);
    if(app->mode_idx > FlipcraftModeCreative) app->mode_idx = FlipcraftModeSurvival;
    app->type_idx = hdr[FLIPCRAFT_HDR_TYPE_OFFSET];
    if(app->type_idx >= FlipcraftWorldCount) app->type_idx = FlipcraftWorldNormal;
    app->mobs_idx = (flags & FlipcraftFlagMobsOff) ? 0 : 1;
    app->dist_idx = (flags & FlipcraftFlagNearOnly) ? 0 : 1;
    open_form(app, FORM_SETTINGS);
}

void form_enter(void* context, uint32_t index) {
    MenuApp* app = static_cast<MenuApp*>(context);

    if(app->form_mode == FORM_CREATE) {
        switch(index) {
        case ROW_NAME:
            snprintf(app->text_buf, sizeof(app->text_buf), "%s", app->name_buf);
            open_text(app, TEXT_NAME, "World name", text_callback);
            return;
        case ROW_SEED:
            snprintf(app->text_buf, sizeof(app->text_buf), "%s", app->seed_text);
            open_text(app, TEXT_SEED, "World seed", text_callback);
            return;
        case ROW_CREATE:
            build_result_path(app, app->name_buf);
            app->result_action = Action::Generate;
            view_dispatcher_stop(app->vd);
            return;
        case ROW_EXIT:
            open_worlds(app);
            return;
        default:
            return;
        }
    }

    switch(index) {
    case SROW_SAVE:
        write_world_flags(app->storage, app->sel_name, form_flags(app));
        open_actions(app);
        return;
    case SROW_EXIT:
        open_actions(app);
        return;
    default:
        return;
    }
}

// Back from the keyboard into the form: only the edited row is refreshed, the
// rest of the form keeps whatever the player had already set.
void return_to_form(MenuApp* app, uint32_t row) {
    VariableItem* it = row == ROW_NAME ? app->it_name : app->it_seed;
    if(it)
        variable_item_set_current_value_text(it, row == ROW_NAME ? app->name_buf : app->seed_text);
    variable_item_list_set_selected_item(app->form, (uint8_t)row);
    switch_view(app, VIEW_FORM);
}

void text_callback(void* context) {
    MenuApp* app = static_cast<MenuApp*>(context);
    switch(app->text_mode) {
    case TEXT_NAME:
        snprintf(app->name_buf, sizeof(app->name_buf), "%s", app->text_buf);
        return_to_form(app, ROW_NAME);
        break;
    case TEXT_SEED:
        app->seed = parse_seed(app->text_buf);
        snprintf(app->seed_text, sizeof(app->seed_text), "%lu", (unsigned long)app->seed);
        return_to_form(app, ROW_SEED);
        break;
    case TEXT_TPL_NAME:
        build_result_path(app, app->text_buf);
        if(copy_file(app->storage, app->chosen_template, app->result_path)) {
            app->result_action = Action::Launch;
            view_dispatcher_stop(app->vd);
        } else {
            open_worlds(app); // copy failed: back to the list
        }
        break;
    case TEXT_RENAME: {
        char oldp[256], newp[256];
        join_path(oldp, sizeof(oldp), DATA_DIR, app->sel_name);
        char fname[NAME_LEN + 8];
        snprintf(fname, sizeof(fname), "%s.fcw", app->text_buf);
        join_path(newp, sizeof(newp), DATA_DIR, fname);
        if(strcmp(oldp, newp) != 0) storage_common_rename(app->storage, oldp, newp);
        open_worlds(app);
        break;
    }
    }
}

void dialog_callback(DialogExResult result, void* context) {
    MenuApp* app = static_cast<MenuApp*>(context);
    if(result == DialogExResultRight) {
        char path[256];
        join_path(path, sizeof(path), DATA_DIR, app->sel_name);
        storage_simply_remove(app->storage, path);
    }
    open_worlds(app);
}

void list_callback(void* context, uint32_t index) {
    MenuApp* app = static_cast<MenuApp*>(context);

    switch(app->list_mode) {
    case LIST_WORLDS:
        if(index == IDX_CREATE) {
            open_create(app);
        } else if(index == IDX_TEMPLATES) {
            open_templates(app);
        } else if((int)index < app->saves_count) {
            strncpy(app->sel_name, app->saves[index], NAME_LEN - 1);
            app->sel_name[NAME_LEN - 1] = '\0';
            open_actions(app);
        }
        break;

    case LIST_TEMPLATES:
        if((int)index < app->templates_count) {
            join_path(
                app->chosen_template,
                sizeof(app->chosen_template),
                ASSETS_DIR,
                app->templates[index]);
            strip_ext(app->text_buf, sizeof(app->text_buf), app->templates[index]);
            open_text(app, TEXT_TPL_NAME, "World name", text_callback);
        }
        break;

    case LIST_ACTIONS:
        switch(index) {
        case ACT_PLAY:
            join_path(app->result_path, sizeof(app->result_path), DATA_DIR, app->sel_name);
            app->result_action = Action::Launch;
            view_dispatcher_stop(app->vd);
            break;
        case ACT_INFO:
            open_info(app);
            break;
        case ACT_SETTINGS:
            open_settings(app);
            break;
        case ACT_RENAME:
            strip_ext(app->text_buf, sizeof(app->text_buf), app->sel_name);
            open_text(app, TEXT_RENAME, "New name", text_callback);
            break;
        case ACT_DELETE:
            dialog_ex_reset(app->dialog);
            dialog_ex_set_context(app->dialog, app);
            dialog_ex_set_result_callback(app->dialog, dialog_callback);
            dialog_ex_set_header(app->dialog, "Delete?", 64, 10, AlignCenter, AlignCenter);
            dialog_ex_set_text(app->dialog, app->sel_name, 64, 32, AlignCenter, AlignCenter);
            dialog_ex_set_left_button_text(app->dialog, "No");
            dialog_ex_set_right_button_text(app->dialog, "Yes");
            switch_view(app, VIEW_CONFIRM);
            break;
        }
        break;
    }
}

bool nav_callback(void* context) {
    MenuApp* app = static_cast<MenuApp*>(context);
    switch(app->current) {
    case VIEW_MAIN:
        app->result_action = Action::Quit;
        view_dispatcher_stop(app->vd);
        break;
    case VIEW_LIST:
        if(app->list_mode == LIST_WORLDS)
            open_main(app);
        else
            open_worlds(app);
        break;
    case VIEW_FORM:
        if(app->form_mode == FORM_CREATE)
            open_worlds(app);
        else
            open_actions(app); // settings are discarded unless Save was hit
        break;
    case VIEW_ABOUT:
        if(app->about_from_actions)
            open_actions(app);
        else
            open_main(app);
        break;
    case VIEW_TEXT:
        if(app->text_mode == TEXT_NAME)
            return_to_form(app, ROW_NAME);
        else if(app->text_mode == TEXT_SEED)
            return_to_form(app, ROW_SEED);
        else if(app->text_mode == TEXT_RENAME)
            open_actions(app);
        else
            open_templates(app);
        break;
    default: // delete dialog
        open_actions(app);
        break;
    }
    return true;
}

} // namespace

Result run(Gui* gui, Storage* storage) {
    storage_common_mkdir(storage, DATA_DIR);

    MenuApp* app = new(std::nothrow) MenuApp();
    Result result;
    if(!app) return result;

    app->gui = gui;
    app->storage = storage;

    app->vd = view_dispatcher_alloc();
    app->main_menu = submenu_alloc();
    app->list = submenu_alloc();
    app->form = variable_item_list_alloc();
    app->text_input = text_input_alloc();
    app->text_box = text_box_alloc();
    app->dialog = dialog_ex_alloc();

    view_dispatcher_set_event_callback_context(app->vd, app);
    view_dispatcher_set_navigation_event_callback(app->vd, nav_callback);

    view_dispatcher_add_view(app->vd, VIEW_MAIN, submenu_get_view(app->main_menu));
    view_dispatcher_add_view(app->vd, VIEW_LIST, submenu_get_view(app->list));
    view_dispatcher_add_view(app->vd, VIEW_FORM, variable_item_list_get_view(app->form));
    view_dispatcher_add_view(app->vd, VIEW_TEXT, text_input_get_view(app->text_input));
    view_dispatcher_add_view(app->vd, VIEW_ABOUT, text_box_get_view(app->text_box));
    view_dispatcher_add_view(app->vd, VIEW_CONFIRM, dialog_ex_get_view(app->dialog));

    open_main(app);

    view_dispatcher_attach_to_gui(app->vd, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_run(app->vd);

    view_dispatcher_remove_view(app->vd, VIEW_MAIN);
    view_dispatcher_remove_view(app->vd, VIEW_LIST);
    view_dispatcher_remove_view(app->vd, VIEW_FORM);
    view_dispatcher_remove_view(app->vd, VIEW_TEXT);
    view_dispatcher_remove_view(app->vd, VIEW_ABOUT);
    view_dispatcher_remove_view(app->vd, VIEW_CONFIRM);
    view_dispatcher_free(app->vd);
    submenu_free(app->main_menu);
    submenu_free(app->list);
    variable_item_list_free(app->form);
    text_input_free(app->text_input);
    text_box_free(app->text_box);
    dialog_ex_free(app->dialog);

    result.action = app->result_action;
    result.params.chunks = SIZE_CHUNKS[app->size_idx & 3];
    result.params.seed = app->seed;
    result.params.flags = form_flags(app);
    result.params.type = app->type_idx;
    strncpy(result.path, app->result_path, sizeof(result.path) - 1);
    delete app;
    return result;
}

}
}

static FlipcraftMenuAction
    flipcraft_menu_run(char* out_path, size_t out_size, FlipcraftWorldParams* out_params) {
    Gui* gui = reinterpret_cast<Gui*>(furi_record_open(RECORD_GUI));
    Storage* storage = reinterpret_cast<Storage*>(furi_record_open(RECORD_STORAGE));

    flipcraft::menu::Result result = flipcraft::menu::run(gui, storage);

    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);

    if(result.action != flipcraft::menu::Action::Quit && out_path && out_size) {
        strncpy(out_path, result.path, out_size - 1);
        out_path[out_size - 1] = '\0';
    }
    if(out_params) *out_params = result.params;
    switch(result.action) {
    case flipcraft::menu::Action::Launch:
        return FlipcraftMenuActionLaunch;
    case flipcraft::menu::Action::Generate:
        return FlipcraftMenuActionGenerate;
    default:
        return FlipcraftMenuActionQuit;
    }
}

static const FlipcraftMenuApi flipcraft_menu_api = {
    .run = flipcraft_menu_run,
};

static const FlipperAppPluginDescriptor flipcraft_menu_descriptor = {
    .appid = FLIPCRAFT_MENU_APP_ID,
    .ep_api_version = FLIPCRAFT_MENU_API_VERSION,
    .entry_point = &flipcraft_menu_api,
};

extern "C" const FlipperAppPluginDescriptor* flipcraft_menu_ep(void) {
    return &flipcraft_menu_descriptor;
}
