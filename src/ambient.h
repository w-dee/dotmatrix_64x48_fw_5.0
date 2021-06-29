#pragma once

void init_ambient();
void poll_ambient();
int16_t get_ambient();

void sensors_set_brightness_always_max(bool b);
void sensors_set_brightness_fix(int brightness);
int sensors_get_brightness_by_current_ambient();
void sensors_change_current_brightness(int amount);
