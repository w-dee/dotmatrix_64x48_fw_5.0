#pragma once


#define MAX_STATUS_LED 1//49
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

extern uint8_t button_scan_bits; //!< holds currently pushed button bit-map ('1':pushed)
void matrix_drive_setup();
void matrix_drive_loop();
