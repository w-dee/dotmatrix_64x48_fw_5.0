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

#ifndef _FLASH_FS_H_
#define _FLASH_FS_H_

#include "FS.h"

namespace fs
{

class ANY_LittleFSFS : public FS
{
public:
    ANY_LittleFSFS();
    bool begin(bool formatOnFail=false, const char *label = nullptr, const char * basePath="/fs", uint8_t maxOpenFiles=10);
    bool format(const char * label = nullptr);
};

}

extern fs::ANY_LittleFSFS FS;

const char *get_main_flash_fs_partition_name(int generation);

void init_fs();

#endif