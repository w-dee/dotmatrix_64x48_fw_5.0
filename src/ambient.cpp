#include <Arduino.h>
#include "ambient.h"
#include "interval.h"

// Ambient sensor handling

#define ADC_NUM 39 // ambient sensor number

static uint16_t read_ambient()
{
    return analogRead(ADC_NUM);
}


void poll_ambient()
{
    EVERY_MS(1000)
    {
        printf("ambient: %d\n", read_ambient());
    }
    END_EVERY_MS
}