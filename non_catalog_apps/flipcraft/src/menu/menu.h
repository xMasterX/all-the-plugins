// Copyright (c) 2026 ApertureFox Technology. MIT License.
#pragma once

#include "../plugin_api.h"

#include <gui/gui.h>
#include <storage/storage.h>

namespace flipcraft {
namespace menu {

enum class Action {
    Quit,
    Launch,
    Generate
};

struct Result {
    Action action = Action::Quit;
    char path[256] = {0}; // full data path of the .fcw save to open or create
    FlipcraftWorldParams params{0, 16, 0, 0}; // what the creation screen was left at
};

// Owns its own ViewDispatcher; the caller keeps ownership of gui and storage.
Result run(Gui* gui, Storage* storage);

}
}
