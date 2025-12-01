#pragma once

#include <gui/gui.h>
#include <storage/storage.h>
#include <notification/notification.h>
#include "icon.h"

#define TAG             "IE"
#define APP_NAME        "IconEdit"
#define VERSION         "0.6"
#define ENABLE_XBM_SAVE // Enables XBM file save

typedef struct {
    InputEvent input;
} IconEditEvent;

// Panel determines what should process input
typedef enum {
    // main view components
    Panel_TabBar, // File, Tools, View, Anim
    // Panel_Tab,
    Panel_Canvas,

    Panel_File,
    Panel_Tools,
    Panel_Settings,
    Panel_Help,
    Panel_About,

    // modal panels
    Panel_Playback,
    Panel_SaveAs, // PNG, XBM, .C, BMX
    Panel_New, // dimension prompt
    Panel_FPS, // select FPS
    Panel_SendUSB,
    Panel_Dialog,
    Panel_SendAs,
} PanelType;

typedef void (*IconEditUpdateCallback)(void* context);

typedef enum {
    Setting_START,
    Setting_Include = Setting_START,
    Setting_Delete_Brace,
    Setting_COUNT,
    Setting_NONE,
} SettingType;
typedef struct {
    bool include_icon_header;
    bool delete_auto_brace;
} IESettings;

typedef struct {
    IEIcon* icon;

    PanelType panel; // the active panel currently reacting to input
    IESettings settings;

    FuriMutex* mutex;
    Storage* storage;
    NotificationApp* notify;
    FuriString* temp_str;
    char temp_cstr[64];
    bool running;
    bool dirty;
} IconEdit;
