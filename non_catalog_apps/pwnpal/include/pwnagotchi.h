#pragma once

#include <furi.h>
#include <gui/canvas.h>
#include <stdbool.h>
#include <string.h>

#include "pwn_constants.h"

#define PWNAGOTCHI_HEIGHT FLIPPER_SCREEN_HEIGHT
#define PWNAGOTCHI_WIDTH FLIPPER_SCREEN_WIDTH
#define PWNAGOTCHI_FACE_I 25
#define PWNAGOTCHI_FACE_J 0
#define PWNAGOTCHI_NAME_I 17
#define PWNAGOTCHI_NAME_J 0
#define PWNAGOTCHI_CHANNEL_I 7
#define PWNAGOTCHI_CHANNEL_J 0
#define PWNAGOTCHI_APS_I 7
#define PWNAGOTCHI_APS_J 30
#define PWNAGOTCHI_UPTIME_I 7
// uptime is right-aligned (see pwnagotchi_draw_uptime), so no fixed J
#define PWNAGOTCHI_LINE1_START_I 8
#define PWNAGOTCHI_LINE1_START_J 0
#define PWNAGOTCHI_LINE1_END_I 8
#define PWNAGOTCHI_LINE1_END_J 127
#define PWNAGOTCHI_LINE2_START_I 54
#define PWNAGOTCHI_LINE2_START_J 0
#define PWNAGOTCHI_LINE2_END_I 54
#define PWNAGOTCHI_LINE2_END_J 127
#define PWNAGOTCHI_HANDSHAKES_I 63
#define PWNAGOTCHI_HANDSHAKES_J 0
#define PWNAGOTCHI_FRIEND_FACE_I 52
#define PWNAGOTCHI_FRIEND_FACE_J 3
#define PWNAGOTCHI_FRIEND_STAT_I 52
#define PWNAGOTCHI_FRIEND_STAT_J 24
#define PWNAGOTCHI_MODE_AI_I 63
#define PWNAGOTCHI_MODE_AI_J 121
#define PWNAGOTCHI_MODE_AUTO_I 63
#define PWNAGOTCHI_MODE_AUTO_J 105
#define PWNAGOTCHI_MODE_MANU_I 63
#define PWNAGOTCHI_MODE_MANU_J 103
#define PWNAGOTCHI_MESSAGE_I 17
#define PWNAGOTCHI_MESSAGE_J 60

#define PWNAGOTCHI_FONT FontSecondary

// faces, kept local rather than transmitted each time
enum PwnagotchiFace {
    NoFace = 0,
    DefaultFace,
    Look_r,
    Look_l,
    Look_r_happy,
    Look_l_happy,
    Sleep,
    Sleep2,
    Awake,
    Bored,
    Intense,
    Cool,
    Happy,
    Grateful,
    Excited,
    Motivated,
    Demotivated,
    Smart,
    Lonely,
    Sad,
    Angry,
    Friend,
    Broken,
    Debug,
    Upload,
    Upload1,
    Upload2
};

enum PwnagotchiMode { PwnMode_Auto, PwnMode_Ai, PwnMode_Manual };

typedef struct {
    enum PwnagotchiFace face;
    FuriString* channel;
    FuriString* apStat;
    FuriString* uptime;
    FuriString* hostname;
    FuriString* message;
    FuriString* handshakes; // last ssid + handshake info (bottom)
    enum PwnagotchiMode mode;
    FuriString* friendStat; // friend name + aps
} Pwnagotchi;

Pwnagotchi* pwnagotchi_alloc();
void pwnagotchi_free(Pwnagotchi* pwn);
void pwnagotchi_draw_blank(Pwnagotchi* pwn, Canvas* canvas);
void pwnagotchi_draw_face(Pwnagotchi* pwn, Canvas* canvas);
void pwnagotchi_draw_name(Pwnagotchi* pwn, Canvas* canvas);
void pwnagotchi_draw_channel(Pwnagotchi* pwn, Canvas* canvas);
void pwnagotchi_draw_aps(Pwnagotchi* pwn, Canvas* canvas);
void pwnagotchi_draw_uptime(Pwnagotchi* pwn, Canvas* canvas);
void pwnagotchi_draw_lines(Pwnagotchi* pwn, Canvas* canvas);
void pwnagotchi_draw_friend(Pwnagotchi* pwn, Canvas* canvas);
void pwnagotchi_draw_mode(Pwnagotchi* pwn, Canvas* canvas);
void pwnagotchi_draw_handshakes(Pwnagotchi* pwn, Canvas* canvas);
void pwnagotchi_draw_message(Pwnagotchi* pwn, Canvas* canvas);
void pwnagotchi_draw_all(Pwnagotchi* pwn, Canvas* canvas);
void pwnagotchi_screen_clear(Pwnagotchi* pwn);
