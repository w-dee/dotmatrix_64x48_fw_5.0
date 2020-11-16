#include <Arduino.h>
#include <Wire.h>

#define PIN_SDA 21
#define PIN_SCL 22


/**
 * Initialize the i2s system
 * */
void init_i2c()
{
    Wire.begin(PIN_SDA, PIN_SCL);
}


