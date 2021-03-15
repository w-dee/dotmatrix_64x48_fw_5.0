#pragma once

#include <Arduino.h>

#define MAX_STATUS_LED 4 // must be multiple of 4 (even if there are less LEDs in series)
struct s_rgb_t
{
    union
    {
        struct
        {
            unsigned int b : 8;
            unsigned int r : 8;
            unsigned int g : 8;
            unsigned int dummy : 8;
        };
        uint32_t value;
    };
};
extern s_rgb_t status_led_array[MAX_STATUS_LED]; //!< RGB color value of status LEDs

void status_led_early_setup(); // first initialization to blank all leds
void status_led_commit(); // transmit data to WS2812
void status_led_setup();
void status_led_loop();
void status_led_set_global_brightness(int v);
