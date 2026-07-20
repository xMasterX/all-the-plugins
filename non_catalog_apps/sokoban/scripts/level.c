#include "level.h"

#include "wave/files/file_lines_reader.h"
#include <furi.h>
#include <storage/storage.h>
#include <stdlib.h>
#include <string.h>

static int parse_row(const char* line, CellType* row)
{
    const char *ch = line;
    int i = 0;
    while (true)
    {
        if (i >= MAX_BOARD_SIZE)
            return -1;
        switch (*(ch++))
        {
        case '\0':
            return i;
        case '#':
            row[i++] = CellHasWall;
            break;
        case '*':
            row[i++] = CellHasBox | CellHasTarget;
            break;
        case '.':
            row[i++] = CellHasTarget;
            break;
        case '@':
            row[i++] = CellHasPlayer;
            break;
        case '+':
            row[i++] = CellHasPlayer | CellHasTarget;
            break;
        case '$':
            row[i++] = CellHasBox;
            break;
        case ' ':
            row[i++] = 0;
            break;
        default:
            return -2;
        }
    }
}

static int calculate_cell_size(int width, int height)
{
    #define MAX_WIDTH (128 + (cellSize - 1) * 2)
    #define MAX_HEIGHT (64 + (cellSize - 1) * 2)

    int cellSize = 9;
    if (width * cellSize > MAX_WIDTH || height * cellSize > MAX_HEIGHT)
        cellSize = 7;
    if (width * cellSize > MAX_WIDTH || height * cellSize > MAX_HEIGHT)
        cellSize = 5;
    return cellSize;

    #undef MAX_WIDTH
    #undef MAX_HEIGHT
}

void level_load(Level* ret_level, const char* collectionName, int levelIndex)
{
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    char filename[256];
    snprintf(filename, sizeof(filename), "%s/%s.txt", STORAGE_APP_ASSETS_PATH_PREFIX, collectionName);
    for (int i = 0; filename[i] != '\0'; i++)
        if (filename[i] >= 'A' && filename[i] <= 'Z')
            filename[i] += 'a' - 'A';

    FURI_LOG_D("GAME", "Opening file: %s", filename);
    storage_file_open(file, filename, FSAM_READ, FSOM_OPEN_EXISTING);

    char line[256];
    FileLinesReader* reader = file_lines_reader_alloc(file, sizeof(line));

    FURI_LOG_D("GAME", "Loading level %d", levelIndex);

    CellType board[MAX_BOARD_SIZE][MAX_BOARD_SIZE] = {0};
    int columnCount = 0, rowCount = 0;

    char levelStartMark[16];
    snprintf(levelStartMark, sizeof(levelStartMark), "%d", levelIndex + 1);

    bool levelFound = false;
    while (!levelFound && file_lines_reader_readln(reader, line, sizeof(line)))
        if (strncmp(levelStartMark, line, sizeof(levelStartMark)) == 0)
            levelFound = true;

    furi_check(levelFound, "level not found");

    while (file_lines_reader_readln(reader, line, sizeof(line)))
    {
        int rowSize = parse_row(line, board[rowCount]);
        if (rowSize < 0)
            break;
        if (rowSize > columnCount)
            columnCount = rowSize;
        rowCount += 1;
        if (rowCount >= MAX_BOARD_SIZE)
            break;
    }

    int naturalCellSize = calculate_cell_size(columnCount, rowCount);
    int rotatedCellSize = calculate_cell_size(rowCount, columnCount);
    if (naturalCellSize >= rotatedCellSize)
    {
        ret_level->cell_size = naturalCellSize;
        ret_level->level_width = columnCount;
        ret_level->level_height = rowCount;
        memcpy(ret_level->board, board, sizeof(board));
    }
    else
    {
        ret_level->cell_size = rotatedCellSize;
        ret_level->level_width = rowCount;
        ret_level->level_height = columnCount;
        for (int row = 0; row < rowCount; row++)
            for (int column = 0; column < columnCount; column++)
                ret_level->board[column][row] = board[row][column];
    }

    FURI_LOG_D("GAME", "Level size: %d x %d", ret_level->level_width, ret_level->level_height);

    file_lines_reader_free(reader);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}