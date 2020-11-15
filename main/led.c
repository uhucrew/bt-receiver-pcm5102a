#include "driver/gpio.h"

#include "led.h"


void led_init() {
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);
}


void led_off() {
    gpio_set_level(GPIO_OUTPUT_IO_RED, 0);
    gpio_set_level(GPIO_OUTPUT_IO_GREEN, 0);
}


void led_on(uint8_t color) {
    switch (color) {
    case RED:
        gpio_set_level(GPIO_OUTPUT_IO_RED, 1);
        gpio_set_level(GPIO_OUTPUT_IO_GREEN, 0);
        break;
    case GREEN:
        gpio_set_level(GPIO_OUTPUT_IO_RED, 0);
        gpio_set_level(GPIO_OUTPUT_IO_GREEN, 1);
        break;
    case ORANGE:
        gpio_set_level(GPIO_OUTPUT_IO_RED, 1);
        gpio_set_level(GPIO_OUTPUT_IO_GREEN, 1);
        break;
    default:
        gpio_set_level(GPIO_OUTPUT_IO_RED, 0);
        gpio_set_level(GPIO_OUTPUT_IO_GREEN, 0);
    }
}
