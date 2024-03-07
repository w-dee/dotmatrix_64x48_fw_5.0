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
#include "esp_littlefs.h"
}

#include "flash_fs.h"
#include "mz_update.h"
#include "web_server.h"

using namespace fs;

namespace 
{

    class LittleFSImpl : public VFSImpl
    {
    public:
        LittleFSImpl();
        virtual ~LittleFSImpl() { }
        virtual bool exists(const char* path);
    };

    LittleFSImpl::LittleFSImpl()
    {
    }

    bool LittleFSImpl::exists(const char* path)
    {
        File f = open(path, "r", false);
        return (f == true) && !f.isDirectory();
    }


}

ANY_LittleFSFS::ANY_LittleFSFS() : FS(FSImplPtr(new LittleFSImpl()))
{

}

bool ANY_LittleFSFS::begin(bool formatOnFail, const char *label, const char * basePath, uint8_t maxOpenFiles)
{
    const char *p_label = label ? label : "(default)"; // for print only
    printf("Mounting LittleFS ... Label:%s\r\n", p_label);

    if(esp_littlefs_mounted(label)){
        log_w("LittleFS Already Mounted!");
        return true;
    }



    esp_vfs_littlefs_conf_t conf = {
      .base_path = basePath,
      .partition_label = label,
      .format_if_mount_failed = formatOnFail 
    };

    // Manual reformatting as coded in the original SPIFFS library did not work
    // on my chip. Instead, esp_vfs_spiffs_conf_t::format_is_mount_failed worked.
    // Here, we use LittleFS instead, but I don't know the same problem exists.

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if(err != ESP_OK){
        printf("Mounting LittleFS failed! Label:%s Error: %d\r\n", p_label, err);
        // TODO: PANIC
        return false;
    }
    _impl->mountpoint(basePath);
    printf("Mounting LittleFS succeeded. Label:%s\r\n", p_label);
 
    size_t total,used;
    if(ESP_OK == esp_littlefs_info(label, &total, &used)){
        printf("Label:%s  Total:%ld  Used:%ld  Mount point:%s\r\n", p_label, (long)total, (long)used,
            basePath);
    }

    return true;
}

bool ANY_LittleFSFS::format(const char *label)
{
    disableCore0WDT();
    esp_err_t err = esp_littlefs_format(label);
    enableCore0WDT();
    if(err){
        log_e("Formatting LittleFS failed! Error: %d", err);
        return false;
    }
    return true;
}

ANY_LittleFSFS FS; // global instance


// returns partition label for specified number, 0 or 1.
const char * get_main_flash_fs_partition_name(int generation)
{
    const esp_partition_t * par =  esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                                                        (generation == 0) ? "spiffs0" : "spiffs1"); // old partition naming scheme
    if(!par)
            par =                  esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                                                        (generation == 0) ? "fs0" : "fs1"); // new partition naming scheme
    if(!par) return nullptr;
    return par->label; // this is ok because the pointer returned from esp_partition_find_first is safe during application run.
}

// initialize the main filesystem
void init_fs()
{
   	puts("Main LittleFS initializing ...");
    if(!(
        ::FS.begin(true, get_main_flash_fs_partition_name(get_current_active_partition_number())) 
    ))
    {
        // main LittleFS mount failed! go in recovery mode
        printf("Main LittleFS mount failed. Going to system recovery mode ...\n");
        set_system_recovery_mode();
    }
    // see custom.csv for the partition label.
}

