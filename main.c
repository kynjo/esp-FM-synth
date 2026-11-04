/**
 * @file main.c
 * @brief ESP32-S3 SuperMini 3-Operator Polyphonic Phase Modulation Synthesizer
 *
 * Multicore Architecture:
 * - CORE 0:
 *    * UI & SH1106 Display rendering task (30 FPS)
 *    * EC11 Rotary Encoder & 2 tactile navigation buttons scan
 *    * TinyUSB USB-MIDI stack task
 *
 * - CORE 1:
 *    * Real-time DSP Audio Synthesis Task (Priority configMAX_PRIORITIES - 2)
 *    * 8-Voice Polyphonic FM/PM synthesis with POLY/MONO/LEGATO/UNISON modes with 2048 Sine LUT
 *    * State Variable Filter (SVF), ADSR, LFOs
 *    * I2S DMA streaming to PCM5102A DAC (44.1kHz Stereo 16-bit)
 */

#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "synth_engine.h"
#include "ui_menu.h"
#include "midi_usb.h"
#include "preset_manager.h"
#include "pin_config.h"

static const char *TAG = "MAIN";

void app_main(void) {
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "  ESP32-S3 SuperMini 3-Op FM Synthesizer Studio  ");
    ESP_LOGI(TAG, "=================================================");

    // 1. Initialize NVS (kept for project compatibility; V14 does not persist patches)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize Synth DSP Engine
    synth_engine_init();

    // Build the factory preset bank before starting the audio task.
    preset_manager_init();
    preset_manager_load(0);

    // 3. Spawn Real-time Audio DSP Task on CORE 1 (High priority)
    xTaskCreatePinnedToCore(
        synth_audio_task,
        "synth_audio",
        8192,
        NULL,
        configMAX_PRIORITIES - 2, // Highest real-time audio priority
        NULL,
        1 // Pinned to Core 1
    );
    ESP_LOGI(TAG, "Spawned synth_audio_task on Core 1");

    // 4. Spawn UI & Display Task on CORE 0
    xTaskCreatePinnedToCore(
        ui_task,
        "ui_task",
        4096,
        NULL,
        5,
        NULL,
        0 // Pinned to Core 0
    );
    ESP_LOGI(TAG, "Spawned ui_task on Core 0");

    // 5. Spawn USB-MIDI Task on CORE 0
    xTaskCreatePinnedToCore(
        usb_midi_task,
        "usb_midi",
        4096,
        NULL,
        6,
        NULL,
        0 // Pinned to Core 0
    );
    ESP_LOGI(TAG, "Spawned usb_midi_task on Core 0");

    ESP_LOGI(TAG, "All system tasks initialized and running!");
}
