#pragma once

#include "scsi_cd.h"

typedef struct CdUsb CdUsb;
typedef void (*CdUsbConnectionStatusCallback)(bool connected, void* context);

CdUsb* cd_usb_start(CdDevice device);
void cd_usb_stop(CdUsb* cd_usb);
void cd_usb_set_connection_status_callback(
    CdUsb* cd_usb,
    CdUsbConnectionStatusCallback cb,
    void* context);
