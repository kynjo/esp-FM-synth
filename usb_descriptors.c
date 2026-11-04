#include "tusb.h"

// Device Descriptor
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = 0xCafe, // TinyUSB VID
    .idProduct          = 0x4001, // MIDI PID
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};


// Configuration Descriptor
enum {
    ITF_NUM_MIDI = 0,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)
#define EPNUM_MIDI_OUT    0x01
#define EPNUM_MIDI_IN     0x81

uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64)
};


// String Descriptors
char const* string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 }, // 0: English (0x0409)
    "Auricle",                   // 1: Производитель
    "FM Synth",           // 2: Имя устройства
    "123456",                      // 3: Серийный номер
};

