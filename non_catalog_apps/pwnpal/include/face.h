#pragma once

#include <gui/canvas.h>

// Face indices are the canonical pwnagotchi face order, matched by the
// ESP32 side's FACE_GLYPHS table, so a single index selects the same face on the
// Flipper screen and in the advertised pwngrid persona (-f on the wire).
typedef enum {
    FaceNone = 0,
    FaceDefault,
    FaceLookR,
    FaceLookL,
    FaceLookRHappy,
    FaceLookLHappy,
    FaceSleep,
    FaceSleep2,
    FaceAwake,
    FaceBored,
    FaceIntense,
    FaceCool,
    FaceHappy,
    FaceGrateful,
    FaceExcited,
    FaceMotivated,
    FaceDemotivated,
    FaceSmart,
    FaceLonely,
    FaceSad,
    FaceAngry,
    FaceFriend,
    FaceBroken,
    FaceDebug,
    FaceUpload,
    FaceUpload1,
    FaceUpload2,
} Face;

// Draw the given face's icon at (x, y) (top-left of the ~30px face bitmap).
void face_draw(Canvas* canvas, Face face, int x, int y);
