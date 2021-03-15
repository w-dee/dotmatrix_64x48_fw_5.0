#include <Arduino.h>
#include <core_version.h>


extern "C" {
// functions are in mz_build_version.c
const char * get_git_rev();
const char * get_build_date();
}



/**
 * Get version info string in JSON syntax
 * */
String version_get_info_string()
{
    return
        String("ESP_IDF_version: \"") + esp_get_idf_version() + "\""
        ",\n" +
        String("Arduino_version: \"") + ARDUINO_ESP32_RELEASE + "\""
        ",\n" +
        String("Source_git_revision: \"") + get_git_rev() + "\""
        ",\n" +
        String("Build_date: \"") + get_build_date() + "\""
         ;
}
