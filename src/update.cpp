#include "update.h"




bool updater_t::begin(update_type_t type, uint32_t size)
{
    _type = type;
    _size = _size;
    if(_size & (SPI_FLASH_SEC_SIZE - 1)) return false; // the size is not a multiple of SPI_FLASH_SEC_SIZE
    _partition = partition_from_type(_type);
    if(!_partition) return false; // partition not found
    _md5.begin();
    return true;
}

bool updater_t::write_sector(const uint8_t *buf)
{
    if(_type == utUnknown) return false; // not begun
    if(_progress >= _partition->size) return false; // already done
    uint8_t *bbuf = (uint8_t*)malloc(SPI_FLASH_SEC_SIZE);
    if(!bbuf) return false; // memory exhausted

    memcpy(bbuf, buf, SPI_FLASH_SEC_SIZE);

    if(_progress == 0)
    {
        // the first sector
        // save first byte (this byte will be overritten at the completion) 
        _first_byte = bbuf[0];
        bbuf[0] = 0xff; // we use 0xff here because flash device can clear bit easily
        // but set bit with difficulty (only erase operation can set the bits)
    }

    if(!ESP.flashEraseSector((_partition->address + _progress)/SPI_FLASH_SEC_SIZE)){
        goto fail;
    }
    if (!ESP.flashWrite(_partition->address + _progress, (uint32_t*)bbuf, SPI_FLASH_SEC_SIZE)) {
        goto fail;
    }

    if(_progress == 0)
    {
        // the first sector
        // write back the first byte to calculate md5
        bbuf[0] = _first_byte;
    }

    _md5.add(bbuf, (uint16_t)SPI_FLASH_SEC_SIZE); //  !?!?!? why the first argument is not const 

    _progress += SPI_FLASH_SEC_SIZE;
    if(_progress >= _partition->size)
    {
        // finished

        _md5.calculate();
        // write back the first sector's first byte to the correct value
        uint8_t buf[4];

        if(!ESP.flashRead(_partition->address, (uint32_t*)buf, 4)) {
            goto fail;
        }
        buf[0] = _first_byte;

        if(!ESP.flashWrite(_partition->address, (uint32_t*)buf, 4)) {
            goto fail;
        }
    }

    free(bbuf);
    return true;

fail:
    free(bbuf);
    _type = utUnknown;
    return false;
}

bool updater_t::match_md5(const uint8_t *md5)
{
    if(_type == utUnknown) return false; // not begun
    if(_progress != _partition->size) return false; // incomplete

    // _md5.calculate() was done in write_sector()
    uint8_t buf[16];
    _md5.getBytes(buf);
    return !memcmp(md5, buf, sizeof(buf));
}

bool updater_t::activate_new_code()
{
    if(_type != utCode) return false;
     if(_progress != _partition->size) return false; // incomplete
    if(esp_ota_set_boot_partition(_partition) != ESP_OK) return false;
    return true;
}


const esp_partition_t* updater_t::partition_from_type(update_type_t _type)
{
    switch(_type)
    {
    case utCode:
        return esp_ota_get_next_update_partition(NULL);
    case utSPIFFS:
        return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
    case utFont:
        return esp_partition_find_first((esp_partition_type_t)0x40, (esp_partition_subtype_t)0, NULL); // see custom.csv for partition table
    case utUnknown:
        return nullptr;
    }
    return nullptr;
}



