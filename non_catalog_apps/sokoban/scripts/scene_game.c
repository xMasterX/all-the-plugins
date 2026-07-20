#include "scene_game.h"
#include "app.h"
#include "levels_database.h"
#include "level.h"
#include "game_state.h"
#include "wave/scene_management.h"
#include "wave/calc.h"
#include "racso_sokoban_icons.h"
#include <dolphin/dolphin.h>
#include <gui/gui.h>
#include <furi.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

const int MAX_UNDO_STATES = 256;

static struct {
    Level* level;
    GameState* state;
} game;

// Victory Popup component
void victory_popup_render_callback(Canvas* const canvas, AppContext* app)
{
    const int ICON_SIDE = 9;

    AppGameplayState* gameplayState = app->gameplay;
    LevelsDatabase* database = app->database;
    LevelItem* levelItem = &database->collections[gameplayState->selectedCollection].levels[gameplayState->selectedLevel];

    GameState* state = game.state;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, "COMPLETED!");

    canvas_set_font(canvas, FontSecondary);
    char str[256];

    snprintf(str, sizeof(str), "Pushes: %d", state->pushesCount);
    canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignCenter, str);

    canvas_draw_icon(canvas, 32 - ICON_SIDE / 2, 28, &I_checkbox_checked);
    snprintf(str, sizeof(str), "Best: %d", levelItem->playerBest);
    canvas_draw_str_aligned(canvas, 32, 42, AlignCenter, AlignCenter, str);

    canvas_draw_icon(canvas, 96 - ICON_SIDE / 2, 28, &I_star);
    snprintf(str, sizeof(str), "World: %d", levelItem->worldBest);
    canvas_draw_str_aligned(canvas, 96, 42, AlignCenter, AlignCenter, str);

    const int START_CENTER_X = 100, START_CENTER_Y = 59;
    canvas_draw_circle(canvas, START_CENTER_X, START_CENTER_Y, 4);
    canvas_draw_disc(canvas, START_CENTER_X, START_CENTER_Y, 2);
    canvas_draw_str_aligned(canvas, START_CENTER_X + 8, START_CENTER_Y + 4, AlignLeft, AlignBottom, "Next");
}

void victory_popup_handle_input(InputKey key, InputType type, AppContext* app)
{
    AppGameplayState* gameplayState = app->gameplay;

    if (key == InputKeyOk && type == InputTypePress)
    {
        if (gameplayState->selectedLevel + 1 < app->database->collections[gameplayState->selectedCollection].levelsCount)
        {
            gameplayState->selectedLevel += 1;
            scene_manager_set_scene(app->sceneManager, SceneType_Game);
            return;
        }
        else
        {
            scene_manager_set_scene(app->sceneManager, SceneType_Menu);
            return;
        }
    }
}

const Icon* findIcon(CellType cellType, int size)
{
    switch (cellType)
    {
    case CellHasWall:
        switch (size)
        {
        case 5:
            return &I_cell_wall_5;
        case 7:
            return &I_cell_wall_7;
        case 9:
            return &I_cell_wall_9;
        }
        break;

    case CellHasBox:
        switch (size)
        {
        case 5:
            return &I_cell_box_5;
        case 7:
            return &I_cell_box_7;
        case 9:
            return &I_cell_box_9;
        }
        break;

    case CellHasTarget:
        switch (size)
        {
        case 5:
            return &I_cell_target_5;
        case 7:
            return &I_cell_target_7;
        case 9:
            return &I_cell_target_9;
        }
        break;

    case CellHasPlayer:
        switch (size)
        {
        case 5:
            return &I_cell_player_5;
        case 7:
            return &I_cell_player_7;
        case 9:
            return &I_cell_player_9;
        }
        break;

    case CellHasBox | CellHasTarget:
        switch (size)
        {
        case 5:
            return &I_cell_box_target_5;
        case 7:
            return &I_cell_box_target_7;
        case 9:
            return &I_cell_box_target_9;
        }
        break;

    case CellHasPlayer | CellHasTarget:
        switch (size)
        {
        case 5:
            return &I_cell_player_target_5;
        case 7:
            return &I_cell_player_target_7;
        case 9:
            return &I_cell_player_target_9;
        }
        break;

    default:
        return NULL;
    }

    return NULL;
}

void draw_game(Canvas* const canvas)
{
    GameState* state = game.state;
    Level *level = game.level;

    int cellSize = level->cell_size;
    int levelWidth = level->level_width * cellSize;
    int levelHeight = level->level_height * cellSize;

    int playerX = state->playerX * cellSize;
    int playerY = state->playerY * cellSize;

    int screenWidth = 128;
    int screenHeight = 64;

    int minScrollingWidth = screenWidth + (cellSize - 1) * 2;
    int minScrollingHeight = screenHeight + (cellSize - 1) * 2;

    int cameraX = levelWidth / 2;
    if (levelWidth > minScrollingWidth)
        cameraX = MAX(screenWidth / 2, MIN(playerX, levelWidth - screenWidth / 2));

    int cameraY = levelHeight / 2;
    if (levelHeight > minScrollingHeight)
        cameraY = MAX(screenHeight / 2, MIN(playerY, levelHeight - screenHeight / 2));

    for (int row = 0; row < level->level_height; row++)
    {
        for (int column = 0; column < level->level_width; column++)
        {
            int x = column * cellSize - cameraX + screenWidth / 2;
            int y = row * cellSize - cameraY + screenHeight / 2;
            const Icon* icon = findIcon(state->board[row][column], cellSize);
            if (icon)
                canvas_draw_icon(canvas, x, y, icon);
        }
    }
}

void game_render_callback(Canvas* const canvas, void* context)
{
    AppContext* app = (AppContext*)context;

    canvas_clear(canvas);
    GameState* state = game.state;
    Level* level = game.level;

    if (state == NULL || level == NULL)
        return;

    if (game.state->isCompleted)
        victory_popup_render_callback(canvas, app);
    else
        draw_game(canvas);
}

void game_transition_callback(int from, int to, void* context)
{
    AppContext* app = (AppContext*)context;
    AppGameplayState* gameplayState = app->gameplay;
    LevelsDatabase* database = app->database;

    if (from == SceneType_Game)
    {
        game_state_free(game.state);
        free(game.level);
    }

    if (to == SceneType_Game)
    {
        const char *collectionName = database->collections[gameplayState->selectedCollection].name;
        int levelIndex = gameplayState->selectedLevel;

        game.level = malloc(sizeof(Level));
        level_load(game.level, collectionName, levelIndex);

        game.state = game_state_initialize(game.level, MAX_UNDO_STATES);
    }
}

void game_handle_player_input(InputKey key, InputType type)
{
    if (type != InputTypePress && type != InputTypeRepeat)
        return;

    if (key == InputKeyOk)
    {
        game_state_undo_move(game.state);
        return;
    }

    int dx = 0, dy = 0;
    switch (key)
    {
    case InputKeyLeft:
        dx = -1;
        break;
    case InputKeyRight:
        dx = 1;
        break;
    case InputKeyUp:
        dy = -1;
        break;
    case InputKeyDown:
        dy = 1;
        break;
    default:
        return;
    }

    game_state_apply_move(game.state, dx, dy);
}

void game_handle_input(InputKey key, InputType type, void* context)
{
    AppContext* app = (AppContext*)context;
    GameState* gameState = game.state;

    if (key == InputKeyBack && type == InputTypePress)
    {
        scene_manager_set_scene(app->sceneManager, SceneType_Menu);
        return;
    }

    if (game.state->isCompleted)
    {
        victory_popup_handle_input(key, type, app);
        return;
    }

    game_handle_player_input(key, type);

    if (game.state->isCompleted)
    {
        FURI_LOG_D("GAME", "Level completed in %d pushes", gameState->pushesCount);

        dolphin_deed(DolphinDeedPluginGameWin);
        AppGameplayState* gameplayState = app->gameplay;
        LevelsDatabase* database = app->database;

        LevelItem* levelItem = &database->collections[gameplayState->selectedCollection].levels[gameplayState->selectedLevel];
        if (levelItem->playerBest == 0 || gameState->pushesCount < levelItem->playerBest)
        {
            levelItem->playerBest = gameState->pushesCount;
            levels_database_save_player_progress(database);
        }
    }
}

void game_tick_callback(void* context)
{
    AppContext* app = (AppContext*)context;
    UNUSED(app);
}