#pragma once

void init_ambient();
void poll_ambient();
int16_t get_ambient();

void sensors_set_contrast_always_max(bool b);
void sensors_change_current_contrast(int amount);
