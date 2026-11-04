#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

// Общая конфигурация для ESP32-S3 и FreeRTOS
#define CFG_TUSB_MCU                OPT_MCU_ESP32S3
#define CFG_TUSB_OS                 OPT_OS_FREERTOS
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUSB_DEBUG              0

#define CFG_TUD_ENABLED             1
#define CFG_TUD_MAX_SPEED           OPT_MODE_DEFAULT_SPEED
#define CFG_TUD_ENDPOINT0_SIZE      64

// Включаем MIDI и отключаем лишние классы (CDC, MSC, HID)
#define CFG_TUD_MIDI                1
#define CFG_TUD_CDC                 0
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_VENDOR              0

// Размеры буферов MIDI FIFO
#define CFG_TUD_MIDI_RX_BUFSIZE     64
#define CFG_TUD_MIDI_TX_BUFSIZE     64

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
