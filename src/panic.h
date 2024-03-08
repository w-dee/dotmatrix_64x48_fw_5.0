#pragma once
#include <stdint.h>
#include <string.h>


enum boot_checkpoint_t : uint8_t 
{
    CP_BOOTED = 0U, // successfully booted
    CP_BOOT = 1U, // at boot
    CP_MATRIX_CHECK = 2U, // before matrix check
    CP_FS_OPEN = 3U, // before main LittleFS open
    CP_SETTINGS_CLEAR = 4U, // before settings clear check
    CP_SETTINGS_OPEN = 5U, // before settings store open
    CP_BUTTON_CHECK = 6U, // before checking button state
    CP_MISC_SETUP = 7U, // before miscellaneous setup
    CP_WIFI_START = 8U, // before WiFi start
    CP_CONSOLE_START = 9U,
};


enum panic_reason_t : uint8_t
{
    PANIC_NONE = 0U,
    PANIC_TOO_WEAK_POWER_SUPPLY = 1U, // power supply might be too weak
    PANIC_MAIN_FS_OPEN_FAILED = 2U, // main LittleFS cannot be mounted
    PANIC_SETTINGS_FS_CLEAR_FAILED = 3U, // settings LittleFS cannot be cleared
    PANIC_BUTTON_CHECK = 4U, // button check has failed
    PANIC_MISC_SETUP = 5U, // miscellaneous setup failed
    PANIC_CONSOLE_START = 6U, // console startup failed
    PANIC_UNKOWN_BOOT_REASON = 7U, // unknown boot related reason
};

__attribute__((noreturn)) void do_panic(panic_reason_t reason);
void panic_init();
uint32_t panic_increment_boot_count();
void panic_reset_boot_count();
void panic_record_checkpoint(boot_checkpoint_t cp);
boot_checkpoint_t panic_get_last_checkpoint();
__attribute__((noreturn)) void set_nvs_clear_settings_on_next_boot_and_reboot();
bool get_nvs_clear_settings();
void panic_check_repeated_boot();
void panic_notify_loop_is_running();
void panic_show_boot_count();
__attribute__((noreturn)) void immediate_reset();
