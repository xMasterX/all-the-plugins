#include "floppy_usb.h"
#include <furi_hal.h>

#define TAG "UsbFloppyUsb"

#define USB_MSC_RX_EP  (0x01)
#define USB_MSC_TX_EP  (0x82)
#define USB_MSC_INT_EP (0x83)

#define USB_MSC_RX_EP_SIZE  (64UL)
#define USB_MSC_TX_EP_SIZE  (64UL)
#define USB_MSC_INT_EP_SIZE (2UL)

#define USB_MSC_SUBCLASS_UFI (0x04)
#define USB_MSC_PROTO_CBI    (0x00) // with command completion interrupt
#define USB_MSC_PROTO_BOT    (0x50)

#define USB_MSC_CBI_ADSC        (0x00)
#define USB_MSC_BOT_GET_MAX_LUN (0xFE)
#define USB_MSC_BOT_RESET       (0xFF)

#define CBW_SIG                  (0x43425355)
#define CBW_FLAGS_DEVICE_TO_HOST (0x80)

#define CSW_SIG                (0x53425355)
#define CSW_STATUS_OK          (0)
#define CSW_STATUS_NOK         (1)
#define CSW_STATUS_PHASE_ERROR (2)

// must be UFI_BLOCK_SIZE aligned. A whole 1.44M track is 9K.
#define USB_MSC_BUF_MAX (0x4000UL)

static usbd_respond usb_ep_config(usbd_device* dev, uint8_t cfg);
static usbd_respond usb_control(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback);

typedef enum {
    EventExit = 1 << 0,
    EventReset = 1 << 1,
    EventRxTx = 1 << 2,
    EventCommand = 1 << 3,

    EventAll = EventExit | EventReset | EventRxTx | EventCommand,
} FloppyUsbEvent;

typedef struct {
    uint32_t sig;
    uint32_t tag;
    uint32_t len;
    uint8_t flags;
    uint8_t lun;
    uint8_t cmd_len;
    uint8_t cmd[16];
} __attribute__((packed)) CBW;

typedef struct {
    uint32_t sig;
    uint32_t tag;
    uint32_t residue;
    uint8_t status;
} __attribute__((packed)) CSW;

struct FloppyUsbDescriptor {
    struct usb_config_descriptor config;
    struct usb_interface_descriptor intf;
    struct usb_endpoint_descriptor ep_rx;
    struct usb_endpoint_descriptor ep_tx;
    struct usb_endpoint_descriptor ep_int; // CBI only
} __attribute__((packed));

struct FloppyUsb {
    FuriHalUsbInterface usb;
    FuriHalUsbInterface* usb_prev;
    struct FloppyUsbDescriptor cfg_descr;

    FuriThread* thread;
    usbd_device* dev;
    FloppyTransport transport;
    UfiDevice device;

    // CBI command block, written by the ADSC control request
    uint8_t cdb[UFI_CDB_LEN];
    uint8_t cdb_len;

    bool connected;
    FloppyUsbConnectionStatusCallback status_cb;
    void* status_cb_context;
};

static int32_t floppy_thread_worker(void* context) {
    FloppyUsb* floppy = context;
    usbd_device* dev = floppy->dev;
    bool cbi = floppy->transport == FloppyTransportCbi;
    UfiSession ufi = {
        .dev = floppy->device,
    };
    CBW cbw = {0};
    CSW csw = {0};
    uint8_t cdb[UFI_CDB_LEN];
    uint8_t cbi_status[2] = {0};
    bool cmd_pending = false;
    bool cmd_ok = false;
    UfiDataDir dir = UfiDataNone;
    uint8_t* buf = NULL;
    uint32_t buf_len = 0, buf_cap = 0, buf_sent = 0;
    uint32_t xfer_len = 0; // bytes left in the data stage
    enum {
        StateReadCommand,
        StateReadData,
        StateWriteData,
        StateBuildStatus,
        StateWriteStatus,
    } state = StateReadCommand;
    while(true) {
        uint32_t flags = furi_thread_flags_wait(EventAll, FuriFlagWaitAny, FuriWaitForever);
        if(flags & EventExit) {
            FURI_LOG_D(TAG, "exit");
            break;
        }
        if(flags & EventReset) {
            FURI_LOG_D(TAG, "reset");
            ufi.sk = 0;
            ufi.asc = 0;
            ufi.ascq = 0;
            memset(&cbw, 0, sizeof(cbw));
            memset(&csw, 0, sizeof(csw));
            if(buf) {
                free(buf);
                buf = NULL;
            }
            buf_len = buf_cap = buf_sent = 0;
            cmd_pending = false;
            state = StateReadCommand;
        }
        if(flags & EventCommand) {
            cmd_pending = true;
        }
        if(flags & (EventRxTx | EventCommand)) do {
                switch(state) {
                case StateReadCommand: {
                    FURI_LOG_T(TAG, "StateReadCommand");
                    uint8_t cmd_len;
                    if(cbi) {
                        if(!cmd_pending) break;
                        cmd_pending = false;
                        cmd_len = floppy->cdb_len;
                        memcpy(cdb, floppy->cdb, sizeof(cdb));
                    } else {
                        int32_t len = usbd_ep_read(dev, USB_MSC_RX_EP, &cbw, sizeof(cbw));
                        if(len <= 0) {
                            FURI_LOG_T(TAG, "cbw not ready");
                            break;
                        }
                        if(len != sizeof(cbw) || cbw.sig != CBW_SIG) {
                            FURI_LOG_W(TAG, "bad cbw sig=%08lx", cbw.sig);
                            usbd_ep_stall(dev, USB_MSC_TX_EP);
                            usbd_ep_stall(dev, USB_MSC_RX_EP);
                            continue;
                        }
                        cmd_len = MIN(cbw.cmd_len, sizeof(cbw.cmd));
                        memcpy(cdb, cbw.cmd, cmd_len);
                    }
                    if(!floppy->connected) floppy->connected = true;

                    cmd_ok = ufi_cmd_start(&ufi, cdb, cmd_len);
                    if(cbi) {
                        // CBI has no wrapper, so the CDB decides the data stage
                        dir = ufi.data_dir;
                        xfer_len = ufi.data_len;
                    } else {
                        dir = (cbw.flags & CBW_FLAGS_DEVICE_TO_HOST) ? UfiDataIn : UfiDataOut;
                        xfer_len = cbw.len;
                    }
                    if(!cmd_ok) {
                        FURI_LOG_W(TAG, "cmd %02X rejected", cdb[0]);
                        if(xfer_len) {
                            usbd_ep_stall(dev, dir == UfiDataIn ? USB_MSC_TX_EP : USB_MSC_RX_EP);
                        }
                        state = StateBuildStatus;
                        continue;
                    }
                    buf_len = 0;
                    buf_sent = 0;
                    state = (dir == UfiDataIn) ? StateWriteData : StateReadData;
                    continue;
                }; break;
                case StateReadData: {
                    FURI_LOG_T(TAG, "StateReadData %lu/%lu", buf_len, xfer_len);
                    if(!xfer_len) {
                        state = StateBuildStatus;
                        continue;
                    }
                    uint32_t buf_clamp = MIN(xfer_len, USB_MSC_BUF_MAX);
                    if(buf_clamp > buf_cap) {
                        FURI_LOG_T(TAG, "growing buf %lu -> %lu", buf_cap, buf_clamp);
                        if(buf) {
                            free(buf);
                        }
                        buf_cap = buf_clamp;
                        buf = malloc(buf_cap);
                    }
                    if(buf_len < buf_clamp) {
                        int32_t len =
                            usbd_ep_read(dev, USB_MSC_RX_EP, buf + buf_len, buf_clamp - buf_len);
                        if(len < 0) {
                            FURI_LOG_T(TAG, "rx not ready %ld", len);
                            break;
                        }
                        buf_len += len;
                    }
                    if(buf_len == buf_clamp) {
                        if(!ufi_cmd_rx_data(&ufi, buf, buf_len)) {
                            FURI_LOG_W(TAG, "rx data failed");
                            usbd_ep_stall(dev, USB_MSC_RX_EP);
                            cmd_ok = false;
                            state = StateBuildStatus;
                            continue;
                        }
                        xfer_len -= buf_len;
                        buf_len = 0;
                    }
                    continue;
                }; break;
                case StateWriteData: {
                    FURI_LOG_T(TAG, "StateWriteData %lu", xfer_len);
                    if(!xfer_len) {
                        state = StateBuildStatus;
                        continue;
                    }
                    uint32_t buf_clamp = MIN(xfer_len, USB_MSC_BUF_MAX);
                    if(buf_clamp > buf_cap) {
                        FURI_LOG_T(TAG, "growing buf %lu -> %lu", buf_cap, buf_clamp);
                        if(buf) {
                            free(buf);
                        }
                        buf_cap = buf_clamp;
                        buf = malloc(buf_cap);
                    }
                    if(!buf_len && !ufi_cmd_tx_data(&ufi, buf, &buf_len, buf_clamp)) {
                        if(ufi.tx_done) {
                            // host asked for more than the response holds
                            FURI_LOG_T(TAG, "short tx");
                        } else {
                            FURI_LOG_W(TAG, "tx data failed");
                            usbd_ep_stall(dev, USB_MSC_TX_EP);
                            cmd_ok = false;
                        }
                        state = StateBuildStatus;
                        continue;
                    }
                    int32_t len = usbd_ep_write(
                        dev,
                        USB_MSC_TX_EP,
                        buf + buf_sent,
                        MIN(USB_MSC_TX_EP_SIZE, buf_len - buf_sent));
                    if(len < 0) {
                        FURI_LOG_T(TAG, "tx not ready %ld", len);
                        break;
                    }
                    buf_sent += len;
                    if(buf_sent == buf_len) {
                        xfer_len -= buf_len;
                        buf_len = 0;
                        buf_sent = 0;
                    }
                    continue;
                }; break;
                case StateBuildStatus: {
                    FURI_LOG_T(TAG, "StateBuildStatus");
                    bool ok = cmd_ok && ufi_cmd_end(&ufi);
                    if(cbi) {
                        // UFI interrupt data block: ASC, ASCQ. Zero means success.
                        cbi_status[0] = ok ? 0 : ufi.asc;
                        cbi_status[1] = ok ? 0 : ufi.ascq;
                    } else {
                        csw.sig = CSW_SIG;
                        csw.tag = cbw.tag;
                        csw.status = ok ? CSW_STATUS_OK : CSW_STATUS_NOK;
                        csw.residue = xfer_len;
                    }
                    if(!ok) {
                        FURI_LOG_W(
                            TAG, "cmd %02X failed sk=%02X asc=%02X", ufi.cmd[0], ufi.sk, ufi.asc);
                    }
                    state = StateWriteStatus;
                    continue;
                }; break;
                case StateWriteStatus: {
                    FURI_LOG_T(TAG, "StateWriteStatus");
                    int32_t len;
                    if(cbi) {
                        len = usbd_ep_write(dev, USB_MSC_INT_EP, cbi_status, sizeof(cbi_status));
                    } else {
                        len = usbd_ep_write(dev, USB_MSC_TX_EP, &csw, sizeof(csw));
                    }
                    if(len < 0) {
                        FURI_LOG_T(TAG, "status not ready");
                        break;
                    }
                    memset(&cbw, 0, sizeof(cbw));
                    memset(&csw, 0, sizeof(csw));
                    state = StateReadCommand;
                    continue;
                }; break;
                }
                break;
            } while(true);
    }
    if(buf) {
        free(buf);
    }
    return 0;
}

// needed in usb_deinit, usb_suspend, usb_rxtx_ep_callback, usb_control,
// where if_ctx isn't passed
static FloppyUsb* floppy_cur = NULL;

static void usb_init(usbd_device* dev, FuriHalUsbInterface* intf, void* ctx) {
    UNUSED(intf);
    FloppyUsb* floppy = ctx;
    floppy_cur = floppy;
    floppy->dev = dev;
    floppy->connected = false;

    usbd_reg_config(dev, usb_ep_config);
    usbd_reg_control(dev, usb_control);
    usbd_connect(dev, true);

    furi_thread_start(floppy->thread);
}

static void usb_deinit(usbd_device* dev) {
    usbd_reg_config(dev, NULL);
    usbd_reg_control(dev, NULL);

    FloppyUsb* floppy = floppy_cur;
    if(!floppy || floppy->dev != dev) {
        FURI_LOG_E(TAG, "deinit floppy_cur leak");
        return;
    }
    floppy_cur = NULL;

    if(floppy->thread) {
        furi_thread_flags_set(furi_thread_get_id(floppy->thread), EventExit);
        furi_thread_join(floppy->thread);
    }

    if(!floppy->connected && floppy->status_cb) {
        floppy->status_cb(false, floppy->status_cb_context);
    }
}

static void usb_wakeup(usbd_device* dev) {
    UNUSED(dev);
}

static void usb_suspend(usbd_device* dev) {
    FloppyUsb* floppy = floppy_cur;
    if(!floppy || floppy->dev != dev) return;
    furi_thread_flags_set(furi_thread_get_id(floppy->thread), EventReset);
}

static void usb_rxtx_ep_callback(usbd_device* dev, uint8_t event, uint8_t ep) {
    UNUSED(ep);
    UNUSED(event);
    FloppyUsb* floppy = floppy_cur;
    if(!floppy || floppy->dev != dev) return;
    furi_thread_flags_set(furi_thread_get_id(floppy->thread), EventRxTx);
}

static usbd_respond usb_ep_config(usbd_device* dev, uint8_t cfg) {
    FloppyUsb* floppy = floppy_cur;
    bool cbi = floppy && floppy->transport == FloppyTransportCbi;
    switch(cfg) {
    case 0: // deconfig
        usbd_ep_deconfig(dev, USB_MSC_RX_EP);
        usbd_ep_deconfig(dev, USB_MSC_TX_EP);
        usbd_reg_endpoint(dev, USB_MSC_RX_EP, NULL);
        usbd_reg_endpoint(dev, USB_MSC_TX_EP, NULL);
        if(cbi) {
            usbd_ep_deconfig(dev, USB_MSC_INT_EP);
            usbd_reg_endpoint(dev, USB_MSC_INT_EP, NULL);
        }
        return usbd_ack;
    case 1: // config
        usbd_ep_config(dev, USB_MSC_RX_EP, USB_EPTYPE_BULK, USB_MSC_RX_EP_SIZE);
        usbd_ep_config(dev, USB_MSC_TX_EP, USB_EPTYPE_BULK, USB_MSC_TX_EP_SIZE);
        usbd_reg_endpoint(dev, USB_MSC_RX_EP, usb_rxtx_ep_callback);
        usbd_reg_endpoint(dev, USB_MSC_TX_EP, usb_rxtx_ep_callback);
        if(cbi) {
            usbd_ep_config(dev, USB_MSC_INT_EP, USB_EPTYPE_INTERRUPT, USB_MSC_INT_EP_SIZE);
            usbd_reg_endpoint(dev, USB_MSC_INT_EP, usb_rxtx_ep_callback);
        }
        return usbd_ack;
    }
    return usbd_fail;
}

static usbd_respond usb_control(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback) {
    UNUSED(callback);
    if(((USB_REQ_RECIPIENT | USB_REQ_TYPE) & req->bmRequestType) !=
       (USB_REQ_INTERFACE | USB_REQ_CLASS)) {
        return usbd_fail;
    }
    FloppyUsb* floppy = floppy_cur;
    if(!floppy || floppy->dev != dev) return usbd_fail;

    if(floppy->transport == FloppyTransportCbi) {
        if(req->bRequest != USB_MSC_CBI_ADSC || (req->bmRequestType & USB_REQ_DEVTOHOST) ||
           !req->wLength || req->wLength > sizeof(floppy->cdb)) {
            return usbd_fail;
        }
        // CBI command block reset is a SEND DIAGNOSTIC CDB of 1D 04 FF FF ...
        if(req->data[0] == 0x1D && req->data[1] == 0x04) {
            furi_thread_flags_set(furi_thread_get_id(floppy->thread), EventReset);
            return usbd_ack;
        }
        memset(floppy->cdb, 0, sizeof(floppy->cdb));
        memcpy(floppy->cdb, req->data, req->wLength);
        floppy->cdb_len = req->wLength;
        furi_thread_flags_set(furi_thread_get_id(floppy->thread), EventCommand);
        return usbd_ack;
    }

    switch(req->bRequest) {
    case USB_MSC_BOT_GET_MAX_LUN: {
        static uint8_t max_lun = 0;
        dev->status.data_ptr = &max_lun;
        dev->status.data_count = 1;
        return usbd_ack;
    }; break;
    case USB_MSC_BOT_RESET: {
        furi_thread_flags_set(furi_thread_get_id(floppy->thread), EventReset);
        return usbd_ack;
    }; break;
    }
    return usbd_fail;
}

static const struct usb_string_descriptor dev_manuf_desc = USB_STRING_DESC("Flipper Devices Inc.");

static const struct usb_device_descriptor usb_floppy_dev_descr = {
    .bLength = sizeof(struct usb_device_descriptor),
    .bDescriptorType = USB_DTYPE_DEVICE,
    .bcdUSB = VERSION_BCD(2, 0, 0),
    .bDeviceClass = USB_CLASS_PER_INTERFACE,
    .bDeviceSubClass = USB_SUBCLASS_NONE,
    .bDeviceProtocol = USB_PROTO_NONE,
    .bMaxPacketSize0 = 8, // USB_EP0_SIZE
    .idVendor = 0x0483,
    .idProduct = 0x5720,
    .bcdDevice = VERSION_BCD(2, 0, 0),
    .iManufacturer = 1, // UsbDevManuf
    .iProduct = 2, // UsbDevProduct
    .iSerialNumber = 3, // UsbDevSerial
    .bNumConfigurations = 1,
};

static const struct FloppyUsbDescriptor usb_floppy_cfg_descr = {
    .config =
        {
            .bLength = sizeof(struct usb_config_descriptor),
            .bDescriptorType = USB_DTYPE_CONFIGURATION,
            .wTotalLength = sizeof(struct FloppyUsbDescriptor),
            .bNumInterfaces = 1,
            .bConfigurationValue = 1,
            .iConfiguration = NO_DESCRIPTOR,
            .bmAttributes = USB_CFG_ATTR_RESERVED | USB_CFG_ATTR_SELFPOWERED,
            .bMaxPower = USB_CFG_POWER_MA(100),
        },
    .intf =
        {
            .bLength = sizeof(struct usb_interface_descriptor),
            .bDescriptorType = USB_DTYPE_INTERFACE,
            .bInterfaceNumber = 0,
            .bAlternateSetting = 0,
            .bNumEndpoints = 3,
            .bInterfaceClass = USB_CLASS_MASS_STORAGE,
            .bInterfaceSubClass = USB_MSC_SUBCLASS_UFI,
            .bInterfaceProtocol = USB_MSC_PROTO_CBI,
            .iInterface = NO_DESCRIPTOR,
        },
    .ep_rx =
        {
            .bLength = sizeof(struct usb_endpoint_descriptor),
            .bDescriptorType = USB_DTYPE_ENDPOINT,
            .bEndpointAddress = USB_MSC_RX_EP,
            .bmAttributes = USB_EPTYPE_BULK,
            .wMaxPacketSize = USB_MSC_RX_EP_SIZE,
            .bInterval = 0,
        },
    .ep_tx =
        {
            .bLength = sizeof(struct usb_endpoint_descriptor),
            .bDescriptorType = USB_DTYPE_ENDPOINT,
            .bEndpointAddress = USB_MSC_TX_EP,
            .bmAttributes = USB_EPTYPE_BULK,
            .wMaxPacketSize = USB_MSC_TX_EP_SIZE,
            .bInterval = 0,
        },
    .ep_int =
        {
            .bLength = sizeof(struct usb_endpoint_descriptor),
            .bDescriptorType = USB_DTYPE_ENDPOINT,
            .bEndpointAddress = USB_MSC_INT_EP,
            .bmAttributes = USB_EPTYPE_INTERRUPT,
            .wMaxPacketSize = USB_MSC_INT_EP_SIZE,
            .bInterval = 1, // ms, the host polls this once per command
        },
};

FloppyUsb* floppy_usb_start(const char* filename, FloppyTransport transport, UfiDevice device) {
    FloppyUsb* floppy = malloc(sizeof(FloppyUsb));
    floppy->transport = transport;
    floppy->device = device;

    floppy->cfg_descr = usb_floppy_cfg_descr;
    if(transport == FloppyTransportBot) {
        // Bulk-Only drops the trailing interrupt endpoint
        floppy->cfg_descr.config.wTotalLength -= sizeof(struct usb_endpoint_descriptor);
        floppy->cfg_descr.intf.bNumEndpoints = 2;
        floppy->cfg_descr.intf.bInterfaceProtocol = USB_MSC_PROTO_BOT;
    }

    floppy->usb_prev = furi_hal_usb_get_config();
    floppy->usb.init = usb_init;
    floppy->usb.deinit = usb_deinit;
    floppy->usb.wakeup = usb_wakeup;
    floppy->usb.suspend = usb_suspend;
    floppy->usb.dev_descr = (struct usb_device_descriptor*)&usb_floppy_dev_descr;
    floppy->usb.str_manuf_descr = (void*)&dev_manuf_desc;
    floppy->usb.str_prod_descr = NULL;
    floppy->usb.str_serial_descr = NULL;
    floppy->usb.cfg_descr = (void*)&floppy->cfg_descr;

    const char* name = furi_hal_version_get_device_name_ptr();
    if(!name) name = "Flipper Zero";
    size_t len = strlen(name);
    struct usb_string_descriptor* str_prod_descr = malloc(len * 2 + 2);
    str_prod_descr->bLength = len * 2 + 2;
    str_prod_descr->bDescriptorType = USB_DTYPE_STRING;
    for(uint8_t i = 0; i < len; i++)
        str_prod_descr->wString[i] = name[i];
    floppy->usb.str_prod_descr = str_prod_descr;

    len = strlen(filename);
    struct usb_string_descriptor* str_serial_descr = malloc(len * 2 + 2);
    str_serial_descr->bLength = len * 2 + 2;
    str_serial_descr->bDescriptorType = USB_DTYPE_STRING;
    for(uint8_t i = 0; i < len; i++)
        str_serial_descr->wString[i] = filename[i];
    floppy->usb.str_serial_descr = str_serial_descr;

    floppy->thread = furi_thread_alloc_ex("UsbFloppyUsb", 2048, floppy_thread_worker, floppy);
    if(!furi_hal_usb_set_config(&floppy->usb, floppy)) {
        FURI_LOG_E(TAG, "USB locked, cannot start USB Floppy");
        furi_thread_free(floppy->thread);
        free(floppy->usb.str_prod_descr);
        free(floppy->usb.str_serial_descr);
        free(floppy);
        return NULL;
    }
    return floppy;
}

void floppy_usb_stop(FloppyUsb* floppy) {
    furi_hal_usb_set_config(floppy->usb_prev, NULL);
    furi_thread_free(floppy->thread);
    free(floppy->usb.str_prod_descr);
    free(floppy->usb.str_serial_descr);
    free(floppy);
}

void floppy_usb_set_connection_status_callback(
    FloppyUsb* floppy,
    FloppyUsbConnectionStatusCallback cb,
    void* context) {
    floppy->status_cb = cb;
    floppy->status_cb_context = context;
}
