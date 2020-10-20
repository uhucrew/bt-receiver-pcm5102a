#pragma once


/*******************************************************************************
 * File:        lcd.h
 * Project:     SP18 - I2C OLED Display
 * Author:      Nicolas Pannwitz
 * Web:         http://pic-projekte.de
 ******************************************************************************/

#include <stdint.h>

/*******************************************************************************
 * Makros
 */

#define SSD1306_DEFAULT_ADDRESS     0x78
#define SSD1306_SETCONTRAST         0x81
#define SSD1306_DISPLAYALLON_RESUME 0xA4
#define SSD1306_DISPLAYALLON        0xA5
#define SSD1306_NORMALDISPLAY       0xA6
#define SSD1306_INVERTDISPLAY       0xA7
#define SSD1306_DISPLAYOFF          0xAE
#define SSD1306_DISPLAYON           0xAF
#define SSD1306_SETDISPLAYOFFSET    0xD3
#define SSD1306_SETCOMPINS          0xDA
#define SSD1306_SETVCOMDETECT       0xDB
#define SSD1306_SETDISPLAYCLOCKDIV  0xD5
#define SSD1306_SETPRECHARGE        0xD9
#define SSD1306_SETMULTIPLEX        0xA8
#define SSD1306_SETLOWCOLUMN        0x00
#define SSD1306_SETHIGHCOLUMN       0x10
#define SSD1306_SETSTARTLINE        0x40
#define SSD1306_MEMORYMODE          0x20
#define SSD1306_COLUMNADDR          0x21
#define SSD1306_PAGEADDR            0x22
#define SSD1306_COMSCANINC          0xC0
#define SSD1306_COMSCANDEC          0xC8
#define SSD1306_SEGREMAP            0xA0
#define SSD1306_CHARGEPUMP          0x8D
#define SSD1306_SWITCHCAPVCC        0x02
#define SSD1306_NOP                 0xE3

#define SSD1306_WIDTH               128
#define SSD1306_HEIGHT              64

#define SSD1306_BUFFERSIZE (SSD1306_WIDTH*SSD1306_HEIGHT)/8


void lcd_init(void);
void lcd_send_framebuffer(uint8_t *buffer);
void lcd_invert(uint8_t inverted);
void lcd_send_command(uint8_t command);
void lcd_send_data(uint8_t data);

void fb_draw_pixel(uint8_t pos_x, uint8_t pos_y, uint8_t pixel_status);
void fb_draw_v_line(uint8_t x, uint8_t y, uint8_t length);
void fb_draw_h_line(uint8_t x, uint8_t y, uint8_t length);
void fb_draw_rectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t fill);
void fb_clear_rectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);
void fb_clear();
void fb_clear_line(uint8_t line);
void fb_clear_line_part(uint8_t line, uint8_t start_x, uint8_t end_x);
void fb_invert(uint8_t status);
void fb_show();
void fb_show_bmp(uint8_t *pBmp);
void fb_draw_char (uint16_t x, uint16_t y, uint16_t fIndex);
void fb_draw_string (uint16_t x, uint16_t y, const char *s);
void fb_draw_string_big (uint16_t x, uint16_t y, const char *s);
