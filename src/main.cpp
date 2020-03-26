#include <Arduino.h>
#include "matrix_drive.h"

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(1000);
  matrix_drive_setup();
}

void loop() {
  // put your main code here, to run repeatedly:
  matrix_drive_loop();
}