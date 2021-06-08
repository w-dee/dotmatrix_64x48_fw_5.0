// Panic handling and boot sanity checks
#include <Arduino.h>
#include "panic.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mz_wifi.h"

static nvs_handle nvs_hnd;
static const char *boot_count_name = "boot_count";
static const char *boot_cp_name = "boot_cp";

// do panic
 __attribute__((noreturn)) void do_panic(panic_reason_t reason)
{
    printf("---------PANIC--------\nReason code: %d\n", (int)reason);
    panic_reset_boot_count(); // reset boot count to prevent infinite boot loop
    for(;;) ; // TODO: real panic indication
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
    nvs_commit(nvs_hnd);
}

// record current boot checkpoint progress
void panic_record_checkpoint(boot_checkpoint_t cp)
{
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

#define BOOT_REPEAT_THRESH 5
// check repeating boot
void panic_check_repeated_boot()
{
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
        boot_checkpoint_t last_cp = panic_get_last_checkpoint();
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

        case CP_SPIFFS_OPEN:
            // SPIFFS open has repeatedly failed; // ummm.......... what can we do ?
            do_panic(PANIC_MAIN_SPIFFS_OPEN_FAILED);
            break;
        
        case CP_SETTINGS_OPEN:
            // settings store open has repeatedly failed.
            // user can re-format the settings store by physical button
            do_panic(PANIC_SETTINGS_STORE_SPIFFS_FAILED);
            break;

        case CP_WIFI_START:
            // wifi config has rotten
            // it's very likely to happen if the user sets the invalid IP address;
            // tell wifi subsystem to clear wifi setting.
            printf("Seems wifi settings is rotten.\n");
            printf("Clearing the wifi setting.\n");
            wifi_set_clear_setting_flag();
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
    printf("Errornous repeated boot count : %d\n", (int)panic_get_boot_count());
}