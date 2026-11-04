/**
 * @file midi_usb.c
 * @brief USB-MIDI Device Handler for ESP32-S3 Native USB
 *
 * Uses TinyUSB MIDI class to receive Note-On, Note-Off, Pitch Bend,
 * and Continuous Controllers from PC / DAW over USB Type-C.
 */

#include "midi_usb.h"
#include "synth_engine.h"
#include "preset_manager.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "USB_MIDI";

void usb_midi_init(void) {
    ESP_LOGI(TAG, "Initializing TinyUSB MIDI stack");
    extern const tusb_desc_device_t desc_device;
    extern uint8_t const desc_fs_configuration[];
    extern char const* string_desc_arr[];

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &desc_device,
        .string_descriptor = string_desc_arr,
        .string_descriptor_count = 4,
        .external_phy = false,
        .configuration_descriptor = desc_fs_configuration,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "TinyUSB installed successfully");
}

void usb_midi_task(void *pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "USB MIDI task running on Core %d", xPortGetCoreID());
    usb_midi_init();

    uint8_t packet[4];

    while (1) {
        if (tud_midi_available()) {
            while (tud_midi_packet_read(packet)) {
                uint8_t header = packet[0];
                (void)header; // Cable/Code Index is not used by the synth.
                uint8_t status = packet[1];
                uint8_t data1  = packet[2];
                uint8_t data2  = packet[3];

                uint8_t msg_type = status & 0xF0;

                switch (msg_type) {
                    case 0x90: // Note On
                        if (data2 > 0) {
                            synth_note_on(data1, data2);
                        } else {
                            synth_note_off(data1);
                        }
                        break;

                    case 0x80: // Note Off
                        synth_note_off(data1);
                        break;

                    case 0xE0: { // Pitch Bend
                        int16_t bend = (data2 << 7) | data1;
                        synth_set_pitch_bend(bend);
                        break;
                    }

                    case 0xB0: // Control Change (CC)
                        synth_set_cc(data1, data2);
                        break;

                    case 0xC0: // Program Change -> preset 1..16
                        preset_manager_load((uint8_t)(data1 % PRESET_COUNT));
                        break;

                    default:
                        break;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
