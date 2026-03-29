#include "PinConfig.h"
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <SensirionI2CScd4x.h>
#include <GxEPD2_BW.h>
#include <ArduinoJson.h>
#include <esp_sleep.h>
#include <time.h>

#define LOGGING_ENABLED false

#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"

#include "../include/config.h"
#include "rendering.h"
#include "home_assistant.h"



DisplayType display(GxEPD2_397_GDEM0397T81(EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN));
Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;
SensirionI2cScd4x scd4x;

const int FORECAST_HOURS = 24;

float tempAir = 0, humidity = 0, tempESP = 0, pressure = 1000, batteryVoltage = 0, co2 = 0, moonPhase = 0;
float tempSCD = 0, humiditySCD = 0;


RTC_DATA_ATTR float rtc_forecastTemp[FORECAST_HOURS];
RTC_DATA_ATTR float rtc_forecastApparentTemp[FORECAST_HOURS];
RTC_DATA_ATTR float rtc_forecastRain[FORECAST_HOURS];
RTC_DATA_ATTR char rtc_sunriseTimeStr[6] = "--:--";
RTC_DATA_ATTR char rtc_sunsetTimeStr[6] = "--:--";
RTC_DATA_ATTR int rtc_forecastStartHour = 0;
RTC_DATA_ATTR bool rtc_weatherDataValid = false;
RTC_DATA_ATTR uint32_t rtc_bootCount = 0;
RTC_DATA_ATTR uint32_t rtc_ntpBaseEpoch = 0;            // Unix epoch at last NTP sync
RTC_DATA_ATTR uint64_t rtc_elapsedUs = 0;               // Microseconds elapsed since NTP sync
RTC_DATA_ATTR uint64_t rtc_priorCycleDurationUs = 0;    // Previous cycle total duration (processing + sleep)
RTC_DATA_ATTR uint32_t rtc_lastDisplayUpdateEpoch = 0;  // Epoch of last display refresh
RTC_DATA_ATTR uint32_t rtc_lastDataSendEpoch = 0;       // Epoch of last data send
RTC_DATA_ATTR uint32_t rtc_lastWeatherFetchEpoch = 0;   // Epoch of last weather fetch
RTC_DATA_ATTR char rtc_lastUpdateTimeStr[6] = "--:--";  // hh:mm shown on display

// ################################ Moon Phase #################################

// Returns moon phase as percentage: 0.0 = new moon, 0.5 = full moon, 1.0 = new moon
void getMoonPhase(uint32_t epochTime) {
  const uint32_t FULL_MOON_REF = 1763614318;
  const uint32_t LUNAR_CYCLE = 2551443; // 29.53 days in seconds
  uint32_t elapsed = epochTime - FULL_MOON_REF;
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
  
  if(pressure < 5000 && pressure > 500)
    scd4x.setAmbientPressure((uint32_t)(pressure * 100));

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
      tempSCD = _tempSCD;
      humiditySCD = _humSCD;
    } else {
      co2 = -error;
    }
  } else {
    co2 = -1.0f;
  }

  scd4x.stopPeriodicMeasurement();

  //if (co2 >= 0 && co2 < 300) {
  //  delay(500);
  //  uint16_t frcCorrection;
  //  scd4x.performForcedRecalibration(400, frcCorrection);
  //}

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
  http.begin("https://api.open-meteo.com/v1/forecast?latitude=50.06&longitude=14.419998&timezone=Europe%2FBerlin&forecast_days=1&hourly=temperature_2m,apparent_temperature,rain,snowfall&daily=sunset,sunrise&forecast_hours=24&models=icon_d2");
  
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
    JsonArray apparentTempArray = doc["hourly"]["apparent_temperature"];
    JsonArray rainArray = doc["hourly"]["rain"];
    JsonArray snowArray = doc["hourly"]["snowfall"];
    JsonArray timeArray = doc["hourly"]["time"];
    
    if (timeArray.size() > 0) {
      String firstTime = timeArray[0].as<String>();
      rtc_forecastStartHour = firstTime.substring(11, 13).toInt();
    }
    
    for (int i = 0; i < FORECAST_HOURS && i < tempArray.size(); i++) {
      rtc_forecastTemp[i] = tempArray[i];
      rtc_forecastApparentTemp[i] = apparentTempArray[i] | rtc_forecastTemp[i];
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

// ################################ Time #####################################

bool isPowerSavingPeriod(const struct tm& t) {
  // Night: 01:00-05:29
  if (t.tm_hour >= 1 && (t.tm_hour < 5 || (t.tm_hour == 5 && t.tm_min < 30))) return true;
  // Weekday absence: Mon-Wed 10:00-14:29
  if (t.tm_wday >= 1 && t.tm_wday <= 3 &&
      t.tm_hour >= 10 && (t.tm_hour < 14 || (t.tm_hour == 14 && t.tm_min < 30))) return true;
  return false;
}

void epochToLocalTm(uint32_t epoch, struct tm& out) {
  time_t t = (time_t)epoch;
  localtime_r(&t, &out);
}

uint32_t getCurrentEpoch() {
  return rtc_ntpBaseEpoch + (uint32_t)(rtc_elapsedUs / 1000000ULL);
}

bool syncNTP() {
  uint32_t oldEpoch = getCurrentEpoch();
  
  configTzTime(TIMEZONE, "pool.ntp.org");
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5000)) return false;
  
  uint32_t newEpoch = (uint32_t)mktime(&timeinfo);
  int32_t timeDrift = rtc_ntpBaseEpoch == 0 ? 0 :(int32_t)(newEpoch - oldEpoch);
  
  rtc_ntpBaseEpoch = newEpoch;
  rtc_elapsedUs = 0;

  rtc_lastDisplayUpdateEpoch += timeDrift;
  rtc_lastDataSendEpoch += timeDrift;
  rtc_lastWeatherFetchEpoch += timeDrift;

  return true;
}

// ################################ Setup ####################################

void setup() {
  rtc_bootCount++;

  // Restore timezone after deep sleep reset
  setenv("TZ", TIMEZONE, 1);
  tzset();

  // Account for elapsed time (processing + sleep) of the previous cycle
  rtc_elapsedUs += rtc_priorCycleDurationUs;
  rtc_priorCycleDurationUs = 0;

  #if LOGGING_ENABLED
    Serial.begin(115200);
    while (!Serial) delay(10);
  #endif

  if (rtc_bootCount == 1) {
    delay(10000); // Wait for possible upload
  }

  uint32_t currentEpoch = getCurrentEpoch();

  struct tm timeinfo;
  epochToLocalTm(currentEpoch, timeinfo);
  int currentHour   = timeinfo.tm_hour;
  int currentMinute = timeinfo.tm_min;
  bool isPowerSaving = isPowerSavingPeriod(timeinfo);

  const uint32_t displayInterval = isPowerSaving ? 3600u : 600u; // 60 min power saving / 10 min active
  const uint32_t dataInterval    = isPowerSaving ? 1800u : 300u; // 30 min power saving / 5 min active
  const uint32_t weatherInterval = 3600u;                        // always 60 min
  const uint32_t ntpInterval     = 86400u;                       // 1 day
  const uint32_t EPSILON         = 10u;                          // 10 s tolerance for timer imprecision

  bool needsNtpSync       = (rtc_ntpBaseEpoch == 0) || (currentEpoch + EPSILON - rtc_ntpBaseEpoch >= ntpInterval);
  bool needsDisplayUpdate = (rtc_bootCount == 1) || (currentEpoch + EPSILON - rtc_lastDisplayUpdateEpoch >= displayInterval);
  bool needsWeather       = (rtc_bootCount == 1) || (needsDisplayUpdate && (!rtc_weatherDataValid || (currentEpoch + EPSILON - rtc_lastWeatherFetchEpoch >= weatherInterval)));
  bool needsDataSend      = (rtc_bootCount == 1) || (currentEpoch + EPSILON - rtc_lastDataSendEpoch >= dataInterval);
  // NTP piggybacks on data/weather WiFi — never wakes WiFi on its own
  bool needsWiFi         = needsWeather || needsDataSend;
  bool largeUpdate       = (rtc_bootCount == 1) || needsWeather;

  initSensors();
  if (needsDisplayUpdate) initDisplay1();
  if (needsWiFi) connectWiFi();

  readSensors();

  if (needsWiFi) waitForWiFi();

  if (needsNtpSync && WiFi.status() == WL_CONNECTED) {
    if (syncNTP()) {
      currentEpoch  = getCurrentEpoch();
      epochToLocalTm(currentEpoch, timeinfo);
      currentHour   = timeinfo.tm_hour;
      currentMinute = timeinfo.tm_min;
      isPowerSaving = isPowerSavingPeriod(timeinfo);
    }
  }

  if (needsWeather && WiFi.status() == WL_CONNECTED) {
    fetchWeatherForecast();
    rtc_lastWeatherFetchEpoch = currentEpoch;
  }

  if (needsDisplayUpdate) {
    initDisplay2();
    if (largeUpdate) {
      largeAntiGhosting(display);
    } else {
      smallAntiGhosting(display);
    }
    getMoonPhase(currentEpoch);
    snprintf(rtc_lastUpdateTimeStr, sizeof(rtc_lastUpdateTimeStr), "%02d:%02d", currentHour, currentMinute);
    updateDisplay(
      display,
      tempAir,
      humidity,
      co2,
      pressure,
      rtc_sunriseTimeStr,
      rtc_sunsetTimeStr,
      rtc_forecastTemp,
      rtc_forecastApparentTemp,
      rtc_forecastRain,
      FORECAST_HOURS,
      rtc_forecastStartHour,
      rtc_weatherDataValid,
      moonPhase,
      rtc_lastUpdateTimeStr
    );
    rtc_lastDisplayUpdateEpoch = currentEpoch;
  }

  if (needsDataSend && WiFi.status() == WL_CONNECTED) {
    sendToHomeAssistant(tempAir, tempESP, humidity, co2, pressure, batteryVoltage, tempSCD, humiditySCD);
    rtc_lastDataSendEpoch = currentEpoch;
  }

  if (needsWiFi) WiFi.disconnect(true);
  if (needsDisplayUpdate) turnOffDisplay();

  // Compute sleep duration until the next needed event
  const uint32_t nextDisplayInterval = isPowerSaving ? 3600u : 600u;
  const uint32_t nextDataInterval    = isPowerSaving ? 3600u : 300u;
  uint32_t nextDisplayWake = rtc_lastDisplayUpdateEpoch + nextDisplayInterval;
  uint32_t nextDataWake    = rtc_lastDataSendEpoch    + nextDataInterval;
  uint32_t nextWake        = min(nextDisplayWake, nextDataWake);

  int32_t sleepSeconds = (int32_t)(nextWake - currentEpoch);
  if (sleepSeconds < 30)   sleepSeconds = 30;
  if (sleepSeconds > 3600) sleepSeconds = 3600;

  uint64_t sleepUs = (uint64_t)sleepSeconds * 1000000ULL;
  rtc_priorCycleDurationUs = (uint64_t)millis() * 1000ULL + sleepUs;

  #if LOGGING_ENABLED
    Serial.println("Faking deep sleep for debug");
    Serial.flush();
    delay(sleepUs / 1000);
    ESP.restart();
  #endif

  esp_sleep_enable_timer_wakeup(sleepUs);
  esp_deep_sleep_start();
}

void loop() {
  // Not used, all logic in setup for deep sleep cycle
}
