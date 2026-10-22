#pragma once

typedef enum MenuIndices {
    MenuIndexInterval,
    MenuIndexTiming,
    MenuIndexTxCount,
    MenuIndexTxMode,
    MenuIndexTxDelay,
    MenuIndexTxRadio,
    MenuIndexSelectFile,
    MenuIndexStepMode,
    MenuIndexScheduleType,
    MenuIndexStartLine,
    MenuIndexEditLines,
    MenuIndexSaveSchedule,
    MenuIndexStartSchedule,
    MenuIndexNum,
} MenuIndices;

typedef enum FileTxType {
    SchedulerFileTypeSingle,
    SchedulerFileTypePlaylist,
    SchedulerFileTypeNum
} FileTxType;
#define FILE_TYPE_COUNT SchedulerFileTypeNum
static const char* const file_type_text[SchedulerFileTypeNum] = {"Single", "Playlist"};

typedef enum SchedulerTxMode {
    SchedulerTxModeNormal,
    SchedulerTxModeImmediate,
    SchedulerTxModeOneShot,
    SchedulerTxModeSettingsNum,
} SchedulerTxMode;
#define TX_MODE_COUNT SchedulerTxModeSettingsNum
static const char* const tx_mode_text[SchedulerTxModeSettingsNum] = {
    "Normal",
    "Immed.",
    "1-Shot",
};

typedef enum SchedulerTimingMode {
    SchedulerTimingModeRelative,
    SchedulerTimingModePrecise,
    SchedulerTimingModeNum
} SchedulerTimingMode;
#define TIMING_MODE_COUNT SchedulerTimingModeNum
static const char* const timing_mode_text[TIMING_MODE_COUNT] = {"Relative", "Precise"};

// Playlist step mode: send the whole playlist per trigger, or one line per trigger
typedef enum SchedulerStepMode {
    SchedulerStepModeAll,
    SchedulerStepModeOne,
    SchedulerStepModeNum
} SchedulerStepMode;
#define STEP_MODE_COUNT SchedulerStepModeNum
static const char* const step_mode_text[STEP_MODE_COUNT] = {"All", "One"};

// Schedule type, only used when stepping through a playlist one line at a time
typedef enum SchedulerScheduleType {
    SchedulerScheduleUniform,
    SchedulerSchedulePerLine,
    SchedulerScheduleTimeOfDay,
    SchedulerScheduleNum
} SchedulerScheduleType;
#define SCHEDULE_TYPE_COUNT SchedulerScheduleNum
static const char* const schedule_type_text[SCHEDULE_TYPE_COUNT] = {"Uniform", "Per Line", "Time"};

// First line of a Time schedule: always line 1, or the line whose time comes up first
typedef enum SchedulerStartLine {
    SchedulerStartLineFirst,
    SchedulerStartLineNearest,
    SchedulerStartLineNum
} SchedulerStartLine;
#define START_LINE_COUNT SchedulerStartLineNum
static const char* const start_line_text[START_LINE_COUNT] = {"Line 1", "Nearest"};

// Playlist line count is stored as int8_t
#define SCHEDULER_MAX_LINES 127

#define SECONDS_PER_DAY      86400UL
#define DEFAULT_LINE_SECONDS (12UL * 60 * 60)

// Only blink the idle LED when the next send is this close
#define IDLE_BLINK_WINDOW_S 60

#define TX_DELAY_STEP_MS 50
#define TX_DELAY_MAX_MS  1000
#define TX_DELAY_COUNT   ((TX_DELAY_MAX_MS / TX_DELAY_STEP_MS) + 1)

#define TX_COUNT 6
static const char* const tx_count_text[TX_COUNT] = {
    "x1",
    "x2",
    "x3",
    "x4",
    "x5",
    "x6",
};

#define RADIO_DEVICE_COUNT 2
static const char* const radio_device_text[RADIO_DEVICE_COUNT] = {
    "Internal",
    "External",
};
