/**
 * Nintendo Switch Pro Controller USB emulation for Flipper Zero.
 *
 * Implements a custom FuriHalUsbInterface with:
 *   - Device descriptor: VID=0x057E, PID=0x2009 (Nintendo Pro Controller)
 *   - 0x80 proprietary handshake handler (in USB data-out callback)
 *   - Continuous 0x30 input reports at 125Hz via FuriTimer
 *
 * References:
 *   - https://github.com/ccyyturralde/Flipper-Zero-Joycon
 *   - https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering
 *   - https://gist.github.com/ToadKing/b883a8ccfa26adcc6ba9905e75aeb4f2
 */

#include "switch_pro_usb.h"
#include <furi.h>
#include <furi_hal.h>
#include <usb.h>
#include <usb_std.h>
#include <usb_hid.h>
#define TAG "SwitchProUSB"

/* ---------- USB Endpoint Configuration ---------- */

#define SWITCH_PRO_EP_IN  0x81
#define SWITCH_PRO_EP_OUT 0x02
#define SWITCH_PRO_EP_SIZE 64
#define SWITCH_PRO_EP_INTERVAL 8 /* 8ms = 125Hz */

#define SWITCH_PRO_REPORT_INTERVAL_MS 8

/* ---------- Reply FIFO Queue ---------- */
#define REPLY_QUEUE_SIZE 8

/* ---------- HID Report Descriptor ---------- */

/**
 * HID report descriptor for the Pro Controller.
 * This is intentionally non-standard -- the Switch ignores it and parses
 * raw bytes at fixed offsets. Taken from ToadKing's gist and the
 * Flipper-Zero-Joycon project.
 */
static const uint8_t hid_report_descriptor[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x15, 0x00,       // Logical Minimum (0)
    0x09, 0x04,       // Usage (Joystick)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x30,       //   Report ID (48)
    0x05, 0x01,       //   Usage Page (Generic Desktop)
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x01,       //   Usage Minimum (1)
    0x29, 0x0A,       //   Usage Maximum (10)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x0A,       //   Report Count (10)
    0x55, 0x00,       //   Unit Exponent (0)
    0x65, 0x00,       //   Unit (None)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x0B,       //   Usage Minimum (11)
    0x29, 0x0E,       //   Usage Maximum (14)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x04,       //   Report Count (4)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x02,       //   Report Count (2)
    0x81, 0x03,       //   Input (Constant)
    0x0B, 0x01, 0x00, 0x01, 0x00, // Usage (Generic Desktop: Pointer)
    0xA1, 0x00,       //   Collection (Physical)
    0x0B, 0x30, 0x00, 0x01, 0x00, //     Usage (Generic Desktop: X)
    0x0B, 0x31, 0x00, 0x01, 0x00, //     Usage (Generic Desktop: Y)
    0x0B, 0x32, 0x00, 0x01, 0x00, //     Usage (Generic Desktop: Z)
    0x0B, 0x35, 0x00, 0x01, 0x00, //     Usage (Generic Desktop: Rz)
    0x15, 0x00,       //     Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00, //     Logical Maximum (65535)
    0x75, 0x10,       //     Report Size (16)
    0x95, 0x04,       //     Report Count (4)
    0x81, 0x02,       //     Input (Data, Variable, Absolute)
    0xC0,             //   End Collection
    0x0B, 0x39, 0x00, 0x01, 0x00, //   Usage (Generic Desktop: Hat Switch)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x07,       //   Logical Maximum (7)
    0x35, 0x00,       //   Physical Minimum (0)
    0x46, 0x3B, 0x01, //   Physical Maximum (315)
    0x65, 0x14,       //   Unit (Degrees)
    0x75, 0x04,       //   Report Size (4)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x0F,       //   Usage Minimum (15)
    0x29, 0x12,       //   Usage Maximum (18)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x04,       //   Report Count (4)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x34,       //   Report Count (52)
    0x81, 0x03,       //   Input (Constant)
    0x06, 0x00, 0xFF, //   Usage Page (Vendor Defined 0xFF00)
    0x85, 0x21,       //   Report ID (33)
    0x09, 0x01,       //   Usage (0x01)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x81, 0x03,       //   Input (Constant)
    0x85, 0x81,       //   Report ID (129)
    0x09, 0x02,       //   Usage (0x02)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x81, 0x03,       //   Input (Constant)
    0x85, 0x01,       //   Report ID (1)
    0x09, 0x03,       //   Usage (0x03)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x91, 0x83,       //   Output (Constant, Variable)
    0x85, 0x10,       //   Report ID (16)
    0x09, 0x04,       //   Usage (0x04)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x91, 0x83,       //   Output (Constant, Variable)
    0x85, 0x80,       //   Report ID (128)
    0x09, 0x05,       //   Usage (0x05)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x91, 0x83,       //   Output (Constant, Variable)
    0x85, 0x82,       //   Report ID (130)
    0x09, 0x06,       //   Usage (0x06)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x91, 0x83,       //   Output (Constant, Variable)
    0xC0,             // End Collection
};

/* ---------- USB Descriptors ---------- */

/**
 * Device descriptor: Nintendo Pro Controller.
 * VID=0x057E (Nintendo), PID=0x2009 (Pro Controller).
 */
static const struct usb_device_descriptor device_descriptor = {
    .bLength = sizeof(struct usb_device_descriptor),
    .bDescriptorType = USB_DTYPE_DEVICE,
    .bcdUSB = VERSION_BCD(2, 0, 0),
    .bDeviceClass = USB_CLASS_PER_INTERFACE,
    .bDeviceSubClass = USB_SUBCLASS_NONE,
    .bDeviceProtocol = USB_PROTO_NONE,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x057E,
    .idProduct = 0x2009,
    .bcdDevice = VERSION_BCD(2, 0, 0),
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

/**
 * Combined configuration + interface + HID + endpoint descriptors.
 * Packed as a single blob for USB enumeration.
 */
struct SwitchProConfigDescriptor {
    struct usb_config_descriptor config;
    struct usb_interface_descriptor intf;
    struct usb_hid_descriptor hid;
    struct usb_endpoint_descriptor ep_in;
    struct usb_endpoint_descriptor ep_out;
} __attribute__((packed));

static const struct SwitchProConfigDescriptor config_descriptor = {
    .config =
        {
            .bLength = sizeof(struct usb_config_descriptor),
            .bDescriptorType = USB_DTYPE_CONFIGURATION,
            .wTotalLength = sizeof(struct SwitchProConfigDescriptor),
            .bNumInterfaces = 1,
            .bConfigurationValue = 1,
            .iConfiguration = 0,
            .bmAttributes = USB_CFG_ATTR_RESERVED | 0x20, /* remote wakeup, not self-powered */
            .bMaxPower = USB_CFG_POWER_MA(500),
        },
    .intf =
        {
            .bLength = sizeof(struct usb_interface_descriptor),
            .bDescriptorType = USB_DTYPE_INTERFACE,
            .bInterfaceNumber = 0,
            .bAlternateSetting = 0,
            .bNumEndpoints = 2,
            .bInterfaceClass = USB_CLASS_HID,
            .bInterfaceSubClass = 0x00,
            .bInterfaceProtocol = 0x00,
            .iInterface = 0,
        },
    .hid =
        {
            .bLength = sizeof(struct usb_hid_descriptor),
            .bDescriptorType = USB_DTYPE_HID,
            .bcdHID = VERSION_BCD(1, 1, 1),
            .bCountryCode = 0,
            .bNumDescriptors = 1,
            .bDescriptorType0 = USB_DTYPE_HID_REPORT,
            .wDescriptorLength0 = sizeof(hid_report_descriptor),
        },
    .ep_in =
        {
            .bLength = sizeof(struct usb_endpoint_descriptor),
            .bDescriptorType = USB_DTYPE_ENDPOINT,
            .bEndpointAddress = SWITCH_PRO_EP_IN,
            .bmAttributes = USB_EPTYPE_INTERRUPT,
            .wMaxPacketSize = SWITCH_PRO_EP_SIZE,
            .bInterval = SWITCH_PRO_EP_INTERVAL,
        },
    .ep_out =
        {
            .bLength = sizeof(struct usb_endpoint_descriptor),
            .bDescriptorType = USB_DTYPE_ENDPOINT,
            .bEndpointAddress = SWITCH_PRO_EP_OUT,
            .bmAttributes = USB_EPTYPE_INTERRUPT,
            .wMaxPacketSize = SWITCH_PRO_EP_SIZE,
            .bInterval = SWITCH_PRO_EP_INTERVAL,
        },
};

/* ---------- String Descriptors ---------- */

/* String descriptors are allocated dynamically in init via hid_set_string_descr
 * (heap memory is ISR-accessible, unlike FAP static memory). */

/* ---------- Internal State ---------- */

typedef struct {
    usbd_device* usb_dev;
    FuriTimer* report_timer;
    FuriMutex* mutex;
    SwitchButtonState button_state;
    uint8_t report_timer_counter;
    uint8_t handshake_stage; /* 0=none, 1-4=0x80 commands received */
    uint8_t last_rx_byte0;   /* debug: last received data[0] */
    uint8_t last_rx_byte1;   /* debug: last received data[1] */
    uint16_t rx_count;       /* debug: total EP OUT callbacks */
    uint16_t tx_count;       /* debug: total EP IN writes */
    uint16_t report_count;   /* debug: 0x30 reports sent by timer */
    uint16_t subcmd_mask;    /* debug: bitmask of unique subcmds seen */
    uint32_t last_spi_addr;  /* debug: last SPI flash read address */
    uint16_t dropped_replies; /* debug: replies dropped due to full queue */
    uint8_t spi_read_count;  /* debug: number of SPI flash reads received */
    uint32_t spi_addrs[4];   /* debug: last 4 SPI read addresses */
    uint8_t subcmd_log[8];   /* debug: last 8 subcmd IDs received */
    uint8_t subcmd_log_idx;  /* debug: index into subcmd_log (wraps at 8) */
    bool handshake_complete;
    bool report_timer_running;
    bool usb_connected;
    bool input_mode_set;     /* debug: subcmd 0x03 received */
    bool imu_enabled;        /* subcmd 0x40 received */
    bool init_called;        /* debug: USB init callback fired */
    bool ep_configured;      /* debug: SET_CONFIGURATION received (ep_config cfg=1) */
    uint8_t ep_in_buf[SWITCH_PRO_EP_SIZE];
    uint8_t ep_out_buf[SWITCH_PRO_EP_SIZE];
    /* FIFO queue for 0x21 subcmd replies (ISR writes, timer reads) */
    uint8_t reply_queue[REPLY_QUEUE_SIZE][SWITCH_PRO_EP_SIZE];
    volatile uint8_t reply_head; /* next write position (ISR) */
    volatile uint8_t reply_tail; /* next read position (timer) */
} SwitchProState;

static SwitchProState* switch_pro_state = NULL;
static FuriHalUsbInterface* prev_usb_config = NULL;
static uint32_t* usb_ctrlreq_buf = NULL;
#define USB_CTRLREQ_BUF_SIZE 256

static void* hid_set_string_descr(char* str) {
    furi_assert(str);
    size_t len = strlen(str);
    struct usb_string_descriptor* dev_str_desc = malloc(len * 2 + 2);
    dev_str_desc->bLength = len * 2 + 2;
    dev_str_desc->bDescriptorType = USB_DTYPE_STRING;
    for(size_t i = 0; i < len; i++)
        dev_str_desc->wString[i] = str[i];
    return dev_str_desc;
}


/* ---------- Virtual SPI Flash Data ---------- */

/*
 * SPI flash layout the Switch reads during Pro Controller init:
 *   0x6000 len=0x10: Serial number (16 bytes)
 *   0x6020 len=0x18: Factory 6-axis sensor calibration (24 bytes)
 *   0x603D len=0x12: Factory stick calibration: left 9 + right 9 (18 bytes)
 *   0x6050 len=0x0D: Body/button/grip colors (13 bytes)
 *   0x6080 len=0x18: 6-axis horizontal offsets (6) + stick params 1 (18) = 24 bytes
 *   0x6098 len=0x12: Stick device parameters 2 (18 bytes)
 *   0x8010 len=0x16: User stick cal (0xFF = use factory)
 *   0x8026 len=0x1A: User sensor cal (0xFF = use factory)
 *
 * Values from NXBT Pro Controller defaults and dekuNukem reverse engineering.
 */

/* Serial number at 0x6000 (16 bytes) */
static const uint8_t spi_serial[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* Factory 6-axis sensor calibration at 0x6020 (24 bytes)
 * Acc XYZ origin, Acc XYZ sensitivity, Gyro XYZ origin, Gyro XYZ sensitivity */
static const uint8_t spi_sensor_cal[] = {
    0xD3, 0xFF, 0xD5, 0xFF, 0x55, 0x01,  /* acc origin */
    0x00, 0x40, 0x00, 0x40, 0x00, 0x40,  /* acc sensitivity (default +/-8G) */
    0x19, 0x00, 0xDD, 0xFF, 0xDC, 0xFF,  /* gyro origin */
    0x3B, 0x34, 0x3B, 0x34, 0x3B, 0x34,  /* gyro sensitivity (default +/-2000dps) */
};

/* Factory stick calibration at 0x603D (18 bytes): left 9 + right 9
 * Left: max-above-center, center, min-below-center
 * Right: center, min-below-center, max-above-center */
static const uint8_t spi_stick_cal[] = {
    /* Left stick (0x603D-0x6045) — from NXBT Pro Controller defaults */
    0xBA, 0xF5, 0x62, 0x6F, 0xC8, 0x77, 0xED, 0x95, 0x5B,
    /* Right stick (0x6046-0x604E) */
    0x16, 0xD8, 0x7D, 0xF2, 0xB5, 0x5F, 0x86, 0x65, 0x5E,
};

/* Body/button colors at 0x6050 (13 bytes): gray Pro Controller */
static const uint8_t spi_colors[] = {
    0x32, 0x32, 0x32,  /* body: dark gray */
    0xFF, 0xFF, 0xFF,  /* buttons: white */
    0x32, 0x32, 0x32,  /* left grip */
    0x32, 0x32, 0x32,  /* right grip */
    0x01,              /* 13th byte (color flag) */
};

/* 6-Axis horizontal offsets (6 bytes at 0x6080) + Stick params 1 (18 bytes at 0x6086)
 * Switch reads as single block: 0x6080, len=0x18 (24 bytes) */
static const uint8_t spi_6axis_and_params1[] = {
    /* 6-axis horizontal offsets (0x6080-0x6085) — Pro Controller values */
    0x50, 0xFD, 0x00, 0x00, 0xC6, 0x0F,
    /* Stick device parameters 1 (0x6086-0x6097) — Pro Controller dead-zone + range */
    0x0F, 0x30, 0x61, 0x96, 0x30, 0xF3,
    0xD4, 0x14, 0x54, 0x41, 0x15, 0x54,
    0xC7, 0x79, 0x9C, 0x33, 0x36, 0x63,
};

/* Stick device parameters 2 at 0x6098 (18 bytes) — identical to params 1 */
static const uint8_t spi_stick_params2[] = {
    0x0F, 0x30, 0x61, 0x96, 0x30, 0xF3,
    0xD4, 0x14, 0x54, 0x41, 0x15, 0x54,
    0xC7, 0x79, 0x9C, 0x33, 0x36, 0x63,
};

typedef struct {
    uint32_t addr;
    const uint8_t* data;
    uint8_t len;
} SpiRegion;

static const SpiRegion spi_regions[] = {
    { 0x6000, spi_serial,            sizeof(spi_serial) },
    { 0x6020, spi_sensor_cal,        sizeof(spi_sensor_cal) },
    { 0x603D, spi_stick_cal,         sizeof(spi_stick_cal) },
    { 0x6050, spi_colors,            sizeof(spi_colors) },
    { 0x6080, spi_6axis_and_params1, sizeof(spi_6axis_and_params1) },
    { 0x6098, spi_stick_params2,     sizeof(spi_stick_params2) },
};
#define SPI_REGION_COUNT (sizeof(spi_regions) / sizeof(spi_regions[0]))

/** Read from virtual SPI flash. Known regions return data; unknown returns 0xFF. */
static void spi_flash_read(uint32_t addr, uint8_t len, uint8_t* out) {
    memset(out, 0xFF, len);
    for(size_t r = 0; r < SPI_REGION_COUNT; r++) {
        const SpiRegion* reg = &spi_regions[r];
        if(addr + len <= reg->addr || addr >= reg->addr + reg->len) continue;
        uint32_t src_off = (addr > reg->addr) ? addr - reg->addr : 0;
        uint32_t dst_off = (reg->addr > addr) ? reg->addr - addr : 0;
        uint32_t n = reg->len - src_off;
        if(n > len - dst_off) n = len - dst_off;
        memcpy(out + dst_off, reg->data + src_off, n);
    }
}

/** Map subcmd ID to a bit position for the debug subcmd_mask. */
static uint16_t subcmd_to_bit(uint8_t subcmd) {
    switch(subcmd) {
    case 0x02: return 0x001;
    case 0x03: return 0x002;
    case 0x04: return 0x004;
    case 0x08: return 0x008;
    case 0x10: return 0x010;
    case 0x21: return 0x020;
    case 0x30: return 0x040;
    case 0x38: return 0x080;
    case 0x40: return 0x100;
    case 0x48: return 0x200;
    default:   return 0x000;
    }
}

/* ---------- Forward Declarations ---------- */

static void switch_pro_init(usbd_device* dev, FuriHalUsbInterface* intf, void* ctx);
static void switch_pro_deinit(usbd_device* dev);
static void switch_pro_on_wakeup(usbd_device* dev);
static void switch_pro_on_suspend(usbd_device* dev);
static usbd_respond switch_pro_ep_config(usbd_device* dev, uint8_t cfg);
static usbd_respond
    switch_pro_control(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback);
static void switch_pro_report_timer_callback(void* context);
static void switch_pro_ep_out_callback(usbd_device* dev, uint8_t event, uint8_t ep);

/* ---------- FuriHalUsbInterface Definition ---------- */

FuriHalUsbInterface usb_switch_pro = {
    .init = switch_pro_init,
    .deinit = switch_pro_deinit,
    .wakeup = switch_pro_on_wakeup,
    .suspend = switch_pro_on_suspend,
    .dev_descr = (struct usb_device_descriptor*)&device_descriptor,
    .str_manuf_descr = NULL,
    .str_prod_descr = NULL,
    .str_serial_descr = NULL,
    .cfg_descr = (void*)&config_descriptor,
};

/* ---------- USB Lifecycle ---------- */

/**
 * USB init callback — only save state and set strings.
 * All USB hardware setup (usbd_init, callbacks, connect) happens in
 * switch_pro_usb_start() AFTER furi_hal_usb_set_config returns,
 * to prevent the HAL from overwriting our callback registrations.
 */
static void switch_pro_init(usbd_device* dev, FuriHalUsbInterface* intf, void* ctx) {
    UNUSED(intf);
    UNUSED(ctx);

    if(switch_pro_state) {
        switch_pro_state->usb_dev = dev;
        switch_pro_state->handshake_complete = false;
        switch_pro_state->usb_connected = false;
        switch_pro_state->report_timer_counter = 0;
        switch_pro_state->init_called = true;
        switch_pro_state->reply_head = 0;
        switch_pro_state->reply_tail = 0;
        switch_pro_report_init(&switch_pro_state->button_state);
    }

    // Set string descriptors dynamically (heap-allocated, ISR-accessible)
    usb_switch_pro.dev_descr->iManufacturer = 1;
    usb_switch_pro.dev_descr->iProduct = 2;
    usb_switch_pro.dev_descr->iSerialNumber = 3;
    usb_switch_pro.str_manuf_descr = hid_set_string_descr("Nintendo Co., Ltd.");
    usb_switch_pro.str_prod_descr = hid_set_string_descr("Pro Controller");
    usb_switch_pro.str_serial_descr = hid_set_string_descr("000000000001");
}

static void switch_pro_deinit(usbd_device* dev) {
    usbd_reg_config(dev, NULL);
    usbd_reg_control(dev, NULL);
    free(usb_switch_pro.str_manuf_descr);
    free(usb_switch_pro.str_prod_descr);
    free(usb_switch_pro.str_serial_descr);
    usb_switch_pro.str_manuf_descr = NULL;
    usb_switch_pro.str_prod_descr = NULL;
    usb_switch_pro.str_serial_descr = NULL;
}

static void switch_pro_on_wakeup(usbd_device* dev) {
    UNUSED(dev);
}

static void switch_pro_on_suspend(usbd_device* dev) {
    UNUSED(dev);
    if(switch_pro_state) {
        // Just set flags — no FreeRTOS APIs in ISR context!
        // The report timer callback checks handshake_complete before sending.
        switch_pro_state->handshake_complete = false;
        switch_pro_state->report_timer_running = false;
        switch_pro_state->reply_head = 0;
        switch_pro_state->reply_tail = 0;
    }
}

/* ---------- USB Endpoint & Control Callbacks ---------- */

static usbd_respond switch_pro_ep_config(usbd_device* dev, uint8_t cfg) {
    switch(cfg) {
    case 0:
        // Deconfigure
        usbd_ep_deconfig(dev, SWITCH_PRO_EP_IN);
        usbd_ep_deconfig(dev, SWITCH_PRO_EP_OUT);
        usbd_reg_endpoint(dev, SWITCH_PRO_EP_OUT, NULL);
        return usbd_ack;
    case 1:
        // Configure endpoints (host sent SET_CONFIGURATION)
        usbd_ep_config(dev, SWITCH_PRO_EP_IN, USB_EPTYPE_INTERRUPT, SWITCH_PRO_EP_SIZE);
        usbd_ep_config(dev, SWITCH_PRO_EP_OUT, USB_EPTYPE_INTERRUPT, SWITCH_PRO_EP_SIZE);
        usbd_reg_endpoint(dev, SWITCH_PRO_EP_IN, NULL);
        usbd_reg_endpoint(dev, SWITCH_PRO_EP_OUT, switch_pro_ep_out_callback);
        // Prime EP OUT for receiving
        if(switch_pro_state) {
            switch_pro_state->ep_configured = true;
            switch_pro_state->usb_connected = true;
            usbd_ep_read(
                dev, SWITCH_PRO_EP_OUT,
                switch_pro_state->ep_out_buf, SWITCH_PRO_EP_SIZE);
        }
        return usbd_ack;
    default:
        return usbd_fail;
    }
}

/**
 * Handle USB control requests (HID class requests + descriptor requests).
 */
static usbd_respond
    switch_pro_control(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback) {
    UNUSED(callback);

    // HID class requests
    if(((USB_REQ_RECIPIENT | USB_REQ_TYPE) & req->bmRequestType) ==
           (USB_REQ_INTERFACE | USB_REQ_CLASS) &&
       req->wIndex == 0) {
        switch(req->bRequest) {
        case USB_HID_SETIDLE:
            return usbd_ack;
        case USB_HID_SETPROTOCOL:
            return usbd_ack;
        case USB_HID_GETREPORT: {
            // GET_REPORT: wValue high byte = report type, low byte = report ID
            // Return a valid 0x30 input report so the host knows our format
            static uint8_t gr_report[SWITCH_PRO_EP_SIZE];
            if(switch_pro_state) {
                SwitchButtonState idle;
                switch_pro_report_init(&idle);
                switch_pro_report_build(&idle, gr_report, 0);
            } else {
                memset(gr_report, 0, SWITCH_PRO_EP_SIZE);
                gr_report[0] = 0x30;
                gr_report[2] = 0x81;
            }
            dev->status.data_ptr = gr_report;
            dev->status.data_count = SWITCH_PRO_EP_SIZE;
            return usbd_ack;
        }
        case USB_HID_SETREPORT:
            // SET_REPORT: the Switch may send commands via control pipe.
            // Accept and ignore — subcommands come via interrupt OUT.
            return usbd_ack;
        default:
            return usbd_fail;
        }
    }

    // Standard descriptor requests (HID descriptor + HID report descriptor)
    if(((USB_REQ_RECIPIENT | USB_REQ_TYPE) & req->bmRequestType) ==
           (USB_REQ_INTERFACE | USB_REQ_STANDARD) &&
       req->wIndex == 0 && req->bRequest == USB_STD_GET_DESCRIPTOR) {
        switch(req->wValue >> 8) {
        case USB_DTYPE_HID:
            dev->status.data_ptr = (uint8_t*)&(config_descriptor.hid);
            dev->status.data_count = sizeof(config_descriptor.hid);
            return usbd_ack;
        case USB_DTYPE_HID_REPORT:
            dev->status.data_ptr = (uint8_t*)hid_report_descriptor;
            dev->status.data_count = sizeof(hid_report_descriptor);
            return usbd_ack;
        default:
            return usbd_fail;
        }
    }

    return usbd_fail;
}

/* ---------- 0x80 Handshake Handler ---------- */

/**
 * Enqueue a response packet for the timer to send via EP IN.
 * Timer is the SOLE writer to EP IN — prevents PMA overwrite race conditions
 * between ISR and timer context.
 */
static void enqueue_reply(const uint8_t* response) {
    uint8_t next_head = (switch_pro_state->reply_head + 1) % REPLY_QUEUE_SIZE;
    if(next_head != switch_pro_state->reply_tail) {
        memcpy(switch_pro_state->reply_queue[switch_pro_state->reply_head],
               response, SWITCH_PRO_EP_SIZE);
        switch_pro_state->reply_head = next_head;
    } else {
        switch_pro_state->dropped_replies++;
    }
}

/**
 * Handle output reports from the Switch (received on EP OUT).
 *
 * ALL replies are queued in the FIFO for the timer to send.
 * The timer is the SOLE writer to EP IN, preventing PMA overwrite
 * race conditions between ISR and timer context.
 *
 * Handshake sequence:
 *   1. Switch sends 80 01 -> Reply: 81 01 00 03 [6-byte MAC]
 *   2. Switch sends 80 02 -> Reply: 81 02
 *   3. Switch sends 80 03 -> Reply: 81 03
 *   4. Switch sends 80 04 -> Reply: 81 04 (begin HID-only mode)
 */
static void switch_pro_ep_out_callback(usbd_device* dev, uint8_t event, uint8_t ep) {
    UNUSED(dev);
    // EP IN 0x81 and EP OUT 0x01 share callback slot endpoint[1].
    // Ignore EP IN TX completion events — only process EP OUT RX.
    if(event != usbd_evt_eprx) return;

    if(switch_pro_state == NULL) return;

    int32_t len =
        usbd_ep_read(switch_pro_state->usb_dev, ep, switch_pro_state->ep_out_buf, SWITCH_PRO_EP_SIZE);

    if(len <= 0) return;

    uint8_t* data = switch_pro_state->ep_out_buf;
    switch_pro_state->rx_count++;
    switch_pro_state->last_rx_byte0 = data[0];
    switch_pro_state->last_rx_byte1 = (len > 1) ? data[1] : 0;

    if(data[0] == 0x80) {
        uint8_t response[SWITCH_PRO_EP_SIZE];
        memset(response, 0, SWITCH_PRO_EP_SIZE);
        bool has_response = true;

        switch(data[1]) {
        case 0x01:
            switch_pro_state->handshake_stage = 1;
            response[0] = 0x81;
            response[1] = 0x01;
            response[2] = 0x00;
            response[3] = 0x03; // Pro Controller type
            response[4] = 0xAA; // MAC
            response[5] = 0xBB;
            response[6] = 0xCC;
            response[7] = 0xDD;
            response[8] = 0xEE;
            response[9] = 0xFF;
            break;

        case 0x02:
            switch_pro_state->handshake_stage = 2;
            response[0] = 0x81;
            response[1] = 0x02;
            break;

        case 0x03:
            switch_pro_state->handshake_stage = 3;
            response[0] = 0x81;
            response[1] = 0x03;
            break;

        case 0x04:
            switch_pro_state->handshake_stage = 4;
            response[0] = 0x81;
            response[1] = 0x04;
            // Don't set handshake_complete here — let the timer send
            // the reply FIRST, then set it on the next tick.
            break;

        case 0x05:
            response[0] = 0x81;
            response[1] = 0x05;
            break;

        default:
            has_response = false;
            break;
        }

        if(has_response) {
            enqueue_reply(response);
        }
    } else if(data[0] == 0x10) {
        // Type 0x10: rumble-only output report — NO reply needed.
    } else if(data[0] == 0x01 && len > 10) {
        // Type 0x01: subcmd output report — requires 0x21 reply.
        uint8_t subcmd = data[10];

        // Track which subcmds we've seen
        switch_pro_state->subcmd_mask |= subcmd_to_bit(subcmd);
        switch_pro_state->subcmd_log[switch_pro_state->subcmd_log_idx % 8] = subcmd;
        switch_pro_state->subcmd_log_idx++;

        // Build 0x21 subcommand reply
        uint8_t response[SWITCH_PRO_EP_SIZE];
        memset(response, 0, SWITCH_PRO_EP_SIZE);
        response[0] = 0x21;
        response[1] = switch_pro_state->report_timer_counter;
        response[2] = 0x81; // Battery full + Pro Controller USB-powered
        // Bytes 3-5: buttons (all released = 0)
        // Bytes 6-8: left stick at center (2048, 2048)
        response[6] = 0x00;
        response[7] = 0x08;
        response[8] = 0x80;
        // Bytes 9-11: right stick at center
        response[9] = 0x00;
        response[10] = 0x08;
        response[11] = 0x80;
        // Byte 12: vibrator input report
        response[12] = 0x80;
        // Byte 13 = ACK type, byte 14 = subcmd echo, byte 15+ = data.
        response[14] = subcmd; // subcmd echo (always required)

        switch(subcmd) {
        case 0x02: // Request device info — 0x82 triggers full subcmd sequence
            response[13] = 0x82;
            response[15] = 0x03; // FW version major
            response[16] = 0x48; // FW version minor
            response[17] = 0x03; // Controller type: Pro Controller
            response[18] = 0x02; // Unknown
            response[19] = 0xAA; // MAC
            response[20] = 0xBB;
            response[21] = 0xCC;
            response[22] = 0xDD;
            response[23] = 0xEE;
            response[24] = 0xFF;
            response[25] = 0x01; // Unknown (matches real Pro Controller)
            response[26] = 0x01; // SPI colors available
            break;

        case 0x10: { // SPI flash read — 0x90 = ACK with data
            response[13] = 0x90;
            uint32_t spi_addr = (uint32_t)data[11] |
                                ((uint32_t)data[12] << 8) |
                                ((uint32_t)data[13] << 16) |
                                ((uint32_t)data[14] << 24);
            uint8_t spi_len = data[15];
            if(spi_len > 29) spi_len = 29;

            switch_pro_state->last_spi_addr = spi_addr;
            if(switch_pro_state->spi_read_count < 4) {
                switch_pro_state->spi_addrs[switch_pro_state->spi_read_count] = spi_addr;
            }
            switch_pro_state->spi_read_count++;

            // Address echo + length + data at byte 15+
            response[15] = data[11];
            response[16] = data[12];
            response[17] = data[13];
            response[18] = data[14];
            response[19] = spi_len;
            spi_flash_read(spi_addr, spi_len, &response[20]);
            break;
        }

        case 0x03: // Set input report mode
            response[13] = 0x80;
            switch_pro_state->input_mode_set = true;
            break;

        case 0x04: // Trigger buttons elapsed time — simple ACK
            response[13] = 0x80;
            break;

        case 0x08: // Set shipment low power state — simple ACK
            response[13] = 0x80;
            break;

        case 0x30: // Set player lights (LEDs) — simple ACK
            response[13] = 0x80;
            break;

        case 0x38: // Set HOME light — simple ACK
            response[13] = 0x80;
            break;

        case 0x40: // Enable 6-axis sensor (IMU) — simple ACK
            response[13] = 0x80;
            switch_pro_state->imu_enabled = (len > 11 && data[11] == 0x01);
            break;

        case 0x01: // Bluetooth manual pairing — ACK 0x81 (not 0x80!)
            response[13] = 0x81;
            break;

        case 0x48: // Enable vibration — simple ACK
            response[13] = 0x80;
            break;

        default:
            response[13] = 0x80;
            break;
        }

        enqueue_reply(response);
    }

    // Re-prime EP OUT for the next incoming packet
    usbd_ep_read(switch_pro_state->usb_dev, SWITCH_PRO_EP_OUT,
                 switch_pro_state->ep_out_buf, SWITCH_PRO_EP_SIZE);
}

/* ---------- Report Timer Callback ---------- */

/**
 * Periodic callback (every 8ms / 125Hz). SOLE writer to EP IN.
 * Sends queued replies (handshake + subcmd) with priority, then 0x30 reports.
 * This prevents PMA overwrite races between ISR and timer.
 */
static void switch_pro_report_timer_callback(void* context) {
    SwitchProState* state = (SwitchProState*)context;
    if(state == NULL || !state->usb_connected) return;

    uint8_t report[SWITCH_PRO_EP_SIZE];

    // ONE write per tick. Priority: queued reply > 0x30 report.
    if(state->reply_tail != state->reply_head) {
        // Send one queued reply (handshake or subcmd)
        __disable_irq();
        uint8_t tail = state->reply_tail;
        if(tail != state->reply_head) {
            memcpy(report, state->reply_queue[tail], SWITCH_PRO_EP_SIZE);
            state->reply_tail = (tail + 1) % REPLY_QUEUE_SIZE;
            __enable_irq();

            usbd_ep_write(state->usb_dev, SWITCH_PRO_EP_IN, report, SWITCH_PRO_EP_SIZE);
            state->tx_count++;
            state->report_timer_counter++;

            // If we just sent 0x81 0x04, mark handshake complete for NEXT tick
            if(report[0] == 0x81 && report[1] == 0x04) {
                state->handshake_complete = true;
            }
            return;
        }
        __enable_irq();
    }

    // Only send 0x30 reports after handshake completes
    if(!state->handshake_complete) return;

    // Send regular 0x30 input report
    if(furi_mutex_acquire(state->mutex, 0) == FuriStatusOk) {
        switch_pro_report_build(&state->button_state, report, state->report_timer_counter);
        furi_mutex_release(state->mutex);
    } else {
        SwitchButtonState idle;
        switch_pro_report_init(&idle);
        switch_pro_report_build(&idle, report, state->report_timer_counter);
    }

    usbd_ep_write(state->usb_dev, SWITCH_PRO_EP_IN, report, SWITCH_PRO_EP_SIZE);
    state->report_timer_counter++;
    state->report_count++;
}

/* ---------- Public API ---------- */

bool switch_pro_usb_start(void) {
    // Allocate state and resources in thread context (init callback must stay lightweight)
    if(switch_pro_state == NULL) {
        switch_pro_state = malloc(sizeof(SwitchProState));
        memset(switch_pro_state, 0, sizeof(SwitchProState));
    }

    switch_pro_state->handshake_complete = false;
    switch_pro_state->usb_connected = false;
    switch_pro_state->report_timer_counter = 0;
    switch_pro_state->init_called = false;
    switch_pro_state->ep_configured = false;
    switch_pro_state->report_count = 0;
    switch_pro_state->subcmd_mask = 0;
    switch_pro_state->last_spi_addr = 0;
    switch_pro_state->input_mode_set = false;
    switch_pro_state->imu_enabled = false;
    switch_pro_state->dropped_replies = 0;
    switch_pro_state->spi_read_count = 0;
    memset(switch_pro_state->spi_addrs, 0, sizeof(switch_pro_state->spi_addrs));
    switch_pro_state->subcmd_log_idx = 0;
    memset(switch_pro_state->subcmd_log, 0, sizeof(switch_pro_state->subcmd_log));
    switch_pro_state->reply_head = 0;
    switch_pro_state->reply_tail = 0;
    switch_pro_report_init(&switch_pro_state->button_state);

    if(switch_pro_state->mutex == NULL) {
        switch_pro_state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    }
    if(switch_pro_state->report_timer == NULL) {
        switch_pro_state->report_timer = furi_timer_alloc(
            switch_pro_report_timer_callback, FuriTimerTypePeriodic, switch_pro_state);
    }

    // Allocate USB control request buffer from heap (ISR-accessible)
    if(usb_ctrlreq_buf == NULL) {
        usb_ctrlreq_buf = malloc(USB_CTRLREQ_BUF_SIZE);
    }

    // Save current USB config so we can restore on exit
    prev_usb_config = furi_hal_usb_get_config();

    // Switch to Pro Controller USB interface.
    // set_config calls our init callback (which only saves state + sets strings).
    furi_hal_usb_unlock();
    bool result = furi_hal_usb_set_config(&usb_switch_pro, NULL);
    if(!result) {
        FURI_LOG_E(TAG, "USB set_config FAILED!");
        return false;
    }

    // Now that the HAL is done, take full control of USB hardware.
    // This ensures our callbacks aren't overwritten by the HAL's post-init steps.
    // 1) Re-init USB with EP0=64 (real Pro Controller) and our control buffer
    usbd_init(switch_pro_state->usb_dev, &usbd_hw, 64, usb_ctrlreq_buf, USB_CTRLREQ_BUF_SIZE);
    // 2) Register our endpoint config and control request handlers
    usbd_reg_config(switch_pro_state->usb_dev, switch_pro_ep_config);
    usbd_reg_control(switch_pro_state->usb_dev, switch_pro_control);
    // 3) Clean disconnect + reconnect so the dock enumerates us fresh
    usbd_connect(switch_pro_state->usb_dev, false);
    furi_delay_ms(200);
    usbd_connect(switch_pro_state->usb_dev, true);

    // 4) Start report timer immediately — callback checks handshake_complete
    //    before doing anything, so no reports are sent until handshake finishes.
    //    This eliminates the 500ms delay from waiting for scene_refresh to poll.
    furi_timer_start(
        switch_pro_state->report_timer,
        furi_ms_to_ticks(SWITCH_PRO_REPORT_INTERVAL_MS));
    switch_pro_state->report_timer_running = true;

    FURI_LOG_I(TAG, "USB Pro Controller interface started");
    return true;
}

void switch_pro_usb_stop(void) {
    // Stop report timer if running
    if(switch_pro_state && switch_pro_state->report_timer) {
        furi_timer_stop(switch_pro_state->report_timer);
    }

    // Restore previous USB config (this will call our deinit)
    furi_hal_usb_unlock();
    if(prev_usb_config) {
        furi_hal_usb_set_config(prev_usb_config, NULL);
        prev_usb_config = NULL;
    }

    // Free resources (thread context, safe to call FreeRTOS APIs)
    if(switch_pro_state) {
        if(switch_pro_state->report_timer) {
            furi_timer_free(switch_pro_state->report_timer);
            switch_pro_state->report_timer = NULL;
        }
        if(switch_pro_state->mutex) {
            furi_mutex_free(switch_pro_state->mutex);
            switch_pro_state->mutex = NULL;
        }
        free(switch_pro_state);
        switch_pro_state = NULL;
    }

    FURI_LOG_I(TAG, "USB Pro Controller interface stopped");
}

void switch_pro_usb_set_button_state(const SwitchButtonState* state) {
    if(switch_pro_state == NULL || switch_pro_state->mutex == NULL) return;

    if(furi_mutex_acquire(switch_pro_state->mutex, FuriWaitForever) == FuriStatusOk) {
        memcpy(&switch_pro_state->button_state, state, sizeof(SwitchButtonState));
        furi_mutex_release(switch_pro_state->mutex);
    }
}

uint8_t switch_pro_usb_get_handshake_stage(void) {
    if(switch_pro_state == NULL) return 0;
    return switch_pro_state->handshake_stage;
}

void switch_pro_usb_get_debug(uint8_t* rx0, uint8_t* rx1, uint16_t* rxn, uint16_t* txn) {
    if(switch_pro_state == NULL) { *rx0=0; *rx1=0; *rxn=0; *txn=0; return; }
    *rx0 = switch_pro_state->last_rx_byte0;
    *rx1 = switch_pro_state->last_rx_byte1;
    *rxn = switch_pro_state->rx_count;
    *txn = switch_pro_state->tx_count;
}

bool switch_pro_usb_is_connected(void) {
    if(switch_pro_state == NULL) return false;
    return switch_pro_state->handshake_complete;
}

uint16_t switch_pro_usb_get_report_count(void) {
    return switch_pro_state ? switch_pro_state->report_count : 0;
}

uint16_t switch_pro_usb_get_subcmd_mask(void) {
    return switch_pro_state ? switch_pro_state->subcmd_mask : 0;
}

uint32_t switch_pro_usb_get_last_spi_addr(void) {
    return switch_pro_state ? switch_pro_state->last_spi_addr : 0;
}

bool switch_pro_usb_get_input_mode_set(void) {
    return switch_pro_state ? switch_pro_state->input_mode_set : false;
}

bool switch_pro_usb_get_bus_connected(void) {
    return switch_pro_state ? switch_pro_state->usb_connected : false;
}

bool switch_pro_usb_get_init_called(void) {
    return switch_pro_state ? switch_pro_state->init_called : false;
}

bool switch_pro_usb_get_ep_configured(void) {
    return switch_pro_state ? switch_pro_state->ep_configured : false;
}

uint16_t switch_pro_usb_get_dropped_replies(void) {
    return switch_pro_state ? switch_pro_state->dropped_replies : 0;
}

uint8_t switch_pro_usb_get_queue_depth(void) {
    if(!switch_pro_state) return 0;
    uint8_t h = switch_pro_state->reply_head;
    uint8_t t = switch_pro_state->reply_tail;
    return (h >= t) ? (h - t) : (REPLY_QUEUE_SIZE - t + h);
}

bool switch_pro_usb_get_imu_enabled(void) {
    return switch_pro_state ? switch_pro_state->imu_enabled : false;
}

uint8_t switch_pro_usb_get_spi_read_count(void) {
    return switch_pro_state ? switch_pro_state->spi_read_count : 0;
}

void switch_pro_usb_get_spi_addrs(uint32_t* out, uint8_t max) {
    if(!switch_pro_state) return;
    uint8_t n = switch_pro_state->spi_read_count;
    if(n > max) n = max;
    if(n > 4) n = 4;
    for(uint8_t i = 0; i < n; i++) out[i] = switch_pro_state->spi_addrs[i];
}

void switch_pro_usb_get_subcmd_log(uint8_t* out, uint8_t* count) {
    if(!switch_pro_state) { *count = 0; return; }
    uint8_t n = switch_pro_state->subcmd_log_idx;
    if(n > 8) n = 8;
    *count = n;
    // Copy most recent subcmds in order
    for(uint8_t i = 0; i < n; i++) {
        uint8_t idx = (switch_pro_state->subcmd_log_idx - n + i) % 8;
        out[i] = switch_pro_state->subcmd_log[idx];
    }
}
