#include <Arduino.h>
#include <Wire.h>
#include <SensirionI2CScd4x.h>

#define I2C_SDA_PIN 6
#define I2C_SCL_PIN 7

SensirionI2cScd4x scd4x;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  scd4x.begin(Wire, 0x62);
  
  scd4x.stopPeriodicMeasurement();
  delay(500);

  float offset;
  scd4x.getTemperatureOffset(offset);
  Serial.printf("Current offset: %.2f°C\n", offset);

  uint16_t asc_enabled;
  scd4x.getAutomaticSelfCalibrationEnabled(asc_enabled);
  Serial.printf("Auto-calibration: %s\n", asc_enabled ? "ENABLED" : "DISABLED");

  Serial.println("\nPerforming factory reset...");
  scd4x.performFactoryReset();
  delay(2000);

  scd4x.begin(Wire, 0x62);
  delay(500);

  scd4x.getTemperatureOffset(offset);
  Serial.printf("New offset: %.2f°C\n", offset);

  Serial.println("\nEnabling auto-calibration...");
  scd4x.setAutomaticSelfCalibrationEnabled(1);
  delay(500);

  scd4x.getAutomaticSelfCalibrationEnabled(asc_enabled);
  Serial.printf("Auto-calibration: %s\n", asc_enabled ? "ENABLED" : "DISABLED");

  Serial.println("\nDone");
}

void loop() {
  delay(1000);
}
