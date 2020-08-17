#pragma once

#include <Arduino.h>
#include "settings.h"

void init_calendar();

void set_tz(const string_vector & time_server, const String & time_zone);
void get_tz(string_vector & time_server, String & time_zone);
