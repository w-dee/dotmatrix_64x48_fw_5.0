#pragma once


/**
 * Check whether the connected terminal is capable of line editing.
 * This needs the terminal is actually connected and can bi-directionally
 * communicatable with the ESP32 at the time that this function is called.
 * */
void console_probe();

/**
 * Initialize console and command line interpreter facility.
 * This also initialize stdout/stdin so printf over the serial line can work.
 * */
void init_console();

/**
 * This must be called after settings filesystem becoming ready.
 * */
void begin_console();

