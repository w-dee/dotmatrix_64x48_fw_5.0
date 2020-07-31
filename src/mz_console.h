#pragma once

void console_probe();
void init_console(); // this also initialize stdout so printf over the serial line can work
void begin_console();
void load_console_history();