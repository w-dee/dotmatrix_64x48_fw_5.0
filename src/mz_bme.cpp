#include <Arduino.h>
#include "bme280.h"
#include "interval.h"
#include "mz_bme.h"

static BME280 bme280;
void init_bme280()
{
    bme280.begin();
    bme280.setMode(BME280_MODE_NORMAL, BME280_TSB_1000MS, BME280_OSRS_x4,
		BME280_OSRS_x4, BME280_OSRS_x4, BME280_FILTER_OFF);
}

static void poll()
{
    double temp = 0, hum = 0, press = 0;
    bme280.getData(&temp, &hum, &press);
    bme280_result.temp_10 = (temp < 0) ? (temp*10 - 0.5) : (temp*10 + 0.5);
    bme280_result.pressure = press + 0.5;
    bme280_result.humidity = hum + 0.5;
}

bme280_result_t bme280_result;

void poll_bme280()
{
    poll();
}