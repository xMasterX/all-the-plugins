#pragma once

#include "usb_floppy_app.h"
#include "scenes/usb_floppy_scene.h"
#include "helpers/floppy_usb.h"

#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <dialogs/dialogs.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/loading.h>
#include <gui/modules/widget.h>
#include <storage/storage.h>
#include "views/floppy_view.h"
#include <usb_floppy_icons.h>

#define USB_FLOPPY_APP_PATH_FOLDER STORAGE_APP_DATA_PATH_PREFIX
#define USB_FLOPPY_APP_EXTENSION   ".img"

struct UsbFloppyApp {
    Gui* gui;
    Storage* fs_api;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Widget* widget;
    DialogsApp* dialogs;
    VariableItemList* variable_item_list;
    Loading* loading;

    FuriString* file_path;
    File* file;
    FloppyView* floppy_view;

    FloppyUsb* usb;
    const UfiGeometry* geometry;
    FloppyTransport transport;
    bool write_protect;

    uint32_t bytes_read, bytes_written;
};

typedef enum {
    UsbFloppyAppViewStart,
    UsbFloppyAppViewWork,
    UsbFloppyAppViewLoading,
    UsbFloppyAppViewWidget,
} UsbFloppyAppView;

enum UsbFloppyCustomEvent {
    // Reserve first 100 events for button types and indexes, starting from 0
    UsbFloppyCustomEventReserved = 100,

    UsbFloppyCustomEventEject,
    UsbFloppyCustomEventConnectionError,
    UsbFloppyCustomEventFileSelect,
    UsbFloppyCustomEventAbout,
};

void usb_floppy_app_show_loading_popup(UsbFloppyApp* app, bool show);

// Checks the image at app->file_path and moves to the scene that fits it
void usb_floppy_app_open_image(UsbFloppyApp* app);

// Goes back to the file browser, or the start menu if the app was opened with a file
bool usb_floppy_app_back_to_menu(UsbFloppyApp* app);
