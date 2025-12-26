#include "PinConfig.h"
#include "../include/config.h"
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <SensirionI2CScd4x.h>
#include <GxEPD2_BW.h>
#include <ArduinoJson.h>
#include "rendering.h"
#include <esp_sleep.h>

#define LOGGING_ENABLED false

const unsigned long UPDATE_INTERVAL_MS = 300 * 1000; // because of scd40 it must be > 30s
const unsigned long WEATHER_UPDATE_INTERVAL_MS = 3600 * 1000;


DisplayType display(GxEPD2_397_GDEM0397T81(EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN));
Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;
SensirionI2cScd4x scd4x;

const int FORECAST_HOURS = 24;

float tempAir = 0, humidity = 0, tempESP = 0, pressure = 1000, batteryVoltage = 0, co2 = 0, moonPhase = 0;

RTC_DATA_ATTR float rtc_forecastTemp[FORECAST_HOURS];
RTC_DATA_ATTR float rtc_forecastRain[FORECAST_HOURS];
RTC_DATA_ATTR char rtc_sunriseTimeStr[6] = "--:--";
RTC_DATA_ATTR char rtc_sunsetTimeStr[6] = "--:--";
RTC_DATA_ATTR int rtc_forecastStartHour = 0;
RTC_DATA_ATTR bool rtc_weatherDataValid = false;
RTC_DATA_ATTR uint32_t rtc_bootCount = 0;
RTC_DATA_ATTR uint32_t rtc_bootsFromLastForecastFetch = 0;
RTC_DATA_ATTR uint32_t rtc_weatherFetchTimestamp = 0;

// ################################ Moon Phase #################################

// Returns moon phase as percentage: 0.0 = new moon, 0.5 = full moon, 1.0 = new moon
void getMoonPhase() {
  const uint32_t FULL_MOON_REF = 1763614318;
  const uint32_t LUNAR_CYCLE = 2551443; // 29.53 days in seconds
  uint32_t currentTime = rtc_weatherFetchTimestamp;
  uint32_t elapsed = currentTime - FULL_MOON_REF;
  moonPhase = (float)(elapsed % LUNAR_CYCLE) / (float)LUNAR_CYCLE;
}

// ################################ Sensors ####################################

void initSensors() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);
  delay(1);
  
  if (!aht.begin(&Wire)) {
    #if LOGGING_ENABLED
      Serial.println("AHT init fail");
    #endif
  }
  
  if (!bmp.begin(BMP280_ADDRESS_ALT)) {
      #if LOGGING_ENABLED
        Serial.println("BMP280 init fail");
      #endif
  }
  
  scd4x.begin(Wire, SCD40_I2C_ADDR_62);
  delay(30);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(POWER_SENSING_PIN, INPUT);
}

void readSensorBMP(){
  // Configure BMP280 for forced mode before measurement
  bmp.setSampling(Adafruit_BMP280::MODE_FORCED,
                  Adafruit_BMP280::SAMPLING_X1,  // temperature
                  Adafruit_BMP280::SAMPLING_X4,  // pressure (reduced from X16 for power savings)
                  Adafruit_BMP280::FILTER_OFF);
  
  bmp.takeForcedMeasurement();  // Wake, measure, return to sleep
  pressure = bmp.readPressure() / 100.0f;  // Convert Pa to hPa
  
  if(pressure < 5000 && pressure > 300)
    scd4x.setAmbientPressure((uint16_t)pressure * 100);

  if(pressure > 5000 || pressure < 300) pressure = -3.0f;
}

void readSensorAHT(){
  sensors_event_t hum, temp;
  aht.getEvent(&hum, &temp);
  tempAir = temp.temperature;
  humidity = hum.relative_humidity;

  if(humidity < 0 || humidity > 100) humidity = -3.0f;
  if(tempAir < -40 || tempAir > 85) tempAir = -3.0f;
}

void readSensorSCD(){
  #if LOGGING_ENABLED
    Serial.println("Initializing SCD40");
  #endif

  uint16_t error = scd4x.startPeriodicMeasurement();
  if (error == 0) {
    #if LOGGING_ENABLED
      Serial.println("SCD40 started successfully");
    #endif
  } else {
    #if LOGGING_ENABLED
      Serial.print("SCD40 start error: ");
      Serial.println(error);
    #endif
  }

  uint16_t _co2Raw;
  float _tempSCD, _humSCD;
  
  scd4x.readMeasurement(_co2Raw, _tempSCD, _humSCD);
  #if LOGGING_ENABLED
  delay(5000);
  #else  
  esp_sleep_enable_timer_wakeup(5000000); // ms
  esp_light_sleep_start();
  #endif
  bool dataReady = false;
  scd4x.getDataReadyStatus(dataReady);
  
  if (dataReady) {
    uint16_t co2Raw;
    int error = scd4x.readMeasurement(co2Raw, _tempSCD, _humSCD);
    if (error == 0) {
      co2 = co2Raw;
    } else {
      co2 = -error;
    }
  } else {
    co2 = -1.0f;
  }

  scd4x.stopPeriodicMeasurement();

  // Check if CO2 value is in invalid range (0 < CO2 < 400) and recalibrate
  if (co2 > 0 && co2 < 400) {
    #if LOGGING_ENABLED
      Serial.print("CO2 value ");
      Serial.print(co2);
      Serial.println(" is below 400ppm, performing recalibration to 400ppm");
    #endif
    
    uint16_t frcCorrection;
    uint16_t calibrationError = scd4x.performForcedRecalibration(400, frcCorrection);
    
    #if LOGGING_ENABLED
      if (calibrationError) {
        Serial.print("Calibration error: ");
        Serial.println(calibrationError);
      } else {
        Serial.print("Calibration successful. Correction: ");
        Serial.print((int16_t)(frcCorrection - 0x8000));
        Serial.println(" ppm");
      }
    #endif
  }

  if(co2 > 10000) co2 = -3.0f;
}

void readSensorBatteryVoltage(){
  uint32_t batteryVoltageSum = 0;
  for (int i = 0; i < BATTERY_AVERAGE_SAMPLES; i++) {
    batteryVoltageSum += analogReadMilliVolts(POWER_SENSING_PIN);
  }
  batteryVoltage = (batteryVoltageSum / static_cast<float>(BATTERY_AVERAGE_SAMPLES)) * VOLTAGE_DIVIDER_RATIO / 1000.0;
}

void readSensors() {
  readSensorBatteryVoltage();
  tempESP = temperatureRead();
  readSensorBMP();
  readSensorAHT();
  readSensorSCD();
}

// ############################### Internet ####################################

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_5dBm);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void waitForWiFi(int timeoutMs = 10000) {
  for (int i = 0; i < timeoutMs && WiFi.status() != WL_CONNECTED; i+=50){
    delay(50);
  }
  
  #if LOGGING_ENABLED
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi OK");
    } else {
      Serial.println("WiFi Failed");
    }
  #endif
}

void fetchWeatherForecast() {
  if (WiFi.status() != WL_CONNECTED) {
    #if LOGGING_ENABLED
      Serial.println("WiFi not connected, skipping weather update");
    #endif
    return;
  }
  
  #if LOGGING_ENABLED
    Serial.println("Fetching weather forecast...");
  #endif
  HTTPClient http;
  http.begin("https://api.open-meteo.com/v1/forecast?latitude=50.06&longitude=14.419998&timezone=Europe%2FBerlin&forecast_days=1&hourly=temperature_2m,rain,snowfall&daily=sunset,sunrise&forecast_hours=24&models=icon_d2");
  
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      #if LOGGING_ENABLED
        Serial.print("JSON parse error: ");
        Serial.println(error.c_str());
      #endif
      http.end();
      return;
    }
    
    // Parse hourly temperature and rain
    JsonArray tempArray = doc["hourly"]["temperature_2m"];
    JsonArray rainArray = doc["hourly"]["rain"];
    JsonArray snowArray = doc["hourly"]["snowfall"];
    JsonArray timeArray = doc["hourly"]["time"];
    
    if (timeArray.size() > 0) {
      String firstTime = timeArray[0].as<String>();
      rtc_forecastStartHour = firstTime.substring(11, 13).toInt();
      
      int year = firstTime.substring(0, 4).toInt();
      int month = firstTime.substring(5, 7).toInt();
      int day = firstTime.substring(8, 10).toInt();
      int hour = firstTime.substring(11, 13).toInt();
      
      struct tm timeinfo = {0};
      timeinfo.tm_year = year - 1900;
      timeinfo.tm_mon = month - 1;
      timeinfo.tm_mday = day;
      timeinfo.tm_hour = hour;
      rtc_weatherFetchTimestamp = mktime(&timeinfo);
    }
    
    for (int i = 0; i < FORECAST_HOURS && i < tempArray.size(); i++) {
      rtc_forecastTemp[i] = tempArray[i];
      // Combine rain and snowfall (snowfall in cm, convert to mm equivalent)
      float rain = rainArray[i] | 0.0f;
      float snow = snowArray[i] | 0.0f;
      rtc_forecastRain[i] = rain + (snow * 10.0f);  // 1cm snow ≈ 10mm water
    }
    
    // Parse sunrise and sunset times
    if (doc["daily"]["sunrise"][0]) {
      String sunriseStr = doc["daily"]["sunrise"][0].as<String>();
      String sunriseShort = sunriseStr.substring(11, 16); // Extract HH:MM
      sunriseShort.toCharArray(rtc_sunriseTimeStr, 6);
    }
    if (doc["daily"]["sunset"][0]) {
      String sunsetStr = doc["daily"]["sunset"][0].as<String>();
      String sunsetShort = sunsetStr.substring(11, 16); // Extract HH:MM
      sunsetShort.toCharArray(rtc_sunsetTimeStr, 6);
    }
    rtc_weatherDataValid = true;
    #if LOGGING_ENABLED
      Serial.println("Weather data updated successfully");
      Serial.print("Sunrise: ");
      Serial.print(rtc_sunriseTimeStr);
      Serial.print(" Sunset: ");
      Serial.println(rtc_sunsetTimeStr);
    #endif
  } else {
    #if LOGGING_ENABLED
      Serial.print("HTTP error: ");
      Serial.println(httpCode);
    #endif
  }
  
  http.end();
}

void sendToThingSpeak() {
  if (WiFi.status() != WL_CONNECTED){
    #if LOGGING_ENABLED
      Serial.println("WiFi not connected, skipping ThingSpeak upload");
    #endif
    return;
  }

  String url;
  url.reserve(256);
  url += "http://api.thingspeak.com/update?api_key=";
  url += THINGSPEAK_API_KEY;
  url += "&field1=" + String(tempAir, 2);
  url += "&field2=" + String(tempESP, 2);
  url += "&field3=" + String(humidity, 2);
  url += "&field4=" + String(co2, 0);
  url += "&field5=" + String(pressure, 0);
  url += "&field6=" + String(batteryVoltage, 4);
  
  HTTPClient http;
  http.begin(url);
  http.setTimeout(100); // We don't need response, just send data quickly
  int code = http.GET();
  http.end();
  
  #if LOGGING_ENABLED
    if (code > 0) Serial.println("Upload OK");
    else Serial.println("Upload failed");
  #endif
}

// ################################ Display ####################################

void initDisplay1() {
  // BC327 is PNP transistor - LOW turns it ON (provides power to display)
  pinMode(EPD_TRANSISTOR_PIN, OUTPUT);
  digitalWrite(EPD_TRANSISTOR_PIN, LOW);  // Turn on transistor to power display
  
  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, HIGH);
}

void initDisplay2() {
  display.init(115200, false, 2, false);
  display.setRotation(2); // landscape
  display.setTextColor(GxEPD_BLACK);
}

void turnOffDisplay() {
  display.powerOff();
  digitalWrite(EPD_PWR_PIN, LOW);
  pinMode(EPD_CS_PIN, INPUT);
  pinMode(EPD_DC_PIN, INPUT);
  pinMode(EPD_SCK_PIN, INPUT);
  pinMode(EPD_MOSI_PIN, INPUT);
  pinMode(EPD_RST_PIN, INPUT);
  pinMode(EPD_BUSY_PIN, INPUT);
  digitalWrite(EPD_TRANSISTOR_PIN, LOW);
}

// ################################ Setup ####################################

void setup() {
  rtc_bootCount++;
  rtc_bootsFromLastForecastFetch++;

  #if LOGGING_ENABLED
    Serial.begin(115200);
    while(!Serial) {
      delay(10);
    }
  #endif

  if(rtc_bootCount == 1) {
    delay(10000); // Wait for possible upload
  }

  bool largeUpdate = rtc_bootCount == 1 || (rtc_bootsFromLastForecastFetch * UPDATE_INTERVAL_MS) >= WEATHER_UPDATE_INTERVAL_MS;


  initSensors();
  readSensors();
  initDisplay1(); 
  connectWiFi();
  
  if (largeUpdate) {
    waitForWiFi();
    fetchWeatherForecast();
    rtc_bootsFromLastForecastFetch = 0;
  }

  initDisplay2();

  if (largeUpdate) {
    largeAntiGhosting(display);
  } else {
    smallAntiGhosting(display);
  }

  getMoonPhase();
  updateDisplay(
    display,
    tempAir,
    humidity,
    co2,
    pressure,
    rtc_sunriseTimeStr,
    rtc_sunsetTimeStr,
    rtc_forecastTemp,
    rtc_forecastRain,
    FORECAST_HOURS,
    rtc_forecastStartHour,
    rtc_weatherDataValid,
    moonPhase
  );
  
  waitForWiFi();
  sendToThingSpeak();

  WiFi.disconnect(true);

  turnOffDisplay();

  unsigned long sleepTimeUs = max((UPDATE_INTERVAL_MS - millis()) * 1000ULL, 1000ULL);


  #if LOGGING_ENABLED
    Serial.println("faking deep sleep for debug");
    delay(10);
    Serial.end();
    delay(sleepTimeUs / 1000);
    ESP.restart();
  #endif
    
  esp_sleep_enable_timer_wakeup(sleepTimeUs);
  esp_deep_sleep_start();
}

void loop() {
  // Not used, all logic in setup for deep sleep cycle
}
