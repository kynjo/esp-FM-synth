/**
 * @file pin_config.h
 * @brief Pin definitions for ESP32-S3 SuperMini FM Synthesizer
 *
 * Target: ESP32-S3 SuperMini (Xtensa Dual-Core LX7, 240MHz)
 * DAC: PCM5102A I2S Module (3.3V)
 * OLED: SH1106 128x64 I2C (Address 0x3C)
 * Control: EC11 Rotary Encoder with Push Button + 2 Tactile Buttons
 * USB: Native USB Type-C (TinyUSB MIDI Device on GPIO 19 / 20)
 */

#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

#include "driver/gpio.h"
#include "driver/i2s_std.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * 1. I2S DAC (PCM5102A) PIN CONFIGURATION
 * ==========================================================================
 * Wiring:
 *  - PCM5102A BCK  -> GPIO 4
 *  - PCM5102A LRCK / WS -> GPIO 5
 *  - PCM5102A DIN  -> GPIO 6
 *  - PCM5102A SCK  -> GND (PCM5102A generates internal master clock from BCK via PLL)
 *  - PCM5102A FMT  -> GND (I2S standard format)
 *  - PCM5102A XMT  -> 3.3V (Soft mute disabled / active audio)
 *  - PCM5102A FLT  -> GND (Normal latency filter)
 *  - PCM5102A VIN  -> 3.3V or 5V
 *  - PCM5102A GND  -> GND
 */
#define I2S_BCK_IO        (GPIO_NUM_4)
#define I2S_WS_IO         (GPIO_NUM_5)
#define I2S_DATA_OUT_IO   (GPIO_NUM_6)
#define I2S_DATA_IN_IO    (I2S_GPIO_UNUSED)

/* ==========================================================================
 * 2. I2C DISPLAY (SH1106 128x64 OLED)
 * ==========================================================================
 * Wiring:
 *  - SH1106 SDA    -> GPIO 8
 *  - SH1106 SCL    -> GPIO 9
 *  - SH1106 VCC    -> 3.3V
 *  - SH1106 GND    -> GND
 * Note: SH1106 I2C default address is 0x3C
 */
#define I2C_MASTER_SDA_IO (GPIO_NUM_8)
#define I2C_MASTER_SCL_IO (GPIO_NUM_9)
#define I2C_MASTER_PORT   (I2C_NUM_0)
#define I2C_MASTER_FREQ_HZ (400000)
#define SH1106_I2C_ADDR   (0x3C)

/* ==========================================================================
 * 3. ROTARY ENCODER (EC11 WITH INTEGRATED PUSH BUTTON)
 * ==========================================================================
 * Wiring:
 *  - Encoder CLK (A) -> GPIO 1 (Internal Pullup enabled)
 *  - Encoder DT (B)  -> GPIO 2 (Internal Pullup enabled)
 *  - Encoder SW (Btn)-> GPIO 3 (Internal Pullup enabled, Active Low)
 *  - Common GND      -> GND
 */
#define ENCODER_PIN_CLK   (GPIO_NUM_1)
#define ENCODER_PIN_DT    (GPIO_NUM_2)
#define ENCODER_PIN_SW    (GPIO_NUM_3)

/* ==========================================================================
 * 4. TACTILE NAVIGATION BUTTONS (MENU PREV / NEXT)
 * ==========================================================================
 * Wiring:
 *  - Button PREV (Page Left)  -> GPIO 10 (Internal Pullup enabled, Active Low)
 *  - Button NEXT (Page Right) -> GPIO 11 (Internal Pullup enabled, Active Low)
 *  - Button other pins        -> GND
 */
#define BTN_NAV_PREV_IO   (GPIO_NUM_10)
#define BTN_NAV_NEXT_IO   (GPIO_NUM_11)

/* ==========================================================================
 * 5. USB MIDI (NATIVE ESP32-S3 USB PHY)
 * ==========================================================================
 * Handled internally by ESP32-S3 USB OTG peripheral:
 *  - USB D- -> GPIO 19 (Directly routed to SuperMini USB-C port)
 *  - USB D+ -> GPIO 20 (Directly routed to SuperMini USB-C port)
 */
#define USB_DM_IO         (GPIO_NUM_19)
#define USB_DP_IO         (GPIO_NUM_20)

/* ==========================================================================
 * 6. OPTIONAL STATUS / BEAT LED
 * ==========================================================================
 */
#define STATUS_LED_IO     (GPIO_NUM_7)

#ifdef __cplusplus
}
#endif

#endif // PIN_CONFIG_H
