#pragma once

#include <Arduino.h>
#include "esp_spi_flash.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include <MD5Builder.h>

class updater_t
{
public:
    enum update_type_t 
    {
        utUnknown,
        utCode,
        utSPIFFS,
        utFont,
    };
    updater_t() : _type(utUnknown), _size(0), _first_byte(0xff), _progress(0), _partition(nullptr) {}
    ~updater_t() {}

    bool begin(update_type_t type, uint32_t size);
    bool write_sector(const uint8_t *buf); //!< write a sector to current position
    bool match_md5(const uint8_t *md5);
    bool activate_new_code(); //!< activate newly written code (only for type == utCode)

    static const esp_partition_t* partition_from_type(update_type_t _type);

private:
    update_type_t _type;
    uint32_t _size;
    uint8_t _first_byte; //!< first byte of the partition (usually a magic number)
    uint32_t _progress;
    const esp_partition_t* _partition;
    MD5Builder _md5;
};
