#pragma once

#include "freertos/FreeRTOS.h"

#define NOP() asm volatile ("nop")

unsigned long IRAM_ATTR us();
void IRAM_ATTR delay_us(uint32_t us);
