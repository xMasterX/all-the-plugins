#pragma once

typedef struct LevelItem
{
    unsigned short worldBest;
    unsigned short playerBest;
} LevelItem;

typedef struct LevelsCollection
{
    char name[32];
    int levelsCount;
    LevelItem* levels;
} LevelsCollection;

typedef struct LevelsDatabase
{
    int collectionsCount;
    LevelsCollection* collections;
} LevelsDatabase;

LevelsDatabase* levels_database_load();
void levels_database_free(LevelsDatabase* levelsMetadata);
void levels_database_load_player_progress(LevelsDatabase* database);
void levels_database_save_player_progress(LevelsDatabase* database);