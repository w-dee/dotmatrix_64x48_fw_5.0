#pragma once


extern uint8_t button_scan_bits; //!< holds currently pushed button bit-map ('1':pushed)
void matrix_drive_setup();
void matrix_drive_loop();
