#include "freertos/FreeRTOS.h"
#include "timer_delay.h"

unsigned long IRAM_ATTR micros()
{
    return (unsigned long) (esp_timer_get_time());
}


void IRAM_ATTR delay_us(uint32_t us)
{
    uint32_t m = micros();
    if (us) {
        uint32_t e = (m + us);
        if(m > e) {
            while(micros() > e) {
                NOP();
            }
        }
        while(micros() < e) {
            NOP();
        }
    }
}
