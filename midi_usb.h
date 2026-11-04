/**
 * @file midi_usb.h
 * @brief USB-MIDI Device Class for ESP32-S3 SuperMini
 */

#ifndef MIDI_USB_H
#define MIDI_USB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void usb_midi_init(void);
void usb_midi_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // MIDI_USB_H
