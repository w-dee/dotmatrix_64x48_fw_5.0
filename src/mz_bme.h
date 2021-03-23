#pragma once

void init_bme280();
void poll_bme280();

struct bme280_result_t
{
    int temp_10; // temperature in degree Celsius
    int pressure; // pressure in hPa
    int humidity; // humidity in RH%
};

extern bme280_result_t bme280_result;
