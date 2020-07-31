#include <Arduino.h>
#include "matrix_drive.h"
#include "spiffs_fs.h"
#include "settings.h"
#include "update.h"
#include "wifi.h"
#include "buttons.h"
#include "mz_console.h"
#include "threadsync.h"

void setup() {
  // put your setup code here, to run once:
  delay(3000);

  matrix_drive_setup();

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


  init_settings();
  init_fs();
  init_console(); load_console_history();
  wifi_setup();

  // find font partition and mmap into the address space
	puts("Compressed BFF font initializing ...");
  const esp_partition_t * part = updater_t::partition_from_type(updater_t::utFont);
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



  begin_console();
}

void loop() {
  // put your main code here, to run repeatedly:
  matrix_drive_loop();
  button_update();
  poll_main_thread_queue();
}