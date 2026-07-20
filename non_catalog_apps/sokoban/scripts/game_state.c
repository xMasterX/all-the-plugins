#include "game_state.h"

#include "level.h"
#include <stdlib.h>
#include <string.h>

GameState* game_state_initialize(Level* level, int undoCapacity)
{
    GameState* state = malloc(sizeof(GameState));

    state->playerX = state->playerY = state->pushesCount = 0;
    state->isCompleted = false;

    state->levelWidth = level->level_width;
    state->levelHeight = level->level_height;
    memcpy(state->board, level->board, sizeof(CellType) * MAX_BOARD_SIZE * MAX_BOARD_SIZE);

    for (int y = 0; y < level->level_height; y++)
    {
        for (int x = 0; x < level->level_width; x++)
        {
            if (state->board[y][x] & CellHasPlayer)
            {
                state->playerX = x;
                state->playerY = y;
            }
        }
    }

    state->undoHead = state->undoTail = 0;
    state->undoCapacity = undoCapacity;
    state->undoBuffer = malloc(undoCapacity * sizeof(UndoToken));

    return state;
}

void game_state_free(GameState* state)
{
    free(state->undoBuffer);
    free(state);
}

enum UndoFlags {
    UndoInvalid = 0x80,

    UndoDirectionMask = 0x03,
    UndoLeft = 0x00,
    UndoRight = 0x01,
    UndoUp = 0x02,
    UndoDown = 0x03,

    UndoBoxPushed = 0x04,
};

static void verify_level_completed(GameState* state)
{
    for (int y = 0; y < state->levelHeight; y++)
    {
        for (int x = 0; x < state->levelWidth; x++)
        {
            if ((state->board[y][x] & CellHasBox) && !(state->board[y][x] & CellHasTarget))
                return;
        }
    }

    state->isCompleted = true;
}

static bool is_in_bounds(GameState *state, int x, int y)
{
    return 0 <= x && x < state->levelWidth
        && 0 <= y && y < state->levelHeight;
}

static void undo_push(GameState *state, UndoToken token)
{
    state->undoHead += 1;
    if (state->undoHead == state->undoCapacity)
        state->undoHead = 0;
    state->undoBuffer[state->undoHead] = token;

    if (state->undoHead == state->undoTail)
    {
        state->undoTail += 1;
        if (state->undoTail == state->undoCapacity)
            state->undoTail = 0;
    }
}

static UndoToken undo_pop(GameState *state)
{
    if (state->undoHead == state->undoTail)
        return UndoInvalid;

    UndoToken token = state->undoBuffer[state->undoHead];
    state->undoHead -= 1;
    if (state->undoHead < 0)
        state->undoHead = state->undoCapacity - 1;
    return token;
}

void game_state_apply_move(GameState* state, int dx, int dy)
{
    int newX = state->playerX + dx, newY = state->playerY + dy;
    if (!is_in_bounds(state, newX, newY))
        return;

    UndoToken token;
    if (dx < 0)
        token = UndoLeft;
    if (dx > 0)
        token = UndoRight;
    if (dy < 0)
        token = UndoUp;
    if (dy > 0)
        token = UndoDown;

    CellType newCell = state->board[newY][newX];
    if (newCell & CellHasWall)
        return;

    if (newCell & CellHasBox)
    {
        int newBoxX = newX + dx, newBoxY = newY + dy;
        if (!is_in_bounds(state, newBoxX, newBoxY))
            return;

        CellType newBoxCell = state->board[newBoxY][newBoxX];
        if ((newBoxCell & CellHasWall) || (newBoxCell & CellHasBox))
            return;

        token |= UndoBoxPushed;

        state->board[newBoxY][newBoxX] |= CellHasBox;
        state->board[newY][newX] = newCell & ~CellHasBox;
        state->pushesCount += 1;
    }

    state->board[newY][newX] |= CellHasPlayer;
    state->board[state->playerY][state->playerX] &= ~CellHasPlayer;

    state->playerX = newX;
    state->playerY = newY;

    undo_push(state, token);
    verify_level_completed(state);
}

void game_state_undo_move(GameState* state)
{
    UndoToken token = undo_pop(state);
    if (token == UndoInvalid)
        return;

    int dx = 0, dy = 0;
    if ((token & UndoDirectionMask) == UndoLeft)
        dx = -1;
    if ((token & UndoDirectionMask) == UndoRight)
        dx = +1;
    if ((token & UndoDirectionMask) == UndoUp)
        dy = -1;
    if ((token & UndoDirectionMask) == UndoDown)
        dy = +1;

    if (token & UndoBoxPushed)
    {
        int boxX = state->playerX + dx, boxY = state->playerY + dy;
        state->board[boxY][boxX] &= ~CellHasBox;
        state->board[state->playerY][state->playerX] |= CellHasBox;
        state->pushesCount -= 1;
    }

    int oldX = state->playerX - dx, oldY = state->playerY - dy;
    state->board[state->playerY][state->playerX] &= ~CellHasPlayer;
    state->board[oldY][oldX] |= CellHasPlayer;

    state->playerX = oldX;
    state->playerY = oldY;

    verify_level_completed(state);
}