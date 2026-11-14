#pragma once

#include <storage/storage.h>
#include "ufi.h"

typedef enum {
    FloppyTransportCbi, // Control/Bulk/Interrupt, used by most real USB floppy drives
    FloppyTransportBot, // Bulk-Only
    FloppyTransportNum,
} FloppyTransport;

typedef struct FloppyUsb FloppyUsb;
typedef void (*FloppyUsbConnectionStatusCallback)(bool connected, void* context);

FloppyUsb* floppy_usb_start(const char* filename, FloppyTransport transport, UfiDevice device);
void floppy_usb_stop(FloppyUsb* floppy);
void floppy_usb_set_connection_status_callback(
    FloppyUsb* floppy,
    FloppyUsbConnectionStatusCallback cb,
    void* context);
