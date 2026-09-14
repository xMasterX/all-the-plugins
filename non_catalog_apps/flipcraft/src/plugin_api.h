// Copyright (c) 2026 ApertureFox Technology. MIT License.
#pragma once
//
// Contract between the tiny host FAP and the three loadable halves of the app.
//
// The host (main.cpp) is the only permanently resident code. The menu, the
// world generator and the game are built as .fal plugins, embedded into the
// FAP's assets, unpacked to /ext/apps_assets/flipcraft/plugins/ on first
// launch and mapped into RAM only while they run. While the menu is open the
// whole game (world, renderer, framebuffers) does not exist in memory, and
// vice versa.
//
// Plugins execute on the host's thread, so the storage aliases "/data" and
// "/assets" keep resolving to the flipcraft app folders.

#include <stdint.h>
#include <stddef.h>

// Per-world settings, stored in byte 36 of the .fcw header (previously a
// reserved zero). Every bit is polarised so that a zero byte -- what every
// world written before this existed, and every bundled template, still has --
// decodes to exactly the pre-settings behaviour: survival, mobs on, full 3x3
// draw distance. No format version bump is needed.
#define FLIPCRAFT_HDR_FLAGS_OFFSET 36u

// Terrain preset, byte 37 of the header (another reserved zero). Zero means
// FlipcraftWorldNormal, so every world written before this field existed reads
// back as the terrain it actually has. Generation-time only: the engine never
// looks at it, the blocks are already on disk.
#define FLIPCRAFT_HDR_TYPE_OFFSET 37u

enum {
    FlipcraftFlagModeMask = 0x03u, // FlipcraftMode
    FlipcraftFlagMobsOff = 0x04u, // creatures never spawn
    FlipcraftFlagNearOnly = 0x10u, // draw only the chunk the player stands in
    // 0x08 and 0x20 were the baked sun shadows and their "cheap" variant.
    // Both bits are ignored on read, so worlds created with them still open.
};

// Terrain presets the creation form offers. The order is part of the file
// format -- never renumber, only append.
typedef enum {
    FlipcraftWorldNormal = 0, // hills, biomes, ravines: the original generator
    FlipcraftWorldFlat = 1, // 5 flat courses, top at y=4, trees and a house
    FlipcraftWorldSuperflat = 2, // the same ground, bare: no trees, no house
    FlipcraftWorldWoods = 3, // normal relief, forest everywhere, dense trees
    FlipcraftWorldCount = 4,
} FlipcraftWorldType;

typedef enum {
    FlipcraftModeSurvival = 0,
    FlipcraftModeHardcore = 1, // death is final: the host removes the save
    FlipcraftModeCreative = 2, // block picker, infinite blocks, no damage
} FlipcraftMode;

// Everything the menu decides about a world that is about to be generated.
typedef struct {
    uint32_t seed;
    uint8_t chunks; // world size, 16..128 chunks per side
    uint8_t flags; // FlipcraftFlag* bitmask, written into the header
    uint8_t type; // FlipcraftWorldType, shapes the terrain and its features
} FlipcraftWorldParams;

#define FLIPCRAFT_MENU_APP_ID      "flipcraft_menu"
#define FLIPCRAFT_MENU_API_VERSION 5u

typedef enum {
    FlipcraftMenuActionQuit = 0, // leave the app
    FlipcraftMenuActionLaunch = 1, // out_path is an existing save to play
    FlipcraftMenuActionGenerate = 2, // generate the world at out_path, then play it
} FlipcraftMenuAction;

typedef struct {
    // Runs the world-selector UI and reports what the player chose. For
    // Generate, out_params carries the whole creation screen.
    FlipcraftMenuAction (*run)(char* out_path, size_t out_size, FlipcraftWorldParams* out_params);
} FlipcraftMenuApi;

#define FLIPCRAFT_GAME_APP_ID      "flipcraft_game"
#define FLIPCRAFT_GAME_API_VERSION 2u

typedef enum {
    FlipcraftGameResultOk = 0, // session over, the save stays
    FlipcraftGameResultDelete = 1, // hardcore death: the host removes the save
    FlipcraftGameResultError = -1,
} FlipcraftGameResult;

typedef struct {
    // Runs one game session for the save at world_path; returns when the
    // player quits back to the menu. Settings come from the world header.
    int32_t (*run)(const char* world_path);
} FlipcraftGameApi;

#define FLIPCRAFT_WORLDGEN_APP_ID      "flipcraft_worldgen"
#define FLIPCRAFT_WORLDGEN_API_VERSION 3u

typedef void (*FlipcraftGenProgress)(void* ctx, uint8_t percent);

typedef struct {
    // Writes a freshly generated params->chunks x params->chunks world
    // (16..128 per side) to path using exactly params->seed and the terrain
    // preset params->type, stamping params->flags into the header. Returns
    // false and removes the file on failure.
    bool (*generate)(
        const char* path,
        const FlipcraftWorldParams* params,
        FlipcraftGenProgress progress,
        void* progress_ctx);
} FlipcraftWorldgenApi;
