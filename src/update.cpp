#include "update.h"




bool partition_updater_t::begin(update_type_t type, uint32_t size)
{
    _type = type;
    _size = _size;
    if(_size & (SPI_FLASH_SEC_SIZE - 1)) return false; // the size is not a multiple of SPI_FLASH_SEC_SIZE
    _partition = next_partition_from_type(_type);
    if(!_partition) return false; // partition not found
    _md5.begin();
    return true;
}

bool partition_updater_t::write_sector(const uint8_t *buf)
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

bool partition_updater_t::match_md5(const uint8_t *md5)
{
    if(_type == utUnknown) return false; // not begun
    if(_progress != _partition->size) return false; // incomplete

    // _md5.calculate() was done in write_sector()
    uint8_t buf[16];
    _md5.getBytes(buf);
    return !memcmp(md5, buf, sizeof(buf));
}

bool partition_updater_t::activate_new_code()
{
    if(_type != utCode) return false;
     if(_progress != _partition->size) return false; // incomplete
    if(esp_ota_set_boot_partition(_partition) != ESP_OK) return false;
    return true;
}


const esp_partition_t* partition_updater_t::next_partition_from_type(update_type_t _type)
{
    int active = get_current_active_partition_number();
    switch(_type)
    {
    case utCode:
        return esp_partition_find_first(ESP_PARTITION_TYPE_APP, 
            (active == 1) ? ESP_PARTITION_SUBTYPE_APP_OTA_0 : ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
    case utSPIFFS:
        return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
            (active == 1) ? "spiffs" : "spiffs1"); // see custom.csv
    case utFont:
        return esp_partition_find_first((esp_partition_type_t)0x40,
            (esp_partition_subtype_t)((active == 1) ? 0 : 1), nullptr); // see custom.csv for partition table
    case utUnknown:
        return nullptr;
    }
    return nullptr;
}


/**
 * returns current active partition number 0 or 1.
 * */
int get_current_active_partition_number()
{
    if(!strcmp(esp_ota_get_running_partition()->label, "app0"))
        return 0;
    if(!strcmp(esp_ota_get_running_partition()->label, "app1"))
        return 1;
    return -1; // TODO: PANIC
}


void updater_t::begin()
{
    if(!buffer) buffer = (uint8_t*)malloc(SPI_FLASH_SEC_SIZE);
    if(!buffer) { /* TODO: PANIC */ }

    remaining_count = 0;
    buffer_pos = 0;
    phase = phBegin;
    status = stNoError;
}

void updater_t::end()
{
    if(buffer) free(buffer), buffer = nullptr;
}


void updater_t::process_block()
{
    // process the block according to the buffer content and the phase
    if(phase == phBegin)
    {
        // received block must be a header
        if(memcmp("MZ5 firmware archive 1.0\r\n\n\x1a    ", buffer, 32))
        {
            // invalid header
            status = stCorrupted;
            return; 
        }
        phase = phHeader;
    }
    else if(phase == phHeader)
    {
        // received block must be a partition header
        if(memcmp(buffer, "-file boundary--", 16))
        {
            // partition header mismatch
            status = stCorrupted;
            return;
        }
        memcpy(&header, buffer + 16, sizeof(header)); // take a copy of it
        header.label[sizeof(header.label)-1 ] = 0; // force terminate the label string

        // some sanity checks
        if(header.orig_len > header.arc_len ||
            header.arc_len % SPI_FLASH_SEC_SIZE != 0)
        {
            status = stCorrupted;
            return;
        }

        // determin partition type and prepare to flash
        partition_updater_t::update_type_t type = partition_updater_t::utUnknown;
        if(!strcmp(header.label, "font"))
            type = partition_updater_t::utFont;
        else if(!strcmp(header.label, "spiffs"))
            type = partition_updater_t::utSPIFFS;
        else if(!strcmp(header.label, "app"))
            type = partition_updater_t::utCode;
        else
        {
            // unknown label
            status = stCorrupted;
            return;
        }
        partition_updater.begin(type, header.arc_len);

        remaining_count = header.arc_len / SPI_FLASH_SEC_SIZE;
    }
    else if(phase == phContent)
    {
        partition_updater.write_sector(buffer);
        -- remaining_count;
        if(remaining_count == 0)
        {
            // all sector in the partition has been written
            if(!partition_updater.match_md5(header.md5))
            {
                // md5 mismatch
                status = stCorrupted;
                return;
            }
            // activate new code
            partition_updater.activate_new_code();

            // prepare to receive next header (if exists)
            phase = phHeader;
        }
    }
}

void updater_t::write_data(const uint8_t * buf, size_t size)
{
    if(status != stNoError) return;
    while(size > 0)
    {
        // fill buffer
        size_t buffer_remain = SPI_FLASH_SEC_SIZE - buffer_pos;
        size_t one_size = std::min(buffer_remain, size);
        memcpy(buffer + buffer_pos, buf, one_size);
        size -= one_size;
        buf += one_size;
        buffer_pos += one_size;
        if(buffer_pos == SPI_FLASH_SEC_SIZE)
        {
            // one block has been filled
            buffer_pos = 0;
            process_block();
            if(status != stNoError) return;
        }
    }
}