#pragma once

typedef enum {
    SchedulerStartEventSetInterval,
    SchedulerStartEventSetTiming,
    SchedulerStartEventSetTxCount,
    SchedulerStartEventSetImmediate,
    SchedulerStartEventSetTxDelay,
    SchedulerStartEventSetRadio,
    SchedulerStartEventSelectFile,
    SchedulerStartEventSaveSchedule,
    SchedulerStartRunEvent,
    SchedulerEventPopupDone,
    SchedulerCustomEventErrorBack,
    SchedulerEventIntervalDone,
    SchedulerStartEventRebuild,
    SchedulerStartEventEditLines,
} SchedulerCustomEvent;
