#pragma once

#include <stdint.h>


#define GPIO_OUTPUT_IO_RED      13
#define GPIO_OUTPUT_IO_GREEN    12


#define RED                     1
#define GREEN                   2
#define ORANGE                  3
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_RED) | (1ULL<<GPIO_OUTPUT_IO_GREEN))


void led_init();
void led_off();
void led_on(uint8_t color);
