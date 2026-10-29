#include "cd_usb.h"
#include <furi_hal.h>

#define TAG "UsbCdromUsb"

#define USB_MSC_RX_EP (0x01)
#define USB_MSC_TX_EP (0x82)

#define USB_MSC_RX_EP_SIZE (64UL)
#define USB_MSC_TX_EP_SIZE (64UL)

#define USB_MSC_SUBCLASS_SCSI (0x06) // SCSI transparent command set
#define USB_MSC_PROTO_BOT     (0x50)

#define USB_MSC_BOT_GET_MAX_LUN (0xFE)
#define USB_MSC_BOT_RESET       (0xFF)

#define CBW_SIG                  (0x43425355)
#define CBW_FLAGS_DEVICE_TO_HOST (0x80)

#define CSW_SIG                (0x53425355)
#define CSW_STATUS_OK          (0)
#define CSW_STATUS_NOK         (1)
#define CSW_STATUS_PHASE_ERROR (2)

// must be CD_BLOCK_SIZE aligned
#define USB_MSC_BUF_MAX (0x4000UL)

// how often a status held back by a stalled endpoint is retried
#define USB_MSC_STATUS_RETRY_MS (10)

static usbd_respond usb_ep_config(usbd_device* dev, uint8_t cfg);
static usbd_respond usb_control(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback);

typedef enum {
    EventExit = 1 << 0,
    EventReset = 1 << 1,
    EventRxTx = 1 << 2,

    EventAll = EventExit | EventReset | EventRxTx,
} CdUsbEvent;

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

struct CdUsbDescriptor {
    struct usb_config_descriptor config;
    struct usb_interface_descriptor intf;
    struct usb_endpoint_descriptor ep_rx;
    struct usb_endpoint_descriptor ep_tx;
} __attribute__((packed));

struct CdUsb {
    FuriHalUsbInterface usb;
    FuriHalUsbInterface* usb_prev;

    FuriThread* thread;
    usbd_device* dev;
    CdDevice device;

    bool connected;
    CdUsbConnectionStatusCallback status_cb;
    void* status_cb_context;
};

static void cd_usb_stall_data(usbd_device* dev, CdDataDir dir) {
    if(dir == CdDataIn) {
        usbd_ep_stall(dev, USB_MSC_TX_EP);
    } else if(dir == CdDataOut) {
        usbd_ep_stall(dev, USB_MSC_RX_EP);
    }
}

static int32_t cd_thread_worker(void* context) {
    CdUsb* cd_usb = context;
    usbd_device* dev = cd_usb->dev;
    CdSession scsi;
    scsi_cd_init(&scsi, cd_usb->device);
    CBW cbw = {0};
    CSW csw = {0};
    uint8_t status = CSW_STATUS_OK;
    CdDataDir host_dir = CdDataNone;
    uint8_t* buf = malloc(USB_MSC_BUF_MAX);
    uint32_t buf_len = 0, buf_sent = 0;
    uint32_t data_len = 0; // bytes the device moves in the data stage
    uint32_t data_left = 0; // of those, bytes not moved yet
    uint32_t residue = 0; // bytes the host asked for that the device won't move
    enum {
        StateReadCommand,
        StateReadData,
        StateWriteData,
        StateBuildStatus,
        StateWriteStatus,
    } state = StateReadCommand;
    while(true) {
        uint32_t flags = furi_thread_flags_wait(
            EventAll,
            FuriFlagWaitAny,
            state == StateWriteStatus ? USB_MSC_STATUS_RETRY_MS : FuriWaitForever);
        if(flags & FuriFlagError) {
            // timed out waiting for the host to clear a stall, try the status again
            flags = EventRxTx;
        }
        if(flags & EventExit) {
            FURI_LOG_D(TAG, "exit");
            break;
        }
        if(flags & EventReset) {
            FURI_LOG_D(TAG, "reset");
            scsi.sk = 0;
            scsi.asc = 0;
            scsi.ascq = 0;
            memset(&cbw, 0, sizeof(cbw));
            memset(&csw, 0, sizeof(csw));
            buf_len = buf_sent = 0;
            state = StateReadCommand;
        }
        if(flags & EventRxTx) do {
                switch(state) {
                case StateReadCommand: {
                    FURI_LOG_T(TAG, "StateReadCommand");
                    int32_t len = usbd_ep_read(dev, USB_MSC_RX_EP, &cbw, sizeof(cbw));
                    if(len <= 0) {
                        FURI_LOG_T(TAG, "cbw not ready");
                        break;
                    }
                    if(len != sizeof(cbw) || cbw.sig != CBW_SIG) {
                        // stays stalled until the host runs reset recovery
                        FURI_LOG_W(TAG, "bad cbw sig=%08lx", cbw.sig);
                        usbd_ep_stall(dev, USB_MSC_TX_EP);
                        usbd_ep_stall(dev, USB_MSC_RX_EP);
                        continue;
                    }
                    if(!cd_usb->connected) cd_usb->connected = true;

                    bool cmd_ok =
                        scsi_cd_cmd_start(&scsi, cbw.cmd, MIN(cbw.cmd_len, sizeof(cbw.cmd)));
                    host_dir = !cbw.len                               ? CdDataNone :
                               (cbw.flags & CBW_FLAGS_DEVICE_TO_HOST) ? CdDataIn :
                                                                        CdDataOut;
                    data_len = cmd_ok ? scsi.data_len : 0;
                    CdDataDir dev_dir = data_len ? scsi.data_dir : CdDataNone;
                    status = cmd_ok ? CSW_STATUS_OK : CSW_STATUS_NOK;
                    residue = cbw.len;
                    data_left = 0;
                    buf_len = buf_sent = 0;
                    state = StateBuildStatus;

                    if(!cmd_ok) {
                        FURI_LOG_W(TAG, "cmd %02X rejected", cbw.cmd[0]);
                        cd_usb_stall_data(dev, host_dir);
                    } else if(dev_dir == CdDataNone) {
                        // the host expected data the command doesn't have
                        cd_usb_stall_data(dev, host_dir);
                    } else if(dev_dir != host_dir || data_len > cbw.len) {
                        FURI_LOG_W(
                            TAG,
                            "phase error cmd=%02X dev=%u/%lu host=%u/%lu",
                            cbw.cmd[0],
                            dev_dir,
                            data_len,
                            host_dir,
                            cbw.len);
                        status = CSW_STATUS_PHASE_ERROR;
                        cd_usb_stall_data(dev, host_dir);
                    } else {
                        data_left = data_len;
                        residue = cbw.len - data_len;
                        state = (dev_dir == CdDataIn) ? StateWriteData : StateReadData;
                    }
                    continue;
                }; break;
                case StateReadData: {
                    FURI_LOG_T(TAG, "StateReadData %lu/%lu", buf_len, data_left);
                    if(!data_left) {
                        // the host has more to send than the command takes
                        if(residue) usbd_ep_stall(dev, USB_MSC_RX_EP);
                        state = StateBuildStatus;
                        continue;
                    }
                    uint32_t want = MIN(data_left, USB_MSC_BUF_MAX);
                    if(buf_len < want) {
                        int32_t len =
                            usbd_ep_read(dev, USB_MSC_RX_EP, buf + buf_len, want - buf_len);
                        if(len < 0) {
                            FURI_LOG_T(TAG, "rx not ready %ld", len);
                            break;
                        }
                        buf_len += len;
                    }
                    if(buf_len == want) {
                        if(!scsi_cd_cmd_rx_data(&scsi, buf, buf_len)) {
                            FURI_LOG_W(TAG, "rx data failed");
                            usbd_ep_stall(dev, USB_MSC_RX_EP);
                            status = CSW_STATUS_NOK;
                            residue += data_left;
                            state = StateBuildStatus;
                            continue;
                        }
                        data_left -= buf_len;
                        buf_len = 0;
                    }
                    continue;
                }; break;
                case StateWriteData: {
                    FURI_LOG_T(TAG, "StateWriteData %lu", data_left);
                    if(!data_left) {
                        // A short packet already ended the data stage. If the last one was
                        // full size the host is still waiting for more, so stall to end it.
                        if(residue && data_len % USB_MSC_TX_EP_SIZE == 0) {
                            usbd_ep_stall(dev, USB_MSC_TX_EP);
                        }
                        state = StateBuildStatus;
                        continue;
                    }
                    if(!buf_len && !scsi_cd_cmd_tx_data(
                                       &scsi, buf, &buf_len, MIN(data_left, USB_MSC_BUF_MAX))) {
                        FURI_LOG_W(TAG, "tx data failed");
                        usbd_ep_stall(dev, USB_MSC_TX_EP);
                        status = CSW_STATUS_NOK;
                        residue += data_left;
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
                    data_left -= len;
                    if(buf_sent == buf_len) {
                        buf_len = 0;
                        buf_sent = 0;
                    }
                    // the TX callback brings the next packet
                    break;
                }; break;
                case StateBuildStatus: {
                    FURI_LOG_T(TAG, "StateBuildStatus");
                    if(status == CSW_STATUS_OK && !scsi_cd_cmd_end(&scsi)) {
                        status = CSW_STATUS_NOK;
                    }
                    if(status != CSW_STATUS_OK) {
                        FURI_LOG_W(
                            TAG,
                            "cmd %02X failed status=%u sk=%02X asc=%02X ascq=%02X",
                            scsi.cmd[0],
                            status,
                            scsi.sk,
                            scsi.asc,
                            scsi.ascq);
                    }
                    csw.sig = CSW_SIG;
                    csw.tag = cbw.tag;
                    csw.status = status;
                    csw.residue = residue;
                    state = StateWriteStatus;
                    continue;
                }; break;
                case StateWriteStatus: {
                    FURI_LOG_T(TAG, "StateWriteStatus");
                    // fails while the IN endpoint is stalled, until the host clears it
                    int32_t len = usbd_ep_write(dev, USB_MSC_TX_EP, &csw, sizeof(csw));
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
    free(buf);
    return 0;
}

// needed in usb_deinit, usb_suspend, usb_rxtx_ep_callback, usb_control,
// where if_ctx isn't passed
static CdUsb* cd_usb_cur = NULL;

static void usb_init(usbd_device* dev, FuriHalUsbInterface* intf, void* ctx) {
    UNUSED(intf);
    CdUsb* cd_usb = ctx;
    cd_usb_cur = cd_usb;
    cd_usb->dev = dev;
    cd_usb->connected = false;

    usbd_reg_config(dev, usb_ep_config);
    usbd_reg_control(dev, usb_control);
    usbd_connect(dev, true);

    furi_thread_start(cd_usb->thread);
}

static void usb_deinit(usbd_device* dev) {
    usbd_reg_config(dev, NULL);
    usbd_reg_control(dev, NULL);

    CdUsb* cd_usb = cd_usb_cur;
    if(!cd_usb || cd_usb->dev != dev) {
        FURI_LOG_E(TAG, "deinit cd_usb_cur leak");
        return;
    }
    cd_usb_cur = NULL;

    if(cd_usb->thread) {
        furi_thread_flags_set(furi_thread_get_id(cd_usb->thread), EventExit);
        furi_thread_join(cd_usb->thread);
    }

    if(!cd_usb->connected && cd_usb->status_cb) {
        cd_usb->status_cb(false, cd_usb->status_cb_context);
    }
}

static void usb_wakeup(usbd_device* dev) {
    UNUSED(dev);
}

static void usb_suspend(usbd_device* dev) {
    CdUsb* cd_usb = cd_usb_cur;
    if(!cd_usb || cd_usb->dev != dev) return;
    furi_thread_flags_set(furi_thread_get_id(cd_usb->thread), EventReset);
}

static void usb_rxtx_ep_callback(usbd_device* dev, uint8_t event, uint8_t ep) {
    UNUSED(ep);
    UNUSED(event);
    CdUsb* cd_usb = cd_usb_cur;
    if(!cd_usb || cd_usb->dev != dev) return;
    furi_thread_flags_set(furi_thread_get_id(cd_usb->thread), EventRxTx);
}

static usbd_respond usb_ep_config(usbd_device* dev, uint8_t cfg) {
    switch(cfg) {
    case 0: // deconfig
        usbd_ep_deconfig(dev, USB_MSC_RX_EP);
        usbd_ep_deconfig(dev, USB_MSC_TX_EP);
        usbd_reg_endpoint(dev, USB_MSC_RX_EP, NULL);
        usbd_reg_endpoint(dev, USB_MSC_TX_EP, NULL);
        return usbd_ack;
    case 1: // config
        usbd_ep_config(dev, USB_MSC_RX_EP, USB_EPTYPE_BULK, USB_MSC_RX_EP_SIZE);
        usbd_ep_config(dev, USB_MSC_TX_EP, USB_EPTYPE_BULK, USB_MSC_TX_EP_SIZE);
        usbd_reg_endpoint(dev, USB_MSC_RX_EP, usb_rxtx_ep_callback);
        usbd_reg_endpoint(dev, USB_MSC_TX_EP, usb_rxtx_ep_callback);
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
    CdUsb* cd_usb = cd_usb_cur;
    if(!cd_usb || cd_usb->dev != dev) return usbd_fail;

    switch(req->bRequest) {
    case USB_MSC_BOT_GET_MAX_LUN: {
        static uint8_t max_lun = 0;
        dev->status.data_ptr = &max_lun;
        dev->status.data_count = 1;
        return usbd_ack;
    }; break;
    case USB_MSC_BOT_RESET: {
        furi_thread_flags_set(furi_thread_get_id(cd_usb->thread), EventReset);
        return usbd_ack;
    }; break;
    }
    return usbd_fail;
}

static const struct usb_string_descriptor dev_manuf_desc = USB_STRING_DESC("Flipper Devices Inc.");

static const struct usb_device_descriptor usb_cd_dev_descr = {
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

static const struct CdUsbDescriptor usb_cd_cfg_descr = {
    .config =
        {
            .bLength = sizeof(struct usb_config_descriptor),
            .bDescriptorType = USB_DTYPE_CONFIGURATION,
            .wTotalLength = sizeof(struct CdUsbDescriptor),
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
            .bNumEndpoints = 2,
            .bInterfaceClass = USB_CLASS_MASS_STORAGE,
            .bInterfaceSubClass = USB_MSC_SUBCLASS_SCSI,
            .bInterfaceProtocol = USB_MSC_PROTO_BOT,
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
};

static struct usb_string_descriptor* usb_string_alloc(const char* str) {
    size_t len = strlen(str);
    struct usb_string_descriptor* descr = malloc(len * 2 + 2);
    descr->bLength = len * 2 + 2;
    descr->bDescriptorType = USB_DTYPE_STRING;
    for(size_t i = 0; i < len; i++)
        descr->wString[i] = str[i];
    return descr;
}

CdUsb* cd_usb_start(CdDevice device) {
    CdUsb* cd_usb = malloc(sizeof(CdUsb));
    cd_usb->device = device;

    cd_usb->usb_prev = furi_hal_usb_get_config();
    cd_usb->usb.init = usb_init;
    cd_usb->usb.deinit = usb_deinit;
    cd_usb->usb.wakeup = usb_wakeup;
    cd_usb->usb.suspend = usb_suspend;
    cd_usb->usb.dev_descr = (struct usb_device_descriptor*)&usb_cd_dev_descr;
    cd_usb->usb.str_manuf_descr = (void*)&dev_manuf_desc;
    cd_usb->usb.cfg_descr = (void*)&usb_cd_cfg_descr;

    const char* name = furi_hal_version_get_device_name_ptr();
    if(!name) name = "Flipper Zero";
    cd_usb->usb.str_prod_descr = usb_string_alloc(name);

    // The serial stays the same across discs, so the host keeps one drive
    FuriString* serial = furi_string_alloc_set_str("CD");
    const uint8_t* uid = furi_hal_version_uid();
    for(size_t i = 0; i < furi_hal_version_uid_size(); i++) {
        furi_string_cat_printf(serial, "%02X", uid[i]);
    }
    cd_usb->usb.str_serial_descr = usb_string_alloc(furi_string_get_cstr(serial));
    furi_string_free(serial);

    cd_usb->thread = furi_thread_alloc_ex("UsbCdromUsb", 2048, cd_thread_worker, cd_usb);
    if(!furi_hal_usb_set_config(&cd_usb->usb, cd_usb)) {
        FURI_LOG_E(TAG, "USB locked, cannot start USB CD-ROM");
        furi_thread_free(cd_usb->thread);
        free(cd_usb->usb.str_prod_descr);
        free(cd_usb->usb.str_serial_descr);
        free(cd_usb);
        return NULL;
    }
    return cd_usb;
}

void cd_usb_stop(CdUsb* cd_usb) {
    furi_hal_usb_set_config(cd_usb->usb_prev, NULL);
    furi_thread_free(cd_usb->thread);
    free(cd_usb->usb.str_prod_descr);
    free(cd_usb->usb.str_serial_descr);
    free(cd_usb);
}

void cd_usb_set_connection_status_callback(
    CdUsb* cd_usb,
    CdUsbConnectionStatusCallback cb,
    void* context) {
    cd_usb->status_cb = cb;
    cd_usb->status_cb_context = context;
}
