#include <Arduino.h>
#include <Wire.h>

#define PIN_SDA 21
#define PIN_SCL 22


/**
 * Scan I2C bus
 * */


/**
 * Initialize the i2s system
 * */
void init_i2c()
{
    printf("I2C subsystem initializing...\n");
    Wire.begin(PIN_SDA, PIN_SCL);

    printf("I2C map:\n  ");
    int addr;
    for(addr = 0; addr < 128; addr++ )
    {
        if(addr == 0 || addr == 127)
        {
            printf("xx ");
            continue;
        }
        Wire.beginTransmission(addr);
        int error = Wire.endTransmission();
    
        if (error == 0)
        {
            printf("%02x ", addr);
        }
        else
        {
            printf("-- ");
        }
        if((addr & 0x0f) == 0x0f) printf("\n  ");
    }
    printf("\n");
}


