#pragma once



extern uint8_t matrix_button_scan_bits; //!< holds currently pushed button bit-map ('1':pushed)
void matrix_drive_early_setup(); // first initialization to blank all leds
void matrix_drive_setup();
void matrix_drive_loop();
#define LED_CURRENT_GAIN_MAX (103+64) // current gain value maximum
void matrix_drive_set_current_gain(int gain);
int matrix_drive_get_current_gain();
