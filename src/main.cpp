#include <Arduino.h>
#include "matrix_drive.h"
#include "spiffs_fs.h"
#include "settings.h"
#include "update.h"

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(1000);

  init_settings();
  init_fs();
  matrix_drive_setup();

  // find font partition and mmap into the address space
  const esp_partition_t * part = updater_t::partition_from_type(updater_t::utFont);
  Serial.printf("Font partition start: 0x%08x, mapped to: ", part->address);

  const void *map_ptr;
  spi_flash_mmap_handle_t map_handle;
  if(ESP_OK != esp_partition_mmap(part, 0, part->size, SPI_FLASH_MMAP_DATA, &map_ptr, &map_handle))
  {
    // TODO: panic
  }

  const uint8_t * ptr = static_cast<const uint8_t *>(map_ptr);
  Serial.printf("%p\n", ptr);
  Serial.printf("Font data magic: %02x %02x %02x %02x\n", ptr[0], ptr[1], ptr[2], ptr[3]);
}

void loop() {
  // put your main code here, to run repeatedly:
  matrix_drive_loop();
}