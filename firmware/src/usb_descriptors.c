///usb_descriptors.c
#include "tusb.h"
#include "protocol.h"

// --- 1. Дескрипторы (Данные) ---

// Device Descriptor
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_VENDOR_SPECIFIC,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = PICOLINK_VID,
    .idProduct          = PICOLINK_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

// Configuration Descriptor
uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN, 0, 100),

    // Vendor Interface: itf_num, str_idx, ep_out, ep_in, buf_size
    TUD_VENDOR_DESCRIPTOR(0, 0, 0x01, 0x81, 64)
};

// String Descriptors
char const* string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 }, // 0: Supported language is English (0x0409)
    "PicoLink Team",               // 1: Manufacturer
    "PicoLink MFD Bridge",         // 2: Product
    "SN001",                       // 3: Serials
};

static uint16_t _desc_str[32];

// --- 2. Callback-функции (Логика) ---

uint8_t const * tud_descriptor_device_cb(void) {
    return (uint8_t const *) &desc_device;
}

uint8_t const * tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_configuration;
}

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    uint8_t chr_count;

    if ( index == 0 ) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if ( !(index < sizeof(string_desc_arr)/sizeof(string_desc_arr[0])) ) return NULL;
        const char* str = string_desc_arr[index];
        chr_count = strlen(str);
        if ( chr_count > 31 ) chr_count = 31;
        for(uint8_t i=0; i<chr_count; i++) _desc_str[1+i] = str[i];
    }

    _desc_str[0] = (TUSB_DESC_STRING << 8 ) | (2*chr_count + 2);
    return _desc_str;
}