#pragma once
#include <stdint.h>
#include <string.h>


enum boot_checkpoint_t : uint8_t 
{
    CP_BOOTED, // successfully booted
    CP_BOOT, // at boot
    CP_MATRIX_CHECK, // before matrix check
    CP_SPIFFS_OPEN, // before main SPIFFS open
    CP_SETTINGS_OPEN, // before settings store open
    CP_WIFI_START, // before WiFi start   
};


enum panic_reason_t : uint8_t
{
    PANIC_NONE,
    PANIC_TOO_WEAK_POWER_SUPPLY, // power supply might be too weak
    PANIC_MAIN_SPIFFS_OPEN_FAILED, // main spiffs cannot be mounted
    PANIC_SETTINGS_STORE_SPIFFS_FAILED, // settings store spiffs cannot be mounted
    PANIC_UNKOWN_BOOT_REASON, // unknown boot related reason
};

 __attribute__((noreturn)) void do_panic(panic_reason_t reason);
void panic_init();
uint32_t panic_increment_boot_count();
void panic_reset_boot_count();
void panic_record_checkpoint(boot_checkpoint_t cp);
boot_checkpoint_t panic_get_last_checkpoint();
void panic_check_repeated_boot();
void panic_notify_loop_is_running();
void panic_show_boot_count();

