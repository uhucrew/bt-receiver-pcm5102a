#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "display.h"
#include "SSD1306/lcd.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
static const char *TAG = "DISPLAY";
#pragma GCC diagnostic pop



static char lcd_string_buffer[64];
static bool freezed = false;
extern const uint8_t *dev_name;
extern const uint8_t *last_device;
extern const int32_t default_sample_rate;

//ms per pixel decay rate
static uint32_t vu_decay = 51;
static uint8_t vu_x_start = 48 + 40;
static uint8_t vu_x_end_l = 48 + 40;
static uint8_t vu_x_end_r = 48 + 40;
static uint8_t vu_x_size = 127 - (48 + 40);
static float vu_x_10db_scale = (128.0 - (48.0 + 40.0)) / 7.0;
static float vu_x_db_scale = 0;
static float vu_db_min = 0;
static uint32_t vu_max = 0;
static uint32_t vu_min = 0;
static uint32_t vu_last_update_l_ms = 0;
static uint32_t vu_last_update_r_ms = 0;
static uint32_t vu_level[2] = { 0, 0 };

static void render_vu_meter();

void display_task() {
    ESP_LOGI(TAG, "display task core: %u", xPortGetCoreID());
    for (;;) {
        if (!freezed) {
            render_vu_meter();
            fb_show();
        }
        vTaskDelay(10 / portTICK_RATE_MS);
    }
}


static void init_vu_meter(uint32_t max) {
    vu_max = max;

    //80db -> factor 10000
    vu_min = max / 10000;
    vu_db_min = log10(vu_min);
    vu_x_db_scale = vu_x_size / (log10(max) - vu_db_min);

    float db_px = 127;
    while (db_px >= vu_x_start + 1) {
        fb_draw_pixel((uint8_t)round(db_px), 7 * 8 + 3, 1);
        fb_draw_pixel((uint8_t)round(db_px), 7 * 8 + 4, 1);
        db_px -= vu_x_10db_scale;
    }
}

void update_vu_meter(uint64_t level[2]) {
    vu_level[0] = level[0] >> 31;
    vu_level[1] = level[1] >> 31;
}

static uint8_t vu_x_end_l_new = 0;
static uint8_t vu_x_end_r_new = 0;

static void render_vu_meter() {
    if (freezed) return;
    static uint32_t now_ms;
    now_ms = esp_timer_get_time() / 1000;

    vu_x_end_l_new = round(vu_level[0] <= vu_min ? vu_x_start : (log10(vu_level[0]) - vu_db_min) * vu_x_db_scale + vu_x_start);
    vu_x_end_r_new = round(vu_level[1] <= vu_min ? vu_x_start : (log10(vu_level[1]) - vu_db_min) * vu_x_db_scale + vu_x_start);
    //ESP_LOGI(TAG, "%s:  vu_x_size: %u  vu_x_db_scale: %f  vu_db_min: %f  vu_level[0]: %u  vu_level[1]: %u  vu_x_end_l_new: %u  vu_x_end_r_new: %u", __func__, vu_x_size, vu_x_db_scale, vu_db_min, vu_level[0], vu_level[1], vu_x_end_l_new, vu_x_end_r_new);

    if (vu_x_end_l_new >= vu_x_end_l) {
        vu_x_end_l = vu_x_end_l_new;
        fb_draw_rectangle(vu_x_start, 7 * 8, vu_x_end_l, 7 * 8 + 2, 1);
        vu_last_update_l_ms = now_ms;
    }
    else {
        while (now_ms - vu_last_update_l_ms >= vu_decay && vu_x_end_l > vu_x_start && vu_x_end_l >= vu_x_end_l_new) {
            vu_last_update_l_ms += vu_decay;
            vu_x_end_l--;
        }
        fb_clear_rectangle(vu_x_end_l + 1, 7 * 8, 127, 7 * 8 + 2);
    }
    if (vu_x_end_r_new >= vu_x_end_r) {
        vu_x_end_r = vu_x_end_r_new;
        fb_draw_rectangle(vu_x_start, 7 * 8 + 5, vu_x_end_r, 7 * 8 + 7, 1);
        vu_last_update_r_ms = now_ms;
    }
    else {
        while (now_ms - vu_last_update_r_ms >= vu_decay && vu_x_end_r > vu_x_start && vu_x_end_r >= vu_x_end_r_new) {
            vu_last_update_r_ms += vu_decay;
            vu_x_end_r--;
        }
        fb_clear_rectangle(vu_x_end_r + 1, 7 * 8 + 5, 127, 7 * 8 + 7);
    }
}


void display_init() {
    lcd_init();
    fb_clear();
    snprintf(lcd_string_buffer, sizeof(lcd_string_buffer), "     %s", dev_name);
    fb_draw_string_big (0, 0, lcd_string_buffer);
    display_sample_rate(default_sample_rate);
    init_vu_meter(1 << 31);
    update_vu_meter((uint64_t[2]){ 0, 0 });

    xTaskCreate(
        display_task,           /* Task function. */
        "DisplayRefresh",       /* String with name of task. */
        10000,                  /* Stack size in bytes. */
        NULL,                   /* Parameter passed as input of the task */
        0,                      /* Priority of the task. */
        NULL
    );                          /* Task handle. */
}

void display_volume(uint8_t vol) {
    if (freezed) return;
    snprintf(lcd_string_buffer, sizeof(lcd_string_buffer), "%*d%%", 3, (uint32_t)vol * 100 / 0x7f);
    fb_draw_string_big(86, 5, lcd_string_buffer);
    fb_draw_rectangle(0, 5 * 8, vol * 83 / 0x7f, 7 * 8 - 3, 1);
    fb_clear_rectangle(vol * 83 / 0x7f + 1, 5 * 8, 83, 7 * 8 - 3);
}

void display_state(char *state, uint8_t *remote_name, uint8_t offset) {
    if (freezed) return;
    snprintf(lcd_string_buffer, sizeof(lcd_string_buffer), state);
    fb_clear_line(2);
    fb_clear_line(3);
    fb_draw_string_big (offset, 2, lcd_string_buffer);

    fb_clear_line(4);
    if (remote_name) {
        snprintf(lcd_string_buffer, sizeof(lcd_string_buffer), "%s", remote_name);
        fb_draw_string(0, 4, lcd_string_buffer);
    }
}

void display_sample_rate(int sample_rate) {
    if (freezed) return;
    snprintf(lcd_string_buffer, sizeof(lcd_string_buffer), " sr: %d", default_sample_rate);
    fb_clear_line_part(7, 0, 44);
    fb_draw_string(0, 7, lcd_string_buffer);
}

void display_packets(uint32_t packets) {
    if (freezed) return;
    snprintf(lcd_string_buffer, sizeof(lcd_string_buffer), "%*u", 8, packets);
    fb_clear_line_part(7, 45, 87);
    fb_draw_string(45, 7, lcd_string_buffer);
}


void display_reboot() {
    freezed = true;
    vTaskDelay(20 / portTICK_RATE_MS);
    fb_clear();
    snprintf(lcd_string_buffer, sizeof(lcd_string_buffer), "     %s", dev_name);
    fb_draw_string_big (0, 0, lcd_string_buffer);

    snprintf(lcd_string_buffer, sizeof(lcd_string_buffer), "release");
    fb_draw_string_big (0, 2, lcd_string_buffer);
    snprintf(lcd_string_buffer, sizeof(lcd_string_buffer), "buttons");
    fb_draw_string_big (0, 4, lcd_string_buffer);
    snprintf(lcd_string_buffer, sizeof(lcd_string_buffer), "to reboot...");
    fb_draw_string_big (0, 6, lcd_string_buffer);
    fb_show();
}
