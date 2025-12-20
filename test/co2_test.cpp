#include <Arduino.h>
#include <Wire.h>
#include <SensirionI2CScd4x.h>

// #define FF  // Uncomment to enable forced recalibration to 400ppm

#define I2C_SDA_PIN 6
#define I2C_SCL_PIN 7

#define EnableCalibration true

SensirionI2cScd4x scd4x;
uint8_t consecutiveErrors = 0;
uint32_t lastReadTime = 0;

void checkI2C() {
  Wire.beginTransmission(0x62);
  uint8_t result = Wire.endTransmission();
  if (result != 0) {
    Serial.printf("FAIL: I2C device not found (error %d)\n", result);
    Serial.println("Check: SDA=D6, SCL=D7, power, pull-ups");
    while (1) delay(1000);
  }
  Serial.println("OK: I2C device detected at 0x62");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== SCD40 Diagnostic ===");

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  checkI2C();

  scd4x.begin(Wire, 0x62);
  
  // Stop any running measurement (ignore error if none running)
  scd4x.stopPeriodicMeasurement();
  delay(500);

#if EnableCalibration
  Serial.println("CALIBRATION MODE: Forced recalibration to 500ppm");
  Serial.println("Ensure sensor is in fresh outdoor air!");
  Serial.println("Calibrating in 60 seconds...");
  delay(60000);
  
  uint16_t frcCorrection;
  uint16_t err2 = scd4x.performForcedRecalibration(500, frcCorrection);
  if (err2) {
    Serial.printf("FAIL: Calibration error %d\n", err2);
  } else {
    Serial.printf("OK: Calibration complete (correction: %d ppm)\n", frcCorrection - 0x8000);
    Serial.println("Calibration saved to sensor EEPROM");
  }
  delay(500);
#endif

  uint16_t err = scd4x.startPeriodicMeasurement();
  if (err) {
    Serial.printf("FAIL: startPeriodicMeasurement error %d\n", err);
    Serial.println("Sensor communication failed - check sensor compatibility");
    while (1) delay(1000);
  }

  Serial.println("OK: Measurement started");
  Serial.println("Reading every 5s (first valid after ~5s)...\n");
  lastReadTime = millis();
}

void loop() {
  if (millis() - lastReadTime < 5000) return;
  lastReadTime = millis();

  bool ready = false;
  uint16_t err = scd4x.getDataReadyStatus(ready);
  
  if (err) {
    Serial.printf("FAIL: getDataReadyStatus error %d\n", err);
    consecutiveErrors++;
    if (consecutiveErrors > 3) {
      Serial.println("CRITICAL: Communication lost - resetting...");
      ESP.restart();
    }
    return;
  }

  if (!ready) {
    Serial.println("WARN: Data not ready (normal on first read)");
    return;
  }

  uint16_t co2;
  float temp, hum;
  err = scd4x.readMeasurement(co2, temp, hum);

  if (err) {
    Serial.printf("FAIL: readMeasurement error %d\n", err);
    consecutiveErrors++;
    if (consecutiveErrors > 3) {
      Serial.println("CRITICAL: Multiple read failures - check sensor");
    }
    return;
  }

  consecutiveErrors = 0;

  // Validate readings
  bool valid = true;
  if (co2 == 0 || co2 > 5000) {
    Serial.printf("WARN: CO2 %d ppm out of range\n", co2);
    valid = false;
  }
  if (temp < -10 || temp > 60) {
    Serial.printf("WARN: Temp %.1f°C out of range\n", temp);
    valid = false;
  }
  if (hum < 0 || hum > 100) {
    Serial.printf("WARN: Humidity %.1f%% out of range\n", hum);
    valid = false;
  }

  if (valid) {
    Serial.printf("OK: %d ppm, %.1f°C, %.1f%%\n", co2, temp, hum);
  }
}
