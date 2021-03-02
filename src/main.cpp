#include <Arduino.h>
#include "matrix_drive.h"
#include "spiffs_fs.h"
#include "settings.h"
#include "mz_update.h"
#include "mz_wifi.h"
#include "buttons.h"
#include "mz_console.h"
#include "threadsync.h"
#include "calendar.h"
#include "status_led.h"
#include "ambient.h"
#include "mz_i2c.h"
#include "mz_bme.h"
#include "web_server.h"
#include "ui.h"

void setup() {
  // put your setup code here, to run once:
  status_led_early_setup();
  matrix_drive_early_setup(); // blank all leds

  delay(1000);

  init_console(); // this also initializes the serial output and stdio
  show_ota_status();
  status_led_setup();
  matrix_drive_setup();
  init_fs();

  // before init_settings, check cancel buttion be pressed over 1sec
  delay(100); // wait for matrix row drive cycles several times
  printf("--buttons: %x\n", button_scan_bits);
  if(button_scan_bits & BUTTON_CANCEL)
  {
    printf("Cancel button pressed. Seeing if this button is pressed over 1 sec .");
    bool clear_it = true;
    for(int i = 0; i < 10; ++i)
    {
      if(!(button_scan_bits & BUTTON_CANCEL)) { clear_it = false; break; }
      delay(100);
      printf(".");
      fflush(stdout);
    }
    if(clear_it)
    {
        printf(" OK, now clearing settings.");
        fflush(stdout);
        clear_settings();
    }
    puts("");
  }

  // or, if /spiffs/.clear exist, clear all settings
  if(FILE *f = fopen(CLEAR_SETTINGS_INDICATOR_FILE, "r"))
  {
    fclose(f);
    unlink(CLEAR_SETTINGS_INDICATOR_FILE);
    printf("Clearing settings requested.\n");
    fflush(stdout);
    clear_settings();
  }


  init_settings();
  wifi_setup();
  init_calendar(); // sntp initialization needs to be located after network stack initialization
  init_i2c();
  init_bme280();
  init_ambient();

  // find font partition and mmap into the address space
	puts("Compressed BFF font initializing ...");
  const esp_partition_t * part =
    esp_partition_find_first((esp_partition_type_t)0x40,
    (esp_partition_subtype_t)get_current_active_partition_number(), NULL); 
  if(part == nullptr)
  {
    // TODO: panic
    printf("No font partitions found!\n");
  }
  printf("Font partition start: 0x%08x, mapped to: ", part->address);

  const void *map_ptr;
  spi_flash_mmap_handle_t map_handle;
  if(ESP_OK != esp_partition_mmap(part, 0, part->size, SPI_FLASH_MMAP_DATA, &map_ptr, &map_handle))
  {
    // TODO: panic
  }

  const uint8_t * ptr = static_cast<const uint8_t *>(map_ptr);
  printf("%p\r\n", ptr);
  printf("Font data magic: %02x %02x %02x %02x\r\n", ptr[0], ptr[1], ptr[2], ptr[3]);

  web_server_setup();

  begin_console();

  ui_setup();
}

void loop() {
  // put your main code here, to run repeatedly:
  matrix_drive_loop();
  button_update();
  poll_main_thread_queue();
  status_led_loop();
  poll_ambient();
  poll_bme280();
  web_server_handle_client();
  ui_process();
}