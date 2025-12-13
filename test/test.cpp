#include "Arduino.h"

void setup() {
  Serial.begin(115200);
  while(!Serial) {
	delay(10);
  }
  Serial.println("Test setup complete.");
}

void loop() {
  // Empty loop
}