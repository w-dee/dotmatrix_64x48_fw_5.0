#pragma once


#define MAX_STATUS_LED 52 // must be multiple of 4 (even if there are less LEDs in series)
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
static_assert(MAX_STATUS_LED * 3 % 4 == 0, "MAX_STATUS_LED size error");

extern uint8_t button_scan_bits; //!< holds currently pushed button bit-map ('1':pushed)
void matrix_drive_early_setup(); // first initialization to blank all leds
void matrix_drive_setup();
void matrix_drive_loop();
