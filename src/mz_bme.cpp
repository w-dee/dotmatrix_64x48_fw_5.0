#include <Arduino.h>
#include "bme280.h"
#include "interval.h"

static BME280 bme280;
void init_bme280()
{
    bme280.begin();
    bme280.setMode(BME280_MODE_NORMAL, BME280_TSB_1000MS, BME280_OSRS_x4,
		BME280_OSRS_x4, BME280_OSRS_x4, BME280_FILTER_OFF);
}


void poll_bme280()
{
    EVERY_MS(5000)
    {
        double temp = 0, hum = 0, press = 0;
        bme280.getData(&temp, &hum, &press);
        printf("temp:%f, hum:%f, pressure:%f\n", temp, hum, press);
    }
    END_EVERY_MS
}