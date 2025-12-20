#include "Arduino.h"

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("setup()");
}

void loop() {
  Serial.println("loop()");
  delay(1000);
}