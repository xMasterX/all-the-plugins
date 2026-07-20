#include "levels_database.h"

#include <stdio.h>
#include <storage/storage.h>
#include "wave/files/file_lines_reader.h"

static const char* DATABASE_PATH = APP_ASSETS_PATH("database.txt");
static const char* SAVE_DATA_PATH = APP_DATA_PATH("sokoban.save");

static LevelsDatabase* levels_database_alloc(int collectionsCount)
{
    LevelsDatabase* levelsMetadata = malloc(sizeof(LevelsDatabase));
    levelsMetadata->collectionsCount = collectionsCount;
    levelsMetadata->collections = malloc(collectionsCount * sizeof(LevelsCollection));
    return levelsMetadata;
}

void levels_database_free(LevelsDatabase* levelsMetadata)
{
    for (int i = 0; i < levelsMetadata->collectionsCount; i++)
        free(levelsMetadata->collections[i].levels);
    free(levelsMetadata->collections);
    free(levelsMetadata);
}

LevelsDatabase* levels_database_load()
{
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if (!storage_file_open(file, DATABASE_PATH, FSAM_READ, FSOM_OPEN_EXISTING))
    {
        FURI_LOG_E("GAME", "Failed to open levels metadata file: %s", DATABASE_PATH);
        furi_crash("Failed to open levels metadata file");
    }

    char line[256];
    FileLinesReader* reader = file_lines_reader_alloc(file, sizeof(line));

    file_lines_reader_readln(reader, line, sizeof(line));
    if (strcmp(line, "1") != 0)
    {
        FURI_LOG_E("GAME", "Unsupported levels metadata version: %s", line);
        furi_crash("Unsupported level metadata version");
    }

    file_lines_reader_readln(reader, line, sizeof(line));
    FURI_LOG_D("GAME", "Collections count: %s", line);
    int collectionsCount = atoi(line);

    LevelsDatabase* levelsMetadata = levels_database_alloc(collectionsCount);

    for (int collectionIndex = 0; collectionIndex < collectionsCount; collectionIndex++)
    {
        LevelsCollection collection;

        file_lines_reader_readln(reader, line, sizeof(line));
        strncpy(collection.name, line, sizeof(collection.name));
        FURI_LOG_D("GAME", "Collection name: %s", collection.name);

        file_lines_reader_readln(reader, line, sizeof(line));
        FURI_LOG_D("GAME", "Levels count: %s", line);
        int levelsCount = atoi(line);
        collection.levelsCount = levelsCount;
        collection.levels = malloc(levelsCount * sizeof(LevelItem));

        for (int levelInCollectionIndex = 0; levelInCollectionIndex < levelsCount; levelInCollectionIndex++)
        {
            LevelItem levelItem;
            file_lines_reader_readln(reader, line, sizeof(line));
            levelItem.worldBest = atoi(line);
            levelItem.playerBest = 0;

            collection.levels[levelInCollectionIndex] = levelItem;
        }

        levelsMetadata->collections[collectionIndex] = collection;
        FURI_LOG_D("GAME", "Loaded %d levels metadata for collection %d.", levelsCount, collectionIndex);
    }

    FURI_LOG_D("GAME", "Loaded %d collections metadata.", collectionsCount);

    file_lines_reader_free(reader);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    return levelsMetadata;
}

void levels_database_save_player_progress(LevelsDatabase* database)
{
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if (!storage_file_open(file, SAVE_DATA_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS))
    {
        FURI_LOG_E("GAME", "Failed to open file to save progress: %s", SAVE_DATA_PATH);
        furi_crash("Failed to open file to save progress");
    }

    const char versionLine[] = "1\n";
    storage_file_write(file, versionLine, sizeof(versionLine) - 1);

    for (int collectionIndex = 0; collectionIndex < database->collectionsCount; collectionIndex++)
    {
        LevelsCollection collection = database->collections[collectionIndex];
        for (int levelInCollectionIndex = 0; levelInCollectionIndex < collection.levelsCount; levelInCollectionIndex++)
        {
            LevelItem levelItem = collection.levels[levelInCollectionIndex];
            char line[32];
            int length = snprintf(line, sizeof(line), "%d\n", levelItem.playerBest);
            storage_file_write(file, line, length);
        }
    }

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

void levels_database_load_player_progress(LevelsDatabase* database)
{
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if (!storage_file_open(file, SAVE_DATA_PATH, FSAM_READ, FSOM_OPEN_EXISTING))
    {
        FURI_LOG_D("GAME", "Could not open file to load progress: %s", SAVE_DATA_PATH);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return;
    }

    FileLinesReader* reader = file_lines_reader_alloc(file, 32);

    char line[32];
    file_lines_reader_readln(reader, line, sizeof(line));
    if (strcmp(line, "1") != 0)
    {
        FURI_LOG_E("GAME", "Unsupported player progress version: %s", line);
        furi_crash("Unsupported player progress version");
    }

    for (int collectionIndex = 0; collectionIndex < database->collectionsCount; collectionIndex++)
    {
        LevelsCollection* collection = &database->collections[collectionIndex];
        for (int levelInCollectionIndex = 0; levelInCollectionIndex < collection->levelsCount; levelInCollectionIndex++)
        {
            LevelItem levelItem = collection->levels[levelInCollectionIndex];
            file_lines_reader_readln(reader, line, 32);
            levelItem.playerBest = atoi(line);

            collection->levels[levelInCollectionIndex] = levelItem;
        }
    }

    file_lines_reader_free(reader);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}