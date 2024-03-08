#pragma once

#include <Arduino.h>
#include "esp_spi_flash.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include <MD5Builder.h>

class partition_updater_t
{
public:
    enum update_type_t 
    {
        utUnknown,
        utCode,
        utFS,
        utFont,
    };
    partition_updater_t() : _type(utUnknown), _size(0), _first_byte(0xff), _progress(0), _partition(nullptr) {}
    ~partition_updater_t() {}

    bool begin(update_type_t type, uint32_t size);
    bool write_sector(const uint8_t *buf); //!< write a sector to current position
    bool match_md5(const uint8_t *md5);
    bool activate_new_code(); //!< activate newly written code (only for type == utCode)

    static const esp_partition_t* next_partition_from_type(update_type_t _type);

private:
    update_type_t _type;
    uint32_t _size;
    uint8_t _first_byte; //!< first byte of the partition (usually a magic number)
    uint32_t _progress;
    const esp_partition_t* _partition;
    MD5Builder _md5;
};

int get_current_active_partition_number();

// update management class
class updater_t
{
    uint8_t *buffer;
    partition_updater_t partition_updater;

#pragma pack(push, 4)
    struct partition_header_t
    {
        char label[8];
        uint32_t orig_len;
        uint32_t arc_len;
        uint8_t md5[16];
    };
#pragma pack(pop)

    enum phase_t
    {
        phBegin, // the begining, waiting for the first header
        phHeader, // waiting for the partition header
        phContent, // waiting for the content
    };

    size_t remaining_count; // remaining block count
    size_t buffer_pos; // buffer writing position
    partition_header_t header; // current partition header

public:
    enum status_t
    {
        stNoError,
        stCorrupted, // data corrupted
    };
protected:
    status_t status;
    phase_t phase;

public:
    updater_t() {;}

    void begin();
    void end(); // should be explicitly called because this method frees large(over 4kB+32kB) buffer


private:
    void process_block(); // process one block

public:
    void write_data(const uint8_t * buf, size_t size); // write a block
    bool finish(); // call this when all data is sent via write_data(); returns whether the update is succeeded or not

    status_t get_last_status() const { return status; } // returns last status
};


// inflator forward decl
class mz_inflator_t;

/**
 * updater management class which consumes compressed firmware stream
 * */
class compressed_updater_t : public updater_t
{
    typedef updater_t inherited;

    size_t total_received_bytes = 0; // total received bytes of compressed stream
    uint32_t compressed_size = 0;
    mz_inflator_t * inflator = nullptr;
    uint8_t sz_buf[4]; // buffer for the size
public:
    /*override*/ void begin();
    /*override*/ void end();

    /*override*/ void write_data(const uint8_t * buf, size_t size); // write a block

};

/**
 * updater global instance
 * */
extern compressed_updater_t Updater;


/**
 * Show current OTA status
 * */
void show_ota_boot_status();

/**
 * Reboot and optionally clear settings
 * */
void reboot(bool clear_settings = false);

