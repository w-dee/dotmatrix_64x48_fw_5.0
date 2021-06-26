#include <cstddef>
#include "mz_update.h"
#include "settings.h"
#include <rom/miniz.h>
#include <functional>

// streamed ZLIB decompressor
class mz_inflator_t
{
public:
    typedef std::function<int(const uint8_t *buf, size_t size)> write_fn_t;

private:
    size_t infile_remaining;
    const void *next_in;
    size_t avail_in = 0;
    void *next_out = s_outbuf;
    size_t avail_out = sizeof(s_outbuf);
    size_t total_in = 0, total_out = 0;
    write_fn_t write_fn;
    tinfl_decompressor inflator;
    uint8_t s_outbuf[TINFL_LZ_DICT_SIZE];

public:
    mz_inflator_t(write_fn_t fn, size_t input_size)
    {
        write_fn = fn;
        tinfl_init(&inflator);
        infile_remaining = input_size;
    }

    // feed compressed data. write_fn is called when uncompressed data is ready.
    int eat(const uint8_t *buf, size_t size)
    {
        next_in = buf;
        for (;;)
        {
            size_t in_bytes, out_bytes;
            tinfl_status status;
            if (!avail_in)
            {
                // Input buffer is empty, so read more bytes from input file.
                if(size == 0)
                {
                    return TINFL_STATUS_FAILED; // no more input; must be a logic error
                }
                size_t n = size < infile_remaining ? size : infile_remaining;

                next_in = buf;
                avail_in = n;
                buf += n;
                size -= n;

                infile_remaining -= n;
            }

            in_bytes = avail_in;
            out_bytes = avail_out;
            status = tinfl_decompress(&inflator,
                                      (const mz_uint8 *)next_in, &in_bytes,
                                      s_outbuf, (mz_uint8 *)next_out, &out_bytes,
                                      (infile_remaining ? TINFL_FLAG_HAS_MORE_INPUT : 0) | TINFL_FLAG_PARSE_ZLIB_HEADER);

            avail_in -= in_bytes;
            next_in = (const mz_uint8 *)next_in + in_bytes;
            total_in += in_bytes;

            avail_out -= out_bytes;
            next_out = (mz_uint8 *)next_out + out_bytes;
            total_out += out_bytes;

            if ((status <= TINFL_STATUS_DONE) || (!avail_out))
            {
                // Output buffer is full, or decompression is done, so write buffer to output file.
                uint n = sizeof(s_outbuf) - (uint)avail_out;
                int status = write_fn(s_outbuf, n);
                if (status < 0)
                    return status;

                next_out = s_outbuf;
                avail_out = sizeof(s_outbuf);
            }

            // If status is <= TINFL_STATUS_DONE then either decompression is done or something went wrong.
            if (status <= TINFL_STATUS_DONE)
            {
                if (status == TINFL_STATUS_DONE)
                {
                    // Decompression completed successfully.
                    return TINFL_STATUS_DONE;
                }
                else
                {
                    // Decompression failed.
                    printf("tinfl_decompress() failed with status %i!\n", status);
                    return status;
                }
            }

            if (avail_in == 0)
                return TINFL_STATUS_NEEDS_MORE_INPUT; // return to get more data
        }
    }
};

bool partition_updater_t::begin(update_type_t type, uint32_t size)
{
    _type = type;
    _size = size;
    _progress = 0;
    if (_size & (SPI_FLASH_SEC_SIZE - 1))
        return false; // the size is not a multiple of SPI_FLASH_SEC_SIZE
    _partition = next_partition_from_type(_type);
    if (!_partition)
        return false; // partition not found
    if (_partition->size < _size)
        return false; // too large
    _md5.begin();
    return true;
}

bool partition_updater_t::write_sector(const uint8_t *buf)
{
    if (_type == utUnknown)
    {
        printf("not begin\n");
        return false;
    } // not begun
    if (_progress >= _size)
    {
        printf("already done %d %d\n", _progress, _size);
        return false;
    } // already done
    uint8_t *bbuf = (uint8_t *)malloc(SPI_FLASH_SEC_SIZE);
    uint8_t *rbuf = nullptr;
    if (!bbuf)
    {
        printf("OTA: Error: Memory exhausted.\n");
        return false; // memory exhausted
    }

    memcpy(bbuf, buf, SPI_FLASH_SEC_SIZE);

    bool skip = true;

    if (_progress == 0)
    {
        // the first sector
        // save first byte (this byte will be overritten at the completion)
        _first_byte = bbuf[0];
        bbuf[0] = 0xff; // we use 0xff here because flash device can clear bit easily
        // but set bit with difficulty (only erase operation can set the bits)
        skip = false;
    }

    if(skip)
    {
        // check the content is already the same as the data to be written, to save time
        rbuf = (uint8_t*)malloc(SPI_FLASH_SEC_SIZE);
        if(!rbuf)
        {
            printf("OTA: Error: Memory exhausted.\n");
            goto fail;
            return false; // memory exhausted
        }
        if(!ESP.flashRead(_partition->address + _progress, (uint32_t*)rbuf, SPI_FLASH_SEC_SIZE))
        {
            printf("OTA: Error: Failed to read a sector at %08lx.\n", (long)(_partition->address + _progress));
            goto fail;
        }
        skip = !memcmp(rbuf, bbuf, SPI_FLASH_SEC_SIZE); // skip erase and write if the content is the same
    }

    if(!skip)
    {
        if (!ESP.flashEraseSector((_partition->address + _progress) / SPI_FLASH_SEC_SIZE))
        {
            printf("OTA: Error: Failed to erase a sector at %08lx.\n", (long)(_partition->address + _progress));
            goto fail;
        }
        if (!ESP.flashWrite(_partition->address + _progress, (uint32_t *)bbuf, SPI_FLASH_SEC_SIZE))
        {
            printf("OTA: Error: Failed to write a sector at %08lx.\n", (long)(_partition->address + _progress));
            goto fail;
        }
        printf("!"); // wrote
    }
    else
    {
        printf("="); // skipped
    }

    if (_progress == 0)
    {
        // the first sector
        // write back the first byte to calculate md5
        bbuf[0] = _first_byte;
    }

    _md5.add(bbuf, (uint16_t)SPI_FLASH_SEC_SIZE); //  !?!?!? why the first argument is not const

    _progress += SPI_FLASH_SEC_SIZE;
    if (_progress >= _size)
    {
        // finished

        _md5.calculate();
        // write back the first sector's first byte to the correct value
        uint8_t buf[4];

        if (!ESP.flashRead(_partition->address, (uint32_t *)buf, 4))
        {
            printf("OTA: Error: Failed to read a sector from %08lx.\n", (long)(_partition->address));
            goto fail;
        }
        buf[0] = _first_byte;

        if (!ESP.flashWrite(_partition->address, (uint32_t *)buf, 4))
        {
            printf("OTA: Error: Failed to write a sector at %08lx.\n", (long)(_partition->address));
            goto fail;
        }
    }

    if(bbuf) free(bbuf);
    if(rbuf) free(rbuf);
    return true;

fail:
    if(bbuf) free(bbuf);
    if(rbuf) free(rbuf);
    _type = utUnknown;
    return false;
}

bool partition_updater_t::match_md5(const uint8_t *md5)
{
    if (_type == utUnknown)
        return false; // not begun
    if (_progress != _size)
        return false; // incomplete

    // _md5.calculate() was done in write_sector()
    uint8_t buf[16];
    _md5.getBytes(buf);
    printf("OTA: Received MD5: ");
    for (int i = 0; i < sizeof(buf); ++i)
        printf("%02x", buf[i]);
    printf("\n");
    return !memcmp(md5, buf, sizeof(buf));
}

bool partition_updater_t::activate_new_code()
{
    if (_type != utCode)
        return false;
    if (_progress != _size)
        return false; // incomplete
    if (esp_ota_set_boot_partition(_partition) != ESP_OK)
        return false;
    return true;
}

const esp_partition_t *partition_updater_t::next_partition_from_type(update_type_t _type)
{
    int active = get_current_active_partition_number();
    switch (_type)
    {
    case utCode:
        return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                        (active == 1) ? ESP_PARTITION_SUBTYPE_APP_OTA_0 : ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
    case utSPIFFS:
        return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                        (active == 1) ? "spiffs0" : "spiffs1");
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
    if (!strcmp(esp_ota_get_running_partition()->label, "app0"))
        return 0;
    if (!strcmp(esp_ota_get_running_partition()->label, "app1"))
        return 1;
    return -1; // TODO: PANIC
}

void updater_t::begin()
{
    if (!buffer)
        buffer = (uint8_t *)malloc(SPI_FLASH_SEC_SIZE);
    if (!buffer)
    { /* TODO: PANIC */
    }

    remaining_count = 0;
    buffer_pos = 0;
    phase = phBegin;
    status = stNoError;
}

void updater_t::end()
{
    if (buffer)
        free(buffer), buffer = nullptr;
}

void updater_t::process_block()
{
    // process the block according to the buffer content and the phase
    if (phase == phBegin)
    {
        // received block must be a header
        printf("OTA: Receiving archive header ...\n");
        if (memcmp("MZ5 firmware archive 1.0\r\n\n\x1a    ", buffer, 32))
        {
            // invalid header
            printf("OTA: Error: invalid archive header.\n");
            status = stCorrupted;
            return;
        }
        printf("OTA: Valid archive header.\n");
        phase = phHeader;
    }
    else if (phase == phHeader)
    {
        // received block must be a partition header
        printf("OTA: Receiving partition header ...\n");
        if (memcmp(buffer, "-file boundary--", 16))
        {
            // partition header mismatch
            printf("OTA: Error: invalid partition header.\n");
            status = stCorrupted;
            return;
        }
        memcpy(&header, buffer + 16, sizeof(header)); // take a copy of it
        header.label[sizeof(header.label) - 1] = 0;   // force terminate the label string

        // print information
        printf("OTA: Partition label: '%s', Original size: %d, Archived size: %d\n",
               header.label, (int)header.orig_len, (int)header.arc_len);
        printf("OTA:            MD5 sum: ");
        for (int i = 0; i < sizeof(header.md5); ++i)
            printf("%02x", header.md5[i]);
        printf("\n");

        // some sanity checks
        if (header.orig_len > header.arc_len ||
            header.arc_len % SPI_FLASH_SEC_SIZE != 0)
        {
            printf("OTA: Error: Invalid partition size.\n");
            status = stCorrupted;
            return;
        }

        // determin partition type and prepare to flash
        partition_updater_t::update_type_t type = partition_updater_t::utUnknown;
        if (!strcmp(header.label, "font"))
            type = partition_updater_t::utFont;
        else if (!strcmp(header.label, "spiffs"))
            type = partition_updater_t::utSPIFFS;
        else if (!strcmp(header.label, "app"))
            type = partition_updater_t::utCode;
        else
        {
            // unknown label
            printf("OTA: Error: Unknown label.\n");
            status = stCorrupted;
            return;
        }

        if (!partition_updater.begin(type, header.arc_len))
        {
            printf("OTA: Error: Possibly too large image to fit.\n");
            status = stCorrupted;
            return;
        }

        remaining_count = header.arc_len / SPI_FLASH_SEC_SIZE;
        printf("OTA: Sector count: %d\n", (int)remaining_count);
        phase = phContent;
    }
    else if (phase == phContent)
    {
        if (!partition_updater.write_sector(buffer))
        {
            printf("\nOTA: Error: Failed at partition_updater.write_sector().\n");
            status = stCorrupted;
            return;
        }
        --remaining_count;
        if (remaining_count == 0)
        {
            // all sector in the partition has been written
            printf("\nOTA: All sectors written.\n");
            if (!partition_updater.match_md5(header.md5))
            {
                // md5 mismatch
                printf("OTA: Error: MD5 mismatch.\n");
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

void updater_t::write_data(const uint8_t *buf, size_t size)
{
    if (status != stNoError)
        return;
    if (!buffer)
        return;
    while (size > 0)
    {
        // fill buffer
        size_t buffer_remain = SPI_FLASH_SEC_SIZE - buffer_pos;
        size_t one_size = std::min(buffer_remain, size);
        memcpy(buffer + buffer_pos, buf, one_size);
        size -= one_size;
        buf += one_size;
        buffer_pos += one_size;
        if (buffer_pos == SPI_FLASH_SEC_SIZE)
        {
            // one block has been filled
            buffer_pos = 0;
            process_block();
            if (status != stNoError)
                return;
        }
    }
}

bool updater_t::finish()
{
    bool success = false;
    if (status != stNoError)
        goto fin;
    if (phase != phHeader)
    {
        printf("OTA: Error: Premature end of input.\n");
        goto fin; // phase mismatch
    }

    printf("OTA: Success.\n");

    success = true; // no error found
fin:
    if (buffer)
        free(buffer), buffer = nullptr;
    return success;
}

void compressed_updater_t::begin()
{
    inherited::begin();
    total_received_bytes = 0;
    compressed_size = 0;
}

void compressed_updater_t::end()
{
    inherited::end();
    if (inflator)
        delete inflator, inflator = nullptr;
}

void compressed_updater_t::write_data(const uint8_t *buf, size_t size)
{
    static const char marks[] = "MZ5 compressed archive\r\n\n\x1a"; // magic header mark
    static constexpr size_t mark_size = sizeof(marks) - 1;

    // compressed image consists of magic header, compressed image size,
    // and the compressed data.
    if (status != stNoError)
        return; // error found

    // check the magic header
    while (total_received_bytes >= 0 && total_received_bytes < mark_size && size)
    {
        // check magic header
        if (*buf != marks[total_received_bytes])
        {
            // magic mismatch
            status = stCorrupted; // data is corrupted;
            return;
        }
        ++buf;
        ++total_received_bytes;
        --size;
    }

    // update sz
    while (total_received_bytes >= mark_size && total_received_bytes < mark_size + 4 && size)
    {
        // receive size
        sz_buf[total_received_bytes - mark_size] = *buf;
        ++buf;
        ++total_received_bytes;
        --size;
    }

    if (total_received_bytes == mark_size + 4)
    {
        // done receiving sz_buf
        // prepare for the receiving of the compressed data
        uint32_t compressed_size =
            ((uint32_t)sz_buf[0] << 0) +
            ((uint32_t)sz_buf[1] << 8) +
            ((uint32_t)sz_buf[2] << 16) +
            ((uint32_t)sz_buf[3] << 24);
        printf("OTA: Compressed image detected. compressed size: %u\n", (unsigned int)compressed_size);
        if (inflator)
            delete inflator, inflator = nullptr;
        using namespace std::placeholders;
        mz_inflator_t::write_fn_t write_fn = [this](const uint8_t *buf, size_t size) -> int {
            inherited::write_data(buf, size);
            return inherited::status == stNoError ? 0 : -1;
        };
        inflator = new mz_inflator_t(write_fn, compressed_size);
    }

    if (total_received_bytes >= mark_size + 4 && size)
    {
        // compressed data
        int status = inflator->eat(buf, size);
        if(status <= TINFL_STATUS_DONE && status != TINFL_STATUS_DONE)
        {
            // decompression error
            printf("OTA: Compressed image corrupted. status: %i\n", status);
            status = stCorrupted;
            return;
        }
        total_received_bytes += size;
    }
}

compressed_updater_t Updater;

void show_ota_boot_status()
{
    int part = get_current_active_partition_number();
    printf("Booting from the OTA partition : %s\n",
           (part == 0) ? "app0" : (part == 1) ? "app1"
                                              : "unknown");
}

void reboot(bool clear_settings)
{
    if (clear_settings)
    {
        // put all settings clear inidication file
        FILE *f = fopen(CLEAR_SETTINGS_INDICATOR_FILE, "w");
        if (f)
            fclose(f);
    }
    // ummm...
    // As far as I know, spiffs is always-consistent,
    // so at any point, rebooting the hardware may not corrupt
    // the filesystem. Obviously FAT is not. Take care if
    // using micro SD cards.
    printf("Rebooting ...\n");
    delay(1000);
    ESP.restart();
    for (;;) /**/
        ;

    return; // must be non-reachable
}
