#include "PinConfig.h"
#include "../include/config.h"
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_AHTX0.h>
#include <SensirionI2CScd4x.h>
#include <GxEPD2_BW.h>
#include <ArduinoJson.h>
#include "rendering.h"
#include <esp_sleep.h>

#define LOGGING_ENABLED false

const unsigned long UPDATE_INTERVAL_MS = 30 * 1000;
const unsigned long WEATHER_UPDATE_INTERVAL_MS = 3600 * 1000;
const int FORECAST_HOURS = 24;


GxEPD2_BW<GxEPD2_397_GDEM0397T81, GxEPD2_397_GDEM0397T81::HEIGHT> display(GxEPD2_397_GDEM0397T81(EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN));
Adafruit_AHTX0 aht;
SensirionI2CScd4x scd4x;

float tempAir = 0, humidity = 0, tempESP = 0, co2 = 0, pressure = 1000;

RTC_DATA_ATTR float rtc_forecastTemp[FORECAST_HOURS];
RTC_DATA_ATTR float rtc_forecastRain[FORECAST_HOURS];
RTC_DATA_ATTR char rtc_sunriseTimeStr[6] = "--:--";
RTC_DATA_ATTR char rtc_sunsetTimeStr[6] = "--:--";
RTC_DATA_ATTR int rtc_forecastStartHour = 0;
RTC_DATA_ATTR bool rtc_weatherDataValid = false;
RTC_DATA_ATTR uint32_t rtc_bootCount = 0;
RTC_DATA_ATTR uint32_t rtc_bootsFromLastForecastFetch = 0;

// ################################ Sensors ####################################

void initSensors() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  
  if (!aht.begin(&Wire)) {
    #if LOGGING_ENABLED
      Serial.println("AHT init fail");
    #endif
  }
  
  scd4x.begin(Wire);
  scd4x.stopPeriodicMeasurement();
  delay(500);
  scd4x.startPeriodicMeasurement();
}

float getDummyPressure() {
  return 1000.0;
}

void readSensors() {
  sensors_event_t hum, temp;
  aht.getEvent(&hum, &temp);
  tempAir = temp.temperature;
  humidity = hum.relative_humidity;
  
  tempESP = temperatureRead();
  
  bool dataReady = false;
  scd4x.getDataReadyFlag(dataReady);
  
  co2 = 0;
  if (dataReady) {
    uint16_t co2Raw;
    float _tempSCD, _humSCD;
    if (scd4x.readMeasurement(co2Raw, _tempSCD, _humSCD) == 0) {
      co2 = co2Raw;
    }
  }
  
  pressure = getDummyPressure();
}

// ############################### Internet ####################################

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
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
  http.begin("https://api.open-meteo.com/v1/forecast?latitude=50.06&longitude=14.419998&timezone=Europe%2FBerlin&forecast_days=1&hourly=temperature_2m,rain&daily=sunset,sunrise&forecast_hours="+String(FORECAST_HOURS));
  
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
    JsonArray timeArray = doc["hourly"]["time"];
    
    // Extract starting hour from first time entry (format: "2025-12-01T20:00")
    if (timeArray.size() > 0) {
      String firstTime = timeArray[0].as<String>();
      rtc_forecastStartHour = firstTime.substring(11, 13).toInt();
    }
    
    for (int i = 0; i < FORECAST_HOURS && i < tempArray.size(); i++) {
      rtc_forecastTemp[i] = tempArray[i];
      rtc_forecastRain[i] = rainArray[i];
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

  String url = "http://api.thingspeak.com/update?api_key=";
  url += THINGSPEAK_API_KEY;
  url += "&field1=" + String(tempAir, 2);
  url += "&field2=" + String(tempESP, 2);
  url += "&field3=" + String(humidity, 2);
  url += "&field4=" + String((int)co2);
  url += "&field5=" + String((int)pressure);
  
  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  http.end();
  
  #if LOGGING_ENABLED
    if (code > 0) Serial.println("Upload OK");
    else Serial.println("Upload failed");
  #endif
}

// ################################ Display ####################################

void initDisplay() {
  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, HIGH);
  delay(100);
  
  display.init(115200, true, 2, false);
  display.setRotation(2); // landscape
  display.setTextColor(GxEPD_BLACK);
}

void setup() {
  #if LOGGING_ENABLED
    Serial.begin(115200);
  #endif

  rtc_bootCount++;
  rtc_bootsFromLastForecastFetch++;

  initSensors();
  readSensors();

  connectWiFi();
  if (rtc_bootCount == 1 || (rtc_bootsFromLastForecastFetch * UPDATE_INTERVAL_MS) >= WEATHER_UPDATE_INTERVAL_MS) {
    fetchWeatherForecast();
    rtc_bootsFromLastForecastFetch = 0;
  }

  initDisplay();
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
    rtc_weatherDataValid
  );
  
  sendToThingSpeak();

  delay(100);

  #if LOGGING_ENABLED
    Serial.println("Going to deep sleep...");
  #endif

  unsigned long sleepTimeUs = max((UPDATE_INTERVAL_MS - millis()) * 1000ULL, 1000ULL);

  esp_sleep_enable_timer_wakeup(sleepTimeUs);
  esp_deep_sleep_start();
}

void loop() {
  // Not used, all logic in setup for deep sleep cycle
}
