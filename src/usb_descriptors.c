/*
 * USB Descriptors - picoamp-2 + HID
 * Based on BambooMaster's picoamp-2
 */

#include "bsp/board_api.h"
#include "tusb.h"
#include "usb_descriptors.h"

//--------------------------------------------------------------------+
// HID Report Descriptor
//--------------------------------------------------------------------+
uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
    
    // Mouse with scroll wheel
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_MOUSE),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
        HID_REPORT_ID(REPORT_ID_MOUSE)
        HID_USAGE(HID_USAGE_DESKTOP_POINTER),
        HID_COLLECTION(HID_COLLECTION_PHYSICAL),
            // Buttons
            HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON),
            HID_USAGE_MIN(1),
            HID_USAGE_MAX(3),
            HID_LOGICAL_MIN(0),
            HID_LOGICAL_MAX(1),
            HID_REPORT_COUNT(3),
            HID_REPORT_SIZE(1),
            HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
            // Padding
            HID_REPORT_COUNT(1),
            HID_REPORT_SIZE(5),
            HID_INPUT(HID_CONSTANT),
            // X, Y movement
            HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
            HID_USAGE(HID_USAGE_DESKTOP_X),
            HID_USAGE(HID_USAGE_DESKTOP_Y),
            HID_LOGICAL_MIN(0x81),
            HID_LOGICAL_MAX(0x7f),
            HID_REPORT_COUNT(2),
            HID_REPORT_SIZE(8),
            HID_INPUT(HID_DATA | HID_VARIABLE | HID_RELATIVE),
            // Scroll wheel
            HID_USAGE(HID_USAGE_DESKTOP_WHEEL),
            HID_LOGICAL_MIN(0x81),
            HID_LOGICAL_MAX(0x7f),
            HID_REPORT_COUNT(1),
            HID_REPORT_SIZE(8),
            HID_INPUT(HID_DATA | HID_VARIABLE | HID_RELATIVE),
        HID_COLLECTION_END,
    HID_COLLECTION_END
};

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
#define USB_PID 0x4011  // Audio + HID

tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = 0xCafe,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const* tud_descriptor_device_cb(void) {
    return (uint8_t const*)&desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
#define EPNUM_AUDIO_FB    0x01
#define EPNUM_AUDIO_OUT   0x01
#define EPNUM_HID         0x02

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_AUDIO_HEADSET_STEREO_DESC_LEN + TUD_HID_DESC_LEN)

uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // Audio descriptor (same as picoamp-2)
    TUD_AUDIO_HEADSET_STEREO_DESCRIPTOR(4, EPNUM_AUDIO_OUT, EPNUM_AUDIO_FB | 0x80, 4),

    // HID descriptor
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 6, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), 0x80 | EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 10)
};

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+
char const* string_desc_arr[] = {
    (const char[]){0x09, 0x04},  // 0: Language ID
    "sctanf",                     // 1: Manufacturer
    "Pico Amp 2 + HID",          // 2: Product
    NULL,                         // 3: Serial (use board serial)
    "Pico Amp 2",                // 4: Audio Interface
    "Pico Amp 2",                // 5: Audio Interface
    "HID Device"                 // 6: HID Interface
};

static uint16_t _desc_str[32 + 1];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count;

    switch (index) {
        case 0:
            memcpy(&_desc_str[1], string_desc_arr[0], 2);
            chr_count = 1;
            break;

        case 3:
            chr_count = board_usb_get_serial(_desc_str + 1, 32);
            break;

        default:
            if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) return NULL;

            const char* str = string_desc_arr[index];
            chr_count = strlen(str);
            size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;
            if (chr_count > max_count) chr_count = max_count;

            for (size_t i = 0; i < chr_count; i++) {
                _desc_str[1 + i] = str[i];
            }
            break;
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}

//--------------------------------------------------------------------+
// HID Callbacks
//--------------------------------------------------------------------+
uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return desc_hid_report;
}
