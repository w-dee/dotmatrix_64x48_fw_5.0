#include <Arduino.h>
#include <stdlib.h>
#include <esp_partition.h>
#include <string.h>
#include <esp_system.h>
#include "frame_buffer.h"
#include "ambient.h"

static spi_flash_mmap_handle_t mmap_handle = 0; // mmap handle
static uint32_t start_address_in_flash = 0; // data start address in flash
static uint32_t mapped_start_address_in_data = 0; // currently mapped address in data
static const uint8_t * mapped_address_head = nullptr;
static uint32_t movie_size = 0; // movie size


#define MMAP_BLOCK_SIZE (65536UL*2)
#define MMAP_ACCESS_SIZE 8192
/**
 * ensure the given address is accessible from the program.
 * the address space accessible is gurannteed at least 8kiB from the starting address.
 * */
static const uint8_t * ensure_access(uint32_t address)
{
    // because spi_flash_mmap can map only a small space,
    // we have to do some caching here.

    // check the requested address is within current scope
    if(mapped_address_head && address >= mapped_start_address_in_data &&
        (address+MMAP_ACCESS_SIZE) <= (mapped_start_address_in_data+MMAP_BLOCK_SIZE))
    {
        // within current space;
        return address - mapped_start_address_in_data + mapped_address_head;
    }

    // not in current address space
    // remap
    if(mmap_handle) { spi_flash_munmap(mmap_handle); mmap_handle = 0; }

    mapped_start_address_in_data = address / 65536 * 65536;

    uint32_t map_start_address = start_address_in_flash + mapped_start_address_in_data;
    if(ESP_OK != spi_flash_mmap(map_start_address, MMAP_BLOCK_SIZE, SPI_FLASH_MMAP_DATA,
        (const void**)&mapped_address_head, &mmap_handle))
    {
        printf("BAD APPLE: mmap failed in flash address 0x%lx\n", (long)map_start_address);
    }
    
    // return the address
    return mapped_address_head - mapped_start_address_in_data + address;
}

/**
 * decode thread
 * */
static void decode_task(void *arg)
{
    for(;;)
    {
        uint32_t addr = 12; // skip signatures

        while(addr < movie_size)
        {
            const uint8_t * ptr, *start_ptr;
            ptr = start_ptr = ensure_access(addr);

            // expand the buffer
            uint8_t * exp_buf = (uint8_t*) get_bg_frame_buffer().array();
            int out_pos = 0;
            while(out_pos < 64*48)
            {
                if(*ptr & 0x80)
                {
                    // running
                    uint8_t len = *ptr & 0x7f;
                    ++ptr;
                    uint8_t val = *ptr;
                    ++ptr;
                    while(len--)
                        exp_buf[out_pos++] = val;
                }
                else
                {
                    // non-running
                    uint8_t len = *ptr;
                    ++ptr;
                    while(len--)
                        exp_buf[out_pos++] = *(ptr++);
                }
            }

            addr += ptr - start_ptr;

            frame_buffer_flip();
            delay(33); // TODO: precious timing control
        }
    }
}


static bool running = false;
bool bad_apple()
{
    // initialize bad_apple
    if(running) return true;

	sensors_set_contrast_always_max(false);

    // find data start address
    const esp_partition_t * part =
        esp_partition_find_first((esp_partition_type_t)0x40,
        (esp_partition_subtype_t)0, NULL);
    if(part == nullptr)
    {
        // no font0 partition
        return false;
    }

    // read signature
    struct sig_t
    {
        uint8_t sig[8];
        uint32_t size;
    } sig;
    if(ESP_OK != spi_flash_read(part->address, &sig, sizeof(sig))) return false;
    if(memcmp(sig.sig, "BADAPPLE\0", 8)) return false; // singnature mismatch

    printf("\n\nBAD APPLE: signature found at 0x%lx, length %d\n", (long)part->address, sig.size);
    start_address_in_flash = part->address;
    movie_size = sig.size; // including signature

    // check ...
    for(int i = 0; i < 1024; ++i)
    {
        uint32_t address = esp_random() % sig.size;
        address &= ~3;
        uint8_t data[4];
        spi_flash_read(part->address + address, data, 4);
        const uint8_t *access = ensure_access(address);
        if(memcmp(access, data, 4))
        {
            printf("BAD APPLE: sanity check failed at flash address 0x%lx, data address 0x%lx\n",
                (long)part->address + address, (long)address);
            return false;
        }
    }
    printf("BAD APPLE: flash sanity check passed\n");

    // spawn decode thread
    running = true;

    xTaskCreatePinnedToCore(decode_task, "BadApple decoder", 4096, NULL, 1, NULL, 0);

    return true;
}


