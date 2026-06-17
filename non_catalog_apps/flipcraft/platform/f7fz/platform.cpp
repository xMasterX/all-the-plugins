#include "platform.h"

#include "../../game/game.h"
#include "../../menu/menu.h"

#include <furi.h>
#include <gui/canvas.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>

#include <new>
#include <string.h>

namespace flipcraft {
namespace platform {

static constexpr int DISP_W = 128;
static constexpr int DISP_H = 64;
static constexpr int SSD_SZ = DISP_W * DISP_H / 8;
static_assert(SCREEN_WIDTH == DISP_W && SCREEN_HEIGHT == DISP_H);

struct F7File {
    Storage* storage = nullptr;
    ::File* file = nullptr;
};

static void* fsOpen(void*, const char* path, FileMode mode) {
    Storage* storage = reinterpret_cast<Storage*>(furi_record_open(RECORD_STORAGE));
    ::File* file = storage_file_alloc(storage);

    FS_AccessMode access = FSAM_READ;
    FS_OpenMode open = FSOM_OPEN_EXISTING;
    switch(mode) {
    case FileMode::Read:
        access = FSAM_READ;
        open = FSOM_OPEN_EXISTING;
        break;
    case FileMode::WriteTruncate:
        access = FSAM_WRITE;
        open = FSOM_CREATE_ALWAYS;
        break;
    case FileMode::ReadWriteExisting:
        access = FSAM_READ_WRITE;
        open = FSOM_OPEN_EXISTING;
        break;
    }

    if(!storage_file_open(file, path, access, open)) {
        storage_file_close(file);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return nullptr;
    }

    F7File* out = new(std::nothrow) F7File();
    if(!out) {
        storage_file_close(file);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return nullptr;
    }

    out->storage = storage;
    out->file = file;
    return out;
}

static void fsClose(void*, void* handle) {
    F7File* file = reinterpret_cast<F7File*>(handle);
    if(!file) return;
    storage_file_close(file->file);
    storage_file_free(file->file);
    furi_record_close(RECORD_STORAGE);
    delete file;
}

static bool fsSeek(void*, void* handle, uint32_t offset) {
    F7File* file = reinterpret_cast<F7File*>(handle);
    return file && storage_file_seek(file->file, offset, true);
}

static size_t fsRead(void*, void* handle, void* data, size_t size) {
    F7File* file = reinterpret_cast<F7File*>(handle);
    if(!file) return 0;
    return storage_file_read(file->file, data, size);
}

static size_t fsWrite(void*, void* handle, const void* data, size_t size) {
    F7File* file = reinterpret_cast<F7File*>(handle);
    if(!file) return 0;
    return storage_file_write(file->file, data, size);
}

static uint32_t fsSize(void*, void* handle) {
    F7File* file = reinterpret_cast<F7File*>(handle);
    if(!file) return 0;
    return storage_file_size(file->file);
}

static void fsSync(void*, void* handle) {
    F7File* file = reinterpret_cast<F7File*>(handle);
    if(file) storage_file_sync(file->file);
}

static void delayMs(uint32_t ms) {
    furi_delay_ms(ms);
}

struct AppState {
    Game* game = nullptr;
    FuriMutex* mutex = nullptr;
    ViewPort* view_port = nullptr;

    volatile uint8_t held = 0;
    volatile uint8_t ev_direction = 0;
    volatile uint8_t ev_ok_direction = 0;
    volatile bool ok_chord_used = false;

    volatile bool ev_ok_short = false;
    volatile bool ev_ok_long = false;
    volatile bool ev_back_short = false;
    volatile bool ev_back_long = false;
    volatile bool ev_exit = false;
};

static void packSsd(const Framebuffer& fb, uint8_t* ssd) {
    memset(ssd, 0, SSD_SZ);
    for(int page = 0; page < (SCREEN_HEIGHT / 8); ++page) {
        for(int col = 0; col < SCREEN_WIDTH; ++col) {
            uint8_t byte = 0;
            for(int bit = 0; bit < 8; ++bit) {
                if(fb.px[page * 8 + bit][col]) byte |= static_cast<uint8_t>(1u << bit);
            }
            ssd[page * DISP_W + col] = byte;
        }
    }
}

static void drawCb(Canvas* canvas, void* ctx) {
    AppState* st = reinterpret_cast<AppState*>(ctx);

    if(furi_mutex_acquire(st->mutex, 0) != FuriStatusOk) return;

    uint8_t* ssd = canvas_get_buffer(canvas);
    if(ssd) packSsd(st->game->fb, ssd);

    furi_mutex_release(st->mutex);
}

static void inputCb(InputEvent* ev, void* ctx) {
    AppState* st = reinterpret_cast<AppState*>(ctx);

    switch(ev->key) {
    case InputKeyUp:
        if(ev->type == InputTypePress || ev->type == InputTypeRepeat) {
            st->held |= 0x01;
            st->ev_direction |= 0x01;
            if(st->held & 0x10) {
                st->ev_direction &= ~0x01;
                st->ev_ok_direction |= 0x01;
                st->ok_chord_used = true;
            }
        } else if(ev->type == InputTypeRelease) {
            st->held &= ~0x01;
        }
        break;
    case InputKeyDown:
        if(ev->type == InputTypePress || ev->type == InputTypeRepeat) {
            st->held |= 0x02;
            st->ev_direction |= 0x02;
            if(st->held & 0x10) {
                st->ev_direction &= ~0x02;
                st->ev_ok_direction |= 0x02;
                st->ok_chord_used = true;
            }
        } else if(ev->type == InputTypeRelease) {
            st->held &= ~0x02;
        }
        break;
    case InputKeyLeft:
        if(ev->type == InputTypePress || ev->type == InputTypeRepeat) {
            st->held |= 0x04;
            st->ev_direction |= 0x04;
            if(st->held & 0x10) {
                st->ev_direction &= ~0x04;
                st->ev_ok_direction |= 0x04;
                st->ok_chord_used = true;
            }
        } else if(ev->type == InputTypeRelease) {
            st->held &= ~0x04;
        }
        break;
    case InputKeyRight:
        if(ev->type == InputTypePress || ev->type == InputTypeRepeat) {
            st->held |= 0x08;
            st->ev_direction |= 0x08;
            if(st->held & 0x10) {
                st->ev_direction &= ~0x08;
                st->ev_ok_direction |= 0x08;
                st->ok_chord_used = true;
            }
        } else if(ev->type == InputTypeRelease) {
            st->held &= ~0x08;
        }
        break;
    case InputKeyOk:
        if(ev->type == InputTypePress) {
            st->held |= 0x10;
            uint8_t directions = st->held & 0x0F;
            st->ok_chord_used = directions != 0;
            st->ev_direction &= ~directions;
            st->ev_ok_direction |= directions;
        } else if(ev->type == InputTypeShort) {
            if(!st->ok_chord_used) st->ev_ok_short = true;
        } else if(ev->type == InputTypeLong) {
            if(!st->ok_chord_used) st->ev_ok_long = true;
        } else if(ev->type == InputTypeRelease) {
            st->held &= ~0x10;
        }
        break;
    case InputKeyBack:
        if(ev->type == InputTypePress && (st->held & 0x10)) {
            st->ev_exit = true;
            st->ok_chord_used = true;
        }
        if(ev->type == InputTypeShort) st->ev_back_short = true;
        if(ev->type == InputTypeLong) st->ev_back_long = true;
        break;
    default:
        break;
    }
}

static Input buildGameInput(AppState* st) {
    uint8_t held = st->held;
    uint8_t direction = st->ev_direction;
    uint8_t ok_direction = st->ev_ok_direction;
    bool ok_s = st->ev_ok_short;
    bool ok_l = st->ev_ok_long;
    bool back_s = st->ev_back_short;
    bool back_l = st->ev_back_long;

    st->ev_direction = 0;
    st->ev_ok_direction = 0;
    st->ev_ok_short = false;
    st->ev_ok_long = false;
    st->ev_back_short = false;
    st->ev_back_long = false;

    bool in_menu = st->game->screenId != SCR_PLAY;
    Input in{};

    if(in_menu) {
        uint8_t nav = ok_direction ? ok_direction : direction;
        if(nav & 0x01) in.navY = 1;
        if(nav & 0x02) in.navY = -1;
        if(nav & 0x04) in.navX = -1;
        if(nav & 0x08) in.navX = 1;
        if(ok_direction) in.distribute = true;
        if(ok_s) in.menuSelect = true;
        if(back_s || back_l) in.openInventory = true;
    } else {
        if(ok_direction) {
            if(ok_direction & 0x01) in.pitch = 1;
            if(ok_direction & 0x02) in.pitch = -1;
            if(ok_direction & 0x04) in.slotScroll = -1;
            if(ok_direction & 0x08) in.slotScroll = 1;
        } else {
            if(held & 0x01) in.forward = 8;
            if(held & 0x02) in.forward = -8;
            if(held & 0x04) in.turn = 1;
            if(held & 0x08) in.turn = -1;
        }
        if(ok_s) in.placePressed = true;
        if(ok_l) in.breakPressed = true;
        if(back_s) in.jump = true;
        if(back_l) in.openInventory = true;
    }

    return in;
}

static const FileSystem g_files = {
    nullptr,
    fsOpen,
    fsClose,
    fsSeek,
    fsRead,
    fsWrite,
    fsSize,
    fsSync,
};

// Run a single game session for the save at `path`. Returns to the caller (the
// menu loop) when the player quits. Opening a save that fails to load is not
// fatal: we simply return so the menu can be shown again.
static void runGame(Game& game, Gui* gui, const char* path) {
    AppState* st = new(std::nothrow) AppState();
    if(!st) return;

    st->game = &game;
    st->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!st->mutex) {
        delete st;
        return;
    }

    GameConfig config = {&g_files, path};
    if(!game.setup(config)) {
        furi_mutex_free(st->mutex);
        delete st;
        return;
    }

    st->view_port = view_port_alloc();
    view_port_draw_callback_set(st->view_port, drawCb, st);
    view_port_input_callback_set(st->view_port, inputCb, st);
    gui_add_view_port(gui, st->view_port, GuiLayerFullscreen);

    furi_mutex_acquire(st->mutex, FuriWaitForever);
    game.frame(Input{});
    furi_mutex_release(st->mutex);
    view_port_update(st->view_port);

    while(true) {
        Input in = buildGameInput(st);
        if(st->ev_exit) break;

        furi_mutex_acquire(st->mutex, FuriWaitForever);
        game.frame(in);
        furi_mutex_release(st->mutex);

        view_port_update(st->view_port);
        delayMs(33);
    }

    furi_mutex_acquire(st->mutex, FuriWaitForever);
    game.shutdown();
    furi_mutex_release(st->mutex);

    view_port_enabled_set(st->view_port, false);
    gui_remove_view_port(gui, st->view_port);
    view_port_free(st->view_port);
    furi_mutex_free(st->mutex);
    delete st;
}

int32_t run(Game& game) {
    Storage* storage = reinterpret_cast<Storage*>(furi_record_open(RECORD_STORAGE));
    Gui* gui = reinterpret_cast<Gui*>(furi_record_open(RECORD_GUI));

    // Show the save selector, play the chosen world, then return to the menu
    // until the player leaves it.
    while(true) {
        menu::Result choice = menu::run(gui, storage);
        if(!choice.launch) break;
        runGame(game, gui, choice.path);
    }

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    return 0;
}

}
}
