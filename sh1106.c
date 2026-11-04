/**
 * @file sh1106.c
 * @brief SH1106 128x64 I2C OLED Driver Implementation
 *
 * Implements SH1106 RAM 2-column offset management (columns 2 to 129),
 * 128x64 frame buffer (1024 bytes), graphics primitives, and 5x7 font.
 */

#include "sh1106.h"
#include "pin_config.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "SH1106";
static uint8_t s_buffer[SH1106_WIDTH * SH1106_PAGES];
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_i2c_dev = NULL;

// 5x7 ASCII font table
static const uint8_t s_font5x7[][5] = {
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00},
    ['!'] = {0x00, 0x00, 0x5F, 0x00, 0x00},
    ['"'] = {0x00, 0x07, 0x00, 0x07, 0x00},
    ['#'] = {0x14, 0x7F, 0x14, 0x7F, 0x14},
    ['$'] = {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    ['%'] = {0x23, 0x13, 0x08, 0x64, 0x62},
    ['&'] = {0x36, 0x49, 0x55, 0x22, 0x50},
    [39] = {0x00, 0x05, 0x03, 0x00, 0x00},
    ['('] = {0x00, 0x1C, 0x22, 0x41, 0x00},
    [')'] = {0x00, 0x41, 0x22, 0x1C, 0x00},
    ['*'] = {0x14, 0x08, 0x3E, 0x08, 0x14},
    ['+'] = {0x08, 0x08, 0x3E, 0x08, 0x08},
    [','] = {0x00, 0x50, 0x30, 0x00, 0x00},
    ['-'] = {0x08, 0x08, 0x08, 0x08, 0x08},
    ['.'] = {0x00, 0x60, 0x60, 0x00, 0x00},
    ['/'] = {0x20, 0x10, 0x08, 0x04, 0x02},
    ['0'] = {0x3E, 0x51, 0x49, 0x45, 0x3E},
    ['1'] = {0x00, 0x42, 0x7F, 0x40, 0x00},
    ['2'] = {0x42, 0x61, 0x51, 0x49, 0x46},
    ['3'] = {0x21, 0x41, 0x45, 0x4B, 0x31},
    ['4'] = {0x18, 0x14, 0x12, 0x7F, 0x10},
    ['5'] = {0x27, 0x45, 0x45, 0x45, 0x39},
    ['6'] = {0x3C, 0x4A, 0x49, 0x49, 0x30},
    ['7'] = {0x01, 0x71, 0x09, 0x05, 0x03},
    ['8'] = {0x36, 0x49, 0x49, 0x49, 0x36},
    ['9'] = {0x06, 0x49, 0x49, 0x29, 0x1E},
    [':'] = {0x00, 0x36, 0x36, 0x00, 0x00},
    [';'] = {0x00, 0x56, 0x36, 0x00, 0x00},
    ['<'] = {0x08, 0x14, 0x22, 0x41, 0x00},
    ['='] = {0x14, 0x14, 0x14, 0x14, 0x14},
    ['>'] = {0x00, 0x41, 0x22, 0x14, 0x08},
    ['?'] = {0x02, 0x01, 0x51, 0x09, 0x06},
    ['@'] = {0x32, 0x49, 0x79, 0x41, 0x3E},
    ['A'] = {0x7E, 0x11, 0x11, 0x11, 0x7E},
    ['B'] = {0x7F, 0x49, 0x49, 0x49, 0x36},
    ['C'] = {0x3E, 0x41, 0x41, 0x41, 0x22},
    ['D'] = {0x7F, 0x41, 0x41, 0x22, 0x1C},
    ['E'] = {0x7F, 0x49, 0x49, 0x49, 0x41},
    ['F'] = {0x7F, 0x09, 0x09, 0x09, 0x01},
    ['G'] = {0x3E, 0x41, 0x49, 0x49, 0x7A},
    ['H'] = {0x7F, 0x08, 0x08, 0x08, 0x7F},
    ['I'] = {0x00, 0x41, 0x7F, 0x41, 0x00},
    ['J'] = {0x20, 0x40, 0x41, 0x3F, 0x01},
    ['K'] = {0x7F, 0x08, 0x14, 0x22, 0x41},
    ['L'] = {0x7F, 0x40, 0x40, 0x40, 0x40},
    ['M'] = {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    ['N'] = {0x7F, 0x04, 0x08, 0x10, 0x7F},
    ['O'] = {0x3E, 0x41, 0x41, 0x41, 0x3E},
    ['P'] = {0x7F, 0x09, 0x09, 0x09, 0x06},
    ['Q'] = {0x3E, 0x41, 0x51, 0x21, 0x5E},
    ['R'] = {0x7F, 0x09, 0x19, 0x29, 0x46},
    ['S'] = {0x46, 0x49, 0x49, 0x49, 0x31},
    ['T'] = {0x01, 0x01, 0x7F, 0x01, 0x01},
    ['U'] = {0x3F, 0x40, 0x40, 0x40, 0x3F},
    ['V'] = {0x1F, 0x20, 0x40, 0x20, 0x1F},
    ['W'] = {0x7F, 0x20, 0x18, 0x20, 0x7F},
    ['X'] = {0x63, 0x14, 0x08, 0x14, 0x63},
    ['Y'] = {0x07, 0x08, 0x70, 0x08, 0x07},
    ['Z'] = {0x61, 0x51, 0x49, 0x45, 0x43},
    ['['] = {0x00, 0x7F, 0x41, 0x41, 0x00},
    ['\\'] = {0x02, 0x04, 0x08, 0x10, 0x20},
    [']'] = {0x00, 0x41, 0x41, 0x7F, 0x00},
    ['_'] = {0x40, 0x40, 0x40, 0x40, 0x40},
    ['a'] = {0x20, 0x54, 0x54, 0x54, 0x78},
    ['b'] = {0x7F, 0x48, 0x44, 0x44, 0x38},
    ['c'] = {0x38, 0x44, 0x44, 0x44, 0x20},
    ['d'] = {0x38, 0x44, 0x44, 0x48, 0x7F},
    ['e'] = {0x38, 0x54, 0x54, 0x54, 0x18},
    ['f'] = {0x08, 0x7E, 0x09, 0x01, 0x02},
    ['g'] = {0x0C, 0x52, 0x52, 0x52, 0x3E},
    ['h'] = {0x7F, 0x08, 0x04, 0x04, 0x78},
    ['i'] = {0x00, 0x44, 0x7D, 0x40, 0x00},
    ['j'] = {0x20, 0x40, 0x44, 0x3D, 0x00},
    ['k'] = {0x7F, 0x10, 0x28, 0x44, 0x00},
    ['l'] = {0x00, 0x41, 0x7F, 0x40, 0x00},
    ['m'] = {0x7C, 0x04, 0x18, 0x04, 0x78},
    ['n'] = {0x7C, 0x08, 0x04, 0x04, 0x78},
    ['o'] = {0x38, 0x44, 0x44, 0x44, 0x38},
    ['p'] = {0x7C, 0x14, 0x14, 0x14, 0x08},
    ['q'] = {0x08, 0x14, 0x14, 0x18, 0x7C},
    ['r'] = {0x7C, 0x08, 0x04, 0x04, 0x08},
    ['s'] = {0x48, 0x54, 0x54, 0x54, 0x20},
    ['t'] = {0x04, 0x3F, 0x44, 0x40, 0x20},
    ['u'] = {0x3C, 0x40, 0x40, 0x20, 0x7C},
    ['v'] = {0x1C, 0x20, 0x40, 0x20, 0x1C},
    ['w'] = {0x3C, 0x40, 0x30, 0x40, 0x3C},
    ['x'] = {0x44, 0x28, 0x10, 0x28, 0x44},
    ['y'] = {0x0C, 0x50, 0x50, 0x50, 0x3C},
    ['z'] = {0x44, 0x64, 0x54, 0x4C, 0x44},
};

static esp_err_t sh1106_send_cmd(uint8_t cmd) {
    uint8_t data[2] = {0x00, cmd}; // Co = 0, D/C# = 0
    return i2c_master_transmit(s_i2c_dev, data, sizeof(data), 100);
}

esp_err_t sh1106_init(void) {
    if (s_i2c_dev != NULL) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_MASTER_PORT,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SH1106_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_i2c_dev));

    // SH1106 initialization sequence
    sh1106_send_cmd(0xAE); // Display OFF
    sh1106_send_cmd(0x02); // Set Lower Column Address (2 for SH1106 offset)
    sh1106_send_cmd(0x10); // Set Higher Column Address
    sh1106_send_cmd(0x40); // Set Display Start Line 0
    sh1106_send_cmd(0xB0); // Set Page Address 0
    sh1106_send_cmd(0x81); // Set Contrast Control
    sh1106_send_cmd(0xCF);
    sh1106_send_cmd(0xA1); // Segment Re-map (A0 or A1)
    sh1106_send_cmd(0xA6); // Normal display
    sh1106_send_cmd(0xA8); // Multiplex Ratio
    sh1106_send_cmd(0x3F); // 1/64 duty
    sh1106_send_cmd(0xC8); // COM Output Scan Direction
    sh1106_send_cmd(0xD3); // Display Offset
    sh1106_send_cmd(0x00);
    sh1106_send_cmd(0xD5); // Display Clock Divide
    sh1106_send_cmd(0x80);
    sh1106_send_cmd(0xD9); // Pre-charge Period
    sh1106_send_cmd(0xF1);
    sh1106_send_cmd(0xDA); // COM Pins config
    sh1106_send_cmd(0x12);
    sh1106_send_cmd(0xDB); // VCOMH Deselect
    sh1106_send_cmd(0x40);
    sh1106_send_cmd(0x8D); // Charge Pump
    sh1106_send_cmd(0x14);
    sh1106_send_cmd(0xAF); // Display ON

    sh1106_clear();
    sh1106_flush();

    ESP_LOGI(TAG, "SH1106 OLED 128x64 initialized on SDA=%d, SCL=%d", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    return ESP_OK;
}

void sh1106_clear(void) {
    memset(s_buffer, 0x00, sizeof(s_buffer));
}

void sh1106_draw_pixel(int x, int y, bool on) {
    if (x < 0 || x >= SH1106_WIDTH || y < 0 || y >= SH1106_HEIGHT) return;
    int page = y / 8;
    int bit = y % 8;
    int idx = page * SH1106_WIDTH + x;

    if (on) {
        s_buffer[idx] |= (1 << bit);
    } else {
        s_buffer[idx] &= ~(1 << bit);
    }
}

void sh1106_draw_line(int x0, int y0, int x1, int y1, bool on) {
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        sh1106_draw_pixel(x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void sh1106_draw_rect(int x, int y, int w, int h, bool on) {
    sh1106_draw_line(x, y, x + w - 1, y, on);
    sh1106_draw_line(x, y + h - 1, x + w - 1, y + h - 1, on);
    sh1106_draw_line(x, y, x, y + h - 1, on);
    sh1106_draw_line(x + w - 1, y, x + w - 1, y + h - 1, on);
}

void sh1106_fill_rect(int x, int y, int w, int h, bool on) {
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            sh1106_draw_pixel(x + i, y + j, on);
        }
    }
}

void sh1106_draw_char(int x, int y, char c, bool on) {
    if (c < 32 || c > 126) c = ' ';
    const uint8_t *glyph = s_font5x7[(uint8_t)c];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            bool pixel_on = (bits & (1 << row)) != 0;
            sh1106_draw_pixel(x + col, y + row, on ? pixel_on : !pixel_on);
        }
    }
}

void sh1106_draw_string(int x, int y, const char *str, bool on) {
    while (*str) {
        sh1106_draw_char(x, y, *str++, on);
        x += 6;
        if (x >= SH1106_WIDTH) break;
    }
}

void sh1106_draw_string_inverted(int x, int y, const char *str) {
    int len = strlen(str);
    sh1106_fill_rect(x - 1, y - 1, len * 6 + 1, 9, true);
    while (*str) {
        sh1106_draw_char(x, y, *str++, false);
        x += 6;
        if (x >= SH1106_WIDTH) break;
    }
}

void sh1106_draw_progress_bar(int x, int y, int w, int h, float percent) {
    sh1106_draw_rect(x, y, w, h, true);
    percent = fmaxf(0.0f, fminf(1.0f, percent));
    int fill_w = (int)((w - 4) * percent);
    if (fill_w > 0) {
        sh1106_fill_rect(x + 2, y + 2, fill_w, h - 4, true);
    }
}

void sh1106_flush(void) {
    // SH1106 is 132 columns wide; display 128 is offset by 2 columns (0x02 column start)
    uint8_t page_buf[1 + SH1106_WIDTH];
    page_buf[0] = 0x40; // Data mode

    for (uint8_t page = 0; page < SH1106_PAGES; page++) {
        sh1106_send_cmd(0xB0 + page); // Set page
        sh1106_send_cmd(0x02);        // Set lower column address = 2 (SH1106 offset)
        sh1106_send_cmd(0x10);        // Set higher column address = 0

        memcpy(&page_buf[1], &s_buffer[page * SH1106_WIDTH], SH1106_WIDTH);
        i2c_master_transmit(s_i2c_dev, page_buf, sizeof(page_buf), 100);
    }
}
