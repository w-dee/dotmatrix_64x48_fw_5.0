// Panic handling and boot sanity checks
#include <Arduino.h>
#include "panic.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mz_wifi.h"
#include "status_led.h"

static nvs_handle nvs_hnd;
static const char *boot_count_name = "boot_count";
static const char *boot_cp_name = "boot_cp";
static const char *boot_clear_settings_name = "boot_clear";


// rough delay for panic loop
static void panic_delay(int ms)
{
    while(ms --) { status_led_loop(); delay(1); }
}

// do panic
 __attribute__((noreturn)) void do_panic(panic_reason_t reason)
{
    printf("---------PANIC--------\nReason code: %d\n", (int)reason);
    panic_reset_boot_count(); // reset boot count to prevent infinite boot loop

    // brink led
    status_led_set_global_brightness(256);
    for(;;)
    {
        for(int i = 0; i < (int)reason; ++i)
        {
            status_led_array[0].b = 0;
            status_led_array[0].g = 0;
            status_led_array[0].r = 255;
            panic_delay(100);
            status_led_array[0].b = 0;
            status_led_array[0].g = 0;
            status_led_array[0].r = 0;
            panic_delay(300);
        }
        panic_delay(2000);
    }
}


// initialize panic module
void panic_init()
{
    // initialize nvs partition
    nvs_flash_init(); // TODO: panic if failed

    nvs_open("mz5_doc",  NVS_READWRITE, &nvs_hnd); // TODO: panic if failed
}

// increment boot counter in nvs
uint32_t panic_increment_boot_count()
{
    uint32_t count = 0;

    if(ESP_OK != nvs_get_u32(nvs_hnd, boot_count_name, &count))
    {
        count = 0;
    }

    ++ count;

    nvs_set_u32(nvs_hnd, boot_count_name, count);
    nvs_commit(nvs_hnd);

    return count;
}

// get boot counter
static uint32_t panic_get_boot_count()
{
    uint32_t count = 0;

    if(ESP_OK != nvs_get_u32(nvs_hnd, boot_count_name, &count))
    {
        count = 0;
    }
    return count;
}


// reset boot counter in nvs
void panic_reset_boot_count()
{
    nvs_set_u32(nvs_hnd, boot_count_name, 0);
    nvs_set_u8(nvs_hnd, boot_cp_name, (uint8_t) 0);
    nvs_set_u8(nvs_hnd, boot_clear_settings_name, (uint8_t) 0);
    nvs_commit(nvs_hnd);
}

// record current boot checkpoint progress
void panic_record_checkpoint(boot_checkpoint_t cp)
{
    printf("Boot checkpoint: %d\r\n", (int)cp);
    fflush(stderr);
    delay(100);
    nvs_set_u8(nvs_hnd, boot_cp_name, (uint8_t) cp);
    nvs_commit(nvs_hnd);
}

// get last recorded checkpoint
boot_checkpoint_t panic_get_last_checkpoint()
{
    uint8_t value;
    if(ESP_OK != nvs_get_u8(nvs_hnd, boot_cp_name, &value))
    {
        return CP_BOOT;
    }
    return (boot_checkpoint_t)value;
}

// set NVS "clear settings on next boot" flag and immediately reboot
void set_nvs_clear_settings_on_next_boot_and_reboot()
{
    printf("Set 'settings clear flag' on NVS. Rebooting now.\n");
    panic_reset_boot_count();
    nvs_set_u8(nvs_hnd, boot_clear_settings_name, (uint8_t) 1);
    nvs_commit(nvs_hnd);
    delay(100);
    immediate_reset();
}

// Get NVS "clear settings on next boot" flag. The flag will be cleared.
bool get_nvs_clear_settings()
{
    uint8_t value;
    bool ret = false;
    if(ESP_OK != nvs_get_u8(nvs_hnd, boot_clear_settings_name, &value))
        ret = false;
    else
        ret = value == 1;

    nvs_set_u8(nvs_hnd, boot_clear_settings_name, (uint8_t) 0);
    nvs_commit(nvs_hnd);

    return ret;
}


#define BOOT_REPEAT_THRESH 5
// check repeating boot
void panic_check_repeated_boot()
{
    boot_checkpoint_t last_cp = panic_get_last_checkpoint();
    if(last_cp == CP_SETTINGS_CLEAR)
    {
        // This will be an immediate failure.
        // Settings store clearing has repeatedly failed; // ummm.......... what can we do ?
        do_panic(PANIC_SETTINGS_FS_CLEAR_FAILED);
    }


    uint32_t boot_count = panic_increment_boot_count();
    if(boot_count > BOOT_REPEAT_THRESH)
    {
        // counter overflow 
        // ... WTF?
        panic_reset_boot_count(); // reset boot count
    }
    else if(boot_count >= BOOT_REPEAT_THRESH)
    {
        // boot repeat threshold reached
        // what's happened?
        switch(last_cp)
        {
        case CP_BOOTED:
            break; // none

        case CP_BOOT:
            // WTF?
            do_panic(PANIC_UNKOWN_BOOT_REASON);
            break;

        case CP_MATRIX_CHECK:
            // matrix check has repeatedly failed
            // might be too weak power supply
            do_panic(PANIC_TOO_WEAK_POWER_SUPPLY);
            break;

        case CP_FS_OPEN:
            // LittleFS open has repeatedly failed; // ummm.......... what can we do ?
            do_panic(PANIC_MAIN_FS_OPEN_FAILED);
            break;
        
        case CP_SETTINGS_CLEAR:
            // no way
            do_panic(PANIC_SETTINGS_FS_CLEAR_FAILED);
            break;

        case CP_SETTINGS_OPEN:
            // settings store open has repeatedly failed after clearing the store.
            // try clearing the settings store.
            set_nvs_clear_settings_on_next_boot_and_reboot();
            break;

        case CP_BUTTON_CHECK:
            do_panic(PANIC_BUTTON_CHECK);
            break;

        case CP_MISC_SETUP:
            do_panic(PANIC_MISC_SETUP);
            break;

        case CP_WIFI_START:
            // wifi config has rotten
            // it's very likely to happen if the user sets the invalid IP address;
            // tell wifi subsystem to clear wifi setting.
            printf("Seems wifi settings is rotten.\n");
            printf("Clearing the wifi setting.\n");
            wifi_set_clear_setting_flag();
            break;

        case CP_CONSOLE_START:
            do_panic(PANIC_CONSOLE_START); // wtf...
            break;
        }
    }
}


// tell the panic subsytem that the main loop is running
void panic_notify_loop_is_running()
{
    static uint32_t start_tick = (uint32_t) 0;
    // wait at least one second from the first loop iteration
    if(start_tick == 0) start_tick = millis();

    if(start_tick != (uint32_t) -1 && millis() - start_tick > 1000)
    {
        // one second elapsed
        start_tick = (uint32_t) -1;
        printf("Successfully booted\n");
        panic_record_checkpoint(CP_BOOTED);
        panic_reset_boot_count(); // set boot count; assumes the boot is complete.
    }
}


void panic_show_boot_count()
{
    printf("Errornous repeated boot count = %d, last checkpoint = %d\n",
        (int)panic_get_boot_count(), (int)panic_get_last_checkpoint());
}

// Reset immediately. This will be different action from 
// reboot() in mz_update.cpp which tries safe reboot.
void immediate_reset()
{
    ESP.restart();
    for(;;) ;
}
