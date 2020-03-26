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

using namespace fs;

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

ANY_SPIFFSFS::ANY_SPIFFSFS() : FS(FSImplPtr(new SPIFFSImpl()))
{

}

bool ANY_SPIFFSFS::begin(bool formatOnFail, const char *label, const char * basePath, uint8_t maxOpenFiles)
{
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

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if(err != ESP_OK){
        log_e("Mounting SPIFFS failed! Error: %d", err);
        return false;
    }
    _impl->mountpoint(basePath);
    return true;
}

ANY_SPIFFSFS FS; // global instance


void init_fs()
{
    ::FS.begin(true, nullptr);
}

