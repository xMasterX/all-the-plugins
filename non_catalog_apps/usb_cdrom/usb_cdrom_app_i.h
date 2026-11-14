#pragma once

#include "usb_cdrom_app.h"
#include "scenes/usb_cdrom_scene.h"
#include "helpers/cd_usb.h"

#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <dialogs/dialogs.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/loading.h>
#include <gui/modules/widget.h>
#include <storage/storage.h>
#include "views/cd_view.h"
#include <usb_cdrom_icons.h>

#define USB_CDROM_APP_PATH_FOLDER STORAGE_APP_DATA_PATH_PREFIX
#define USB_CDROM_APP_EXTENSION   ".iso"

// storage_file_seek takes a 32 bit offset, so the last block must start below 4 GiB
#define USB_CDROM_MAX_BLOCKS (0x100000000ULL / CD_BLOCK_SIZE)

// a 99 minute CD, the biggest there is. Anything larger is a DVD.
#define USB_CDROM_CD_MAX_BLOCKS (99UL * 60 * 75)

typedef enum {
    UsbCdromDiscTypeAuto,
    UsbCdromDiscTypeCd,
    UsbCdromDiscTypeDvd,
    UsbCdromDiscTypeNum,
} UsbCdromDiscType;

struct UsbCdromApp {
    Gui* gui;
    Storage* fs_api;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Widget* widget;
    DialogsApp* dialogs;
    VariableItemList* variable_item_list;
    Loading* loading;

    FuriString* file_path;
    CdView* cd_view;

    CdUsb* usb;
    UsbCdromDiscType disc_type;

    // The tray, shared with the USB thread. disc_mutex guards everything up to bytes_read.
    FuriMutex* disc_mutex;
    File* file; // NULL when the tray is open
    uint32_t disc_blocks;
    bool disc_dvd;
    uint32_t disc_id; // bumped on every insert and eject
    bool locked; // the host has prevented medium removal
    bool eject_request; // waiting for the host to eject a locked disc
    uint32_t bytes_read;
};

typedef enum {
    UsbCdromAppViewStart,
    UsbCdromAppViewWork,
    UsbCdromAppViewLoading,
    UsbCdromAppViewWidget,
} UsbCdromAppView;

enum UsbCdromCustomEvent {
    // Reserve first 100 events for button types and indexes, starting from 0
    UsbCdromCustomEventReserved = 100,

    UsbCdromCustomEventTrayChanged,
    UsbCdromCustomEventTrayButton,
    UsbCdromCustomEventForceEject,
    UsbCdromCustomEventConnectionError,
    UsbCdromCustomEventFileSelect,
    UsbCdromCustomEventAbout,
};

void usb_cdrom_app_show_loading_popup(UsbCdromApp* app, bool show);

// Shows the SD card file browser. Returns true if an image was picked into app->file_path.
bool usb_cdrom_app_browse(UsbCdromApp* app);

// Size of the image at path in CD blocks, or 0 if it isn't a usable ISO
uint32_t usb_cdrom_app_image_blocks(UsbCdromApp* app, const char* path);

// Checks the image at app->file_path and moves to the scene that fits it
void usb_cdrom_app_open_image(UsbCdromApp* app);

// Goes back to the file browser, or the start menu if the app was opened with a file
bool usb_cdrom_app_back_to_menu(UsbCdromApp* app);
