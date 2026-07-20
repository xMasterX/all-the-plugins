#pragma once

#define MAX_BOARD_SIZE 50

typedef char CellType;
enum {
    CellHasWall = 0x1,
    CellHasBox = 0x2,
    CellHasTarget = 0x4,
    CellHasPlayer = 0x8
};

typedef struct Level
{
    int level_width, level_height;
    int cell_size;
    CellType board[MAX_BOARD_SIZE][MAX_BOARD_SIZE];
} Level;

void level_load(Level* ret_level, const char* collectionName, int levelIndex);