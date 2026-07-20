#pragma once

#include "level.h"
#include <stdbool.h>

typedef char UndoToken;

typedef struct GameState
{
    int playerX, playerY, pushesCount;
    int levelWidth, levelHeight;
    CellType board[MAX_BOARD_SIZE][MAX_BOARD_SIZE];
    bool isCompleted;
    int undoHead, undoTail, undoCapacity;
    UndoToken* undoBuffer;
} GameState;

GameState* game_state_initialize(Level* level, int undoCapacity);
void game_state_free(GameState* state);

void game_state_apply_move(GameState* state, int dx, int dy);
void game_state_undo_move(GameState* state);