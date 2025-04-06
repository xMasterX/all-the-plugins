#pragma once

#include <toolbox/cli/cli_command.h>
#include <cli/cli_main_commands.h>
#include <cli/cli_ansi.h>
#include <furi/core/string.h>
#include "../types/plugin_state.h"

#define PLUGIN_APP_ID "totp_cli"
#define PLUGIN_API_VERSION 1

typedef struct {
    const char* name;
    void (*handle)(PluginState*, FuriString*, PipeSide*);
} CliPlugin;
