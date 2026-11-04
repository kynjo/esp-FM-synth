/**
 * @file sh1106.h
 * @brief SH1106 128x64 I2C OLED driver API.
 */

#ifndef SH1106_H
#define SH1106_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SH1106_WIDTH  128
#define SH1106_HEIGHT 64
#define SH1106_PAGES  (SH1106_HEIGHT / 8)

esp_err_t sh1106_init(void);
void sh1106_clear(void);
void sh1106_draw_pixel(int x, int y, bool on);
void sh1106_draw_line(int x0, int y0, int x1, int y1, bool on);
void sh1106_draw_rect(int x, int y, int w, int h, bool on);
void sh1106_fill_rect(int x, int y, int w, int h, bool on);
void sh1106_draw_char(int x, int y, char c, bool on);
void sh1106_draw_string(int x, int y, const char *str, bool on);
void sh1106_draw_string_inverted(int x, int y, const char *str);
void sh1106_draw_progress_bar(int x, int y, int w, int h, float percent);
void sh1106_flush(void);

#ifdef __cplusplus
}
#endif

#endif // SH1106_H
