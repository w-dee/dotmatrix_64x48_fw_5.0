// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Modified by W.Dee to be able to mount specified partition label
// Contents are almost taken from SPIFFS.cpp/.h from the original ESP32 Arduino library.

#include "vfs_api.h"

extern "C" {
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_spiffs.h"
}

#include "spiffs_fs.h"
#include "mz_update.h"

using namespace fs;

namespace 
{

    class SPIFFSImpl : public VFSImpl
    {
    public:
        SPIFFSImpl();
        virtual ~SPIFFSImpl() { }
        virtual bool exists(const char* path);
    };

    SPIFFSImpl::SPIFFSImpl()
    {
    }

    bool SPIFFSImpl::exists(const char* path)
    {
        File f = open(path, "r");
        return (f == true) && !f.isDirectory();
    }


}

ANY_SPIFFSFS::ANY_SPIFFSFS() : FS(FSImplPtr(new SPIFFSImpl()))
{

}

bool ANY_SPIFFSFS::begin(bool formatOnFail, const char *label, const char * basePath, uint8_t maxOpenFiles)
{
    const char *p_label = label ? label : "(default)"; // for print only
    printf("Mounting SPIFFS ... Label:%s\r\n", p_label);

    if(esp_spiffs_mounted(label)){
        log_w("SPIFFS Already Mounted!");
        return true;
    }



    esp_vfs_spiffs_conf_t conf = {
      .base_path = basePath,
      .partition_label = label,
      .max_files = maxOpenFiles,
      .format_if_mount_failed = formatOnFail 
    };

    // Manual reformatting as coded in the original SPIFFS library does not work
    // on my chip. Instead, esp_vfs_spiffs_conf_t::format_is_mount_failed works.
    // Why ?

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if(err != ESP_OK){
        printf("Mounting SPIFFS failed! Label:%s Error: %d\r\n", p_label, err);
        // TODO: PANIC
        return false;
    }
    _impl->mountpoint(basePath);
    printf("Mounting SPIFFS succeeded. Label:%s\r\n", p_label);
 
    size_t total,used;
    if(ESP_OK == esp_spiffs_info(label, &total, &used)){
        printf("Label:%s  Total:%ld  Used:%ld\r\n", p_label, (long)total, (long)used);
    }

    return true;
}

bool ANY_SPIFFSFS::format(const char *label)
{
    disableCore0WDT();
    esp_err_t err = esp_spiffs_format(label);
    enableCore0WDT();
    if(err){
        log_e("Formatting SPIFFS failed! Error: %d", err);
        return false;
    }
    return true;
}

ANY_SPIFFSFS FS; // global instance


void init_fs()
{
   	puts("Main SPIFFS initializing ...");
    ::FS.begin(true, (get_current_active_partition_number()==1) ? "spiffs1" : "spiffs");
    // see custom.csv for the partition label.
}

