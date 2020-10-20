/*******************************************************************************
 * File:        lcd.c
 * Project:     SP18 - I2C OLED Display
 * Author:      Nicolas Pannwitz
 * Version:     
 * Web:         http://pic-projekte.de
 ******************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include "lcd.h"
#include "../i2c_x.h"
#include "font.h"

/*******************************************************************************
 * Framebuffer
 */

static uint8_t buffer[1024];
static uint8_t buffer_mirror[1024];

/*******************************************************************************
 * Initialisierung des OLED-Displays
 */

void lcd_init(void)
{
    //display off
    lcd_send_command(SSD1306_DISPLAYOFF);

    lcd_send_command(SSD1306_SETDISPLAYCLOCKDIV);
    lcd_send_command(0x80);

    lcd_send_command(SSD1306_SETMULTIPLEX);
    lcd_send_command(0x3F);

    lcd_send_command(SSD1306_SETDISPLAYOFFSET);
    lcd_send_command(0x00);

    lcd_send_command(SSD1306_SETSTARTLINE | 0x00);

    lcd_send_command(SSD1306_CHARGEPUMP);
    lcd_send_command(0x14);

    lcd_send_command(SSD1306_MEMORYMODE);
    lcd_send_command(0x00);

    lcd_send_command(SSD1306_SEGREMAP | 0x01);

    lcd_send_command(SSD1306_COMSCANDEC);

    lcd_send_command(SSD1306_SETCOMPINS);
    lcd_send_command(0x12);

    lcd_send_command(SSD1306_SETCONTRAST);
    lcd_send_command(0xCF);

    lcd_send_command(SSD1306_SETPRECHARGE);
    lcd_send_command(0xF1);

    lcd_send_command(SSD1306_SETVCOMDETECT);
    lcd_send_command(0x40);

    lcd_send_command(SSD1306_DISPLAYALLON_RESUME);

    lcd_send_command(SSD1306_NORMALDISPLAY);

    fb_clear();
    lcd_send_framebuffer(buffer);

    //display on
    lcd_send_command(SSD1306_DISPLAYON);
}


void lcd_send_command(uint8_t command)
{
    uint8_t cmd_buf[2];
    cmd_buf[0] = 0;
    cmd_buf[1] = command;
    i2c_master_write_slave(SSD1306_DEFAULT_ADDRESS, &cmd_buf[0], 2, I2C_WAIT_MS_LONG);
}


void lcd_invert(uint8_t inverted)
{
    if (inverted) {
        lcd_send_command(SSD1306_INVERTDISPLAY);
    }
    else {
        lcd_send_command(SSD1306_NORMALDISPLAY);
    }
}


static void lcd_set_addr(uint8_t column, uint8_t page)
{
    lcd_send_command(SSD1306_COLUMNADDR);
    lcd_send_command(column);
    lcd_send_command(0x7F);

    lcd_send_command(SSD1306_PAGEADDR);
    lcd_send_command(page);
    lcd_send_command(0x07);
}


void lcd_send_framebuffer(uint8_t *buffer)
{
    uint8_t cmd_buf[17];
    cmd_buf[0] = 0x40;

    lcd_set_addr(0, 0);

    for (uint8_t packet = 0; packet < 64; packet++) {
        for (uint8_t i = 0; i < 16; i++) {
            cmd_buf[i + 1] = buffer[packet * 16 + i];
            buffer_mirror[packet * 16 + i] = buffer[packet * 16 + i];
        }
        i2c_master_write_slave(SSD1306_DEFAULT_ADDRESS, &cmd_buf[0], 17, I2C_WAIT_MS_DEFAULT);
    }
}


void lcd_update_framebuffer(uint8_t *buffer, uint8_t *buffer_mirror)
{
    uint8_t start_column = 0;
    uint8_t start_page = 0;
    uint8_t cmd_buf[17];
    bool chunk_changed = false;
    bool address_sent = false;
    bool address_sent_zero = false;
    cmd_buf[0] = 0x40;

    for (uint8_t packet = 0; packet < 64; packet++) {
        start_page = packet >> 3;
        for (uint8_t i = 0; i < 16; i++) {
            cmd_buf[i + 1] = buffer[packet * 16 + i];
            if (buffer_mirror[packet * 16 + i] != buffer[packet * 16 + i]) {
                chunk_changed = true;
                buffer_mirror[packet * 16 + i] = buffer[packet * 16 + i];
            }
        }
        if (chunk_changed) {
            if (address_sent == false) {
                address_sent = true;
                if (packet % 8 == 0) address_sent_zero = true;
                lcd_set_addr(start_column, start_page);
            }
            if (packet % 8 == 0 && !address_sent_zero) {
                lcd_set_addr(start_column, start_page);
            }
            i2c_master_write_slave(SSD1306_DEFAULT_ADDRESS, &cmd_buf[0], 17, I2C_WAIT_MS_DEFAULT);
        }
        else {
            address_sent = false;
            address_sent_zero = false;
        }
        start_column = (start_column + 16) % 128;
    }
}


void fb_draw_pixel(uint8_t pos_x, uint8_t pos_y, uint8_t pixel_status)
{
    if (pos_x >= SSD1306_WIDTH || pos_y >= SSD1306_HEIGHT) {
        return;
    }

    if (pixel_status) {
        buffer[pos_x + (pos_y / 8) * SSD1306_WIDTH] |= (1 << (pos_y & 7));
    }
    else {
        buffer[pos_x + (pos_y / 8) * SSD1306_WIDTH] &= ~(1 << (pos_y & 7));
    }
}


void fb_draw_v_line(uint8_t x, uint8_t y, uint8_t length)
{
    for (uint8_t i = 0; i < length; ++i) {
        fb_draw_pixel(x, i + y, 1);
    }
}


void fb_draw_h_line(uint8_t x, uint8_t y, uint8_t length)
{
    for (uint8_t i = 0; i < length; ++i) {
        fb_draw_pixel(i + x, y, 1);
    }
}


void fb_draw_rectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t fill)
{
    uint8_t length = x2 - x1 + 1;
    uint8_t height = y2 - y1;

    if (! fill) {
        fb_draw_h_line(x1, y1, length);
        fb_draw_h_line(x1, y2, length);
        fb_draw_v_line(x1, y1, height);
        fb_draw_v_line(x2, y1, height);
    }
    else {
        for (uint8_t x = 0; x < length; ++x) {
            for (uint8_t y = 0; y <= height; ++y) {
                fb_draw_pixel(x1 + x, y + y1, 1);
            }
        }
    }
}

void fb_clear_rectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2)
{
    uint8_t length = x2 - x1 + 1;
    uint8_t height = y2 - y1;

    for (uint8_t x = 0; x < length; ++x) {
        for (uint8_t y = 0; y <= height; ++y) {
            fb_draw_pixel(x1 + x, y + y1, 0);
        }
    }
}


void fb_clear_line(uint8_t line)
{
    fb_clear_rectangle(0, line * 8, 127, line * 8 + 7);
}


void fb_clear_line_part(uint8_t line, uint8_t start_x, uint8_t end_x)
{
    fb_clear_rectangle(start_x, line * 8, end_x, line * 8 + 7);
}


void fb_clear()
{
    for (uint16_t i = 0; i < SSD1306_BUFFERSIZE; i++) {
        buffer[i] = 0;
    }
}


void fb_invert(uint8_t status)
{
    lcd_invert(status);
}


void fb_show()
{
    lcd_update_framebuffer(buffer, buffer_mirror);
}

void fb_show_bmp(uint8_t *pBmp)
{
    lcd_update_framebuffer(pBmp, buffer_mirror);
}


void fb_draw_char (uint16_t x, uint16_t y, uint16_t fIndex)
{
    uint16_t bufIndex = (y << 7) + x;

    for(uint8_t j = 0; j < FONT_WIDTH; j++) {
        buffer[bufIndex + j] = font[fIndex + j + 1];
    }
}


void fb_draw_string (uint16_t x, uint16_t y, const char *s)
{
    while(*s) {
        /* index the width information of character <c> */
        uint16_t lIndex = 0;
        for(uint16_t k = 0; k < (*s - ' '); k++) {
            lIndex += (font[lIndex]) + 1;
        }

        /* draw character */
        fb_draw_char(x, y, lIndex);

        /* move the cursor forward for the next character */
        x += font[lIndex] + 1;

        /* next charachter */
        s++;
    }
}

void fb_draw_string_big (uint16_t x, uint16_t y, const char *s)
{
    while(*s) {
        for(uint8_t k = 0; k < 10; k++) {
            buffer[( y    << 7) + x + k] = font2[*s - ' '][k * 2  ];
            buffer[((y+1) << 7) + x + k] = font2[*s - ' '][k * 2 + 1];
        }

        x += 10;

        /* next charachter */
        s++;
    }
}
