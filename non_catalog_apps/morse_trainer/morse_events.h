#pragma once

/* Custom events routed through the ViewDispatcher into the SceneManager.
 * Values 0..49 are reserved for submenu item indexes. */
typedef enum {
    MTEventTeachOk = 50, /* replay current teach letter */
    MTEventTeachNext, /* advance to next teach letter / start quiz */
    MTEventKeyDot, /* player keyed a dot */
    MTEventKeyDash, /* player keyed a dash */
    MTEventKeyClear, /* Left: wipe current attempt */
    MTEventCommit, /* pause timer fired: evaluate keyed pattern */
    MTEventFeedbackDone, /* feedback display time elapsed */
    MTEventChoicePrev,
    MTEventChoiceNext,
    MTEventChoiceConfirm,
    MTEventReplay, /* Up: replay decode audio */
    MTEventHint, /* Down: eliminate two wrong choices */
    MTEventResultsOk, /* leave results screen */
} MTEvent;
