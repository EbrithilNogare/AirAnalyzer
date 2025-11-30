#include "PinConfig.h"
#include "../include/config.h"
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_AHTX0.h>
#include <SensirionI2CScd4x.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <ArduinoJson.h>

GxEPD2_BW<GxEPD2_397_GDEM0397T81, GxEPD2_397_GDEM0397T81::HEIGHT> display(GxEPD2_397_GDEM0397T81(EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN));
Adafruit_AHTX0 aht;
SensirionI2CScd4x scd4x;

const unsigned long UPDATE_INTERVAL = 120000;
const int HISTORY_SIZE = 24;
const int FORECAST_HOURS = 24;
unsigned long lastUpdate = 0;
unsigned long lastWeatherUpdate = 0;
const unsigned long WEATHER_UPDATE_INTERVAL = 3600000; // 1 hour

float tempAir = 0, humidity = 0, tempESP = 0, co2 = 0, pressure = 1000;
float tempHistory[HISTORY_SIZE];
float humidHistory[HISTORY_SIZE];
float co2History[HISTORY_SIZE];
int historyIndex = 0;

// Weather forecast data
float forecastTemp[FORECAST_HOURS];
float forecastRain[FORECAST_HOURS];
String sunriseTime = "--:--";
String sunsetTime = "--:--";
bool weatherDataValid = false;

float getDummyPressure() {
  return 1000.0;
}

void fetchWeatherForecast() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping weather update");
    return;
  }
  
  Serial.println("Fetching weather forecast...");
  HTTPClient http;
  http.begin("https://api.open-meteo.com/v1/forecast?latitude=50.0440426&longitude=14.4220692&timezone=Europe%2FBerlin&forecast_days=1&hourly=temperature_2m,rain&daily=sunset,sunrise&wind_speed_unit=ms&forecast_hours=24");
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
      http.end();
      return;
    }
    
    // Parse hourly temperature and rain
    JsonArray tempArray = doc["hourly"]["temperature_2m"];
    JsonArray rainArray = doc["hourly"]["rain"];
    
    for (int i = 0; i < FORECAST_HOURS && i < tempArray.size(); i++) {
      forecastTemp[i] = tempArray[i];
      forecastRain[i] = rainArray[i];
    }
    
    // Parse sunrise and sunset times
    if (doc["daily"]["sunrise"][0]) {
      String sunriseStr = doc["daily"]["sunrise"][0].as<String>();
      sunriseTime = sunriseStr.substring(11, 16); // Extract HH:MM
    }
    
    if (doc["daily"]["sunset"][0]) {
      String sunsetStr = doc["daily"]["sunset"][0].as<String>();
      sunsetTime = sunsetStr.substring(11, 16); // Extract HH:MM
    }
    
    weatherDataValid = true;
    Serial.println("Weather data updated successfully");
    Serial.print("Sunrise: ");
    Serial.print(sunriseTime);
    Serial.print(" Sunset: ");
    Serial.println(sunsetTime);
  } else {
    Serial.print("HTTP error: ");
    Serial.println(httpCode);
  }
  
  http.end();
}

void initSensors() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  
  if (!aht.begin(&Wire)) {
    Serial.println("AHT init fail");
  }
  
  scd4x.begin(Wire);
  scd4x.stopPeriodicMeasurement();
  delay(500);
  scd4x.startPeriodicMeasurement();
}

void initDisplay() {
  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, HIGH);
  delay(100);
  
  display.init(115200, true, 2, false);
  // rotate by 90 degrees to landscape
  display.setRotation(2);
  display.setTextColor(GxEPD_BLACK);
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi OK");
  }
}

void readSensors() {
  sensors_event_t hum, temp;
  aht.getEvent(&hum, &temp);
  tempAir = temp.temperature;
  humidity = hum.relative_humidity;
  
  tempESP = temperatureRead();
  
  uint16_t co2Raw;
  float tempSCD, humSCD;
  if (scd4x.readMeasurement(co2Raw, tempSCD, humSCD) == 0) {
    co2 = co2Raw;
  }
  
  pressure = getDummyPressure();
  
  tempHistory[historyIndex] = tempAir;
  humidHistory[historyIndex] = humidity;
  co2History[historyIndex] = co2;
  historyIndex = (historyIndex + 1) % 24;
}

void drawGraph(int x, int y, int w, int h, float* data, float minVal, float maxVal) {
  display.drawRect(x, y, w, h, GxEPD_BLACK);
  
  for (int i = 1; i < HISTORY_SIZE; i++) {
    if (data[i] > 0 && data[i-1] > 0) {
      int x1 = x + (i - 1) * w / HISTORY_SIZE;
      int y1 = y + h - (data[i-1] - minVal) / (maxVal - minVal) * h;
      int x2 = x + i * w / HISTORY_SIZE;
      int y2 = y + h - (data[i] - minVal) / (maxVal - minVal) * h;
      display.drawLine(x1, y1, x2, y2, GxEPD_BLACK);
    }
  }
}
void drawDashedHLine(int x1, int x2, int y, int onLen=3, int offLen=3) {
  for (int xx = x1; xx <= x2; xx += onLen + offLen) {
    int segW = min(onLen, x2 - xx + 1);
    if (segW > 0) display.drawLine(xx, y, xx + segW - 1, y, GxEPD_BLACK);
  }
}

void drawDashedVLine(int y1, int y2, int x, int onLen=3, int offLen=3) {
  for (int yy = y1; yy <= y2; yy += onLen + offLen) {
    int segH = min(onLen, y2 - yy + 1);
    if (segH > 0) display.drawLine(x, yy, x, yy + segH - 1, GxEPD_BLACK);
  }
}

void drawForecastGraph(int x, int y, int w, int h, float* data, int dataSize, float minVal, float maxVal) {
  display.drawRect(x, y, w, h, GxEPD_BLACK);

  // Guard against degenerate range
  float range = maxVal - minVal;
  if (range <= 0.001) range = 1.0;

  // Horizontal lines every 10 degrees (dashed), solid for 0°C
  int firstLine = (int)floor(minVal / 10.0) * 10;
  for (int t = firstLine; t <= (int)ceil(maxVal); t += 10) {
    float val = (float)t;
    int yy = y + h - (int)((val - minVal) / range * h);
    if (yy < y || yy > y + h) continue;
    if (t == 0) {
      display.drawLine(x + 1, yy, x + w - 2, yy, GxEPD_BLACK);
    } else {
      drawDashedHLine(x + 1, x + w - 2, yy);
    }
  }

  // Vertical dashed lines for every even hour, solid for 12 and right edge
  for (int i = 0; i <= dataSize; i++) {
    if (i == dataSize) {
      // rightmost full line represents 24h
      int xx = x + w;
      display.drawLine(xx, y + 1, xx, y + h - 1, GxEPD_BLACK);
      continue;
    }
    int xx = x + i * w / dataSize;
    if ((i % 2) == 0) {
      if (i == 12) {
        display.drawLine(xx, y + 1, xx, y + h - 1, GxEPD_BLACK);
      } else {
        drawDashedVLine(y + 2, y + h - 2, xx);
      }
    }
  }

  // Draw temperature polyline thick (5px)
  for (int i = 1; i < dataSize; i++) {
    int x1 = x + (i - 1) * w / dataSize;
    int y1 = y + h - (int)((data[i-1] - minVal) / range * h);
    int x2 = x + i * w / dataSize;
    int y2 = y + h - (int)((data[i] - minVal) / range * h);
    for (int off = -2; off <= 2; off++) {
      display.drawLine(x1, y1 + off, x2, y2 + off, GxEPD_BLACK);
    }
  }
}

void drawRainColumns(int x, int y, int w, int h, float* data, int dataSize, float maxVal) {
  display.drawRect(x, y, w, h, GxEPD_BLACK);

  int colWidth = max(1, w / dataSize);
  for (int i = 0; i < dataSize; i++) {
    float v = data[i];
    if (v <= 0) continue;
    int colHeight = (int)((v / maxVal) * h);
    if (colHeight < 1) colHeight = 1;
    int x1 = x + i * colWidth;
    int y1 = y; // start at top and grow downward for "upside down"
    display.fillRect(x1, y1, colWidth - 1, colHeight, GxEPD_BLACK);
  }
}

void drawWeatherForecast() {
  if (!weatherDataValid) return;
  int screenW = display.width();
  int screenH = display.height();

  int weatherY = 8;
  int graphWidth = (screenW * 90) / 100; // 90% of width
  int graphX = (screenW - graphWidth) / 2;
  int graphHeight = (screenH * 50) / 100; // 50% of height

  // Find min/max for temperature
  float minTemp = forecastTemp[0], maxTemp = forecastTemp[0];
  float maxRain = 0;
  for (int i = 0; i < FORECAST_HOURS; i++) {
    if (forecastTemp[i] < minTemp) minTemp = forecastTemp[i];
    if (forecastTemp[i] > maxTemp) maxTemp = forecastTemp[i];
    if (forecastRain[i] > maxRain) maxRain = forecastRain[i];
  }
  
  // Add some padding to ranges and round to whole numbers for side labels
  minTemp = floor(minTemp);
  maxTemp = ceil(maxTemp);
  if (minTemp == maxTemp) { minTemp -= 1; maxTemp += 1; }
  if (maxRain < 1.0) maxRain = 1.0; // minimum scale 1mm

  // Top row: hour labels (only even hours)
  display.setFont(&FreeSans18pt7b);
  int hourY = weatherY + 18; // top small margin
  for (int i = 0; i < FORECAST_HOURS; i++) {
    if ((i % 2) != 0) continue; // only even hours
    int xx = graphX + i * graphWidth / FORECAST_HOURS;
    String hlabel = String(i);
    int16_t tbx, tby; uint16_t tbw, tbh;
    display.getTextBounds(hlabel, xx, hourY, &tbx, &tby, &tbw, &tbh);
    display.setCursor(xx - tbw/2, hourY);
    display.print(hlabel);
  }

  // Temperature graph (takes 50% height)
  int tempGraphY = weatherY + 28;
  drawForecastGraph(graphX, tempGraphY, graphWidth, graphHeight, forecastTemp, FORECAST_HOURS, minTemp, maxTemp);

  // Right side max/min labels (whole numbers)
  display.setFont(&FreeSansBold18pt7b);
  int labelX = graphX + graphWidth + 6;
  display.setCursor(labelX, tempGraphY + 18);
  display.print(String((int)maxTemp) + "C");
  display.setCursor(labelX, tempGraphY + graphHeight - 4);
  display.print(String((int)minTemp) + "C");

  // Rain area: immediately below temperature graph, height = 1/3 of tempGraphHeight
  int rainHeight = max(12, graphHeight / 3);
  int rainY = tempGraphY + graphHeight; // seamless, no spacing
  drawRainColumns(graphX, rainY, graphWidth, rainHeight, forecastRain, FORECAST_HOURS, maxRain);

  // Right side max rain label
  display.setFont(&FreeSans18pt7b);
  display.setCursor(labelX, rainY + 18);
  display.print(String((int)ceil(maxRain)) + "mm");
}

void updateDisplay() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    // Draw weather forecast at top (large section)
    drawWeatherForecast();

    // Bottom area: icons placeholders (bottom 1/5 of screen)
    int screenW = display.width();
    int screenH = display.height();
    int bottomH = max(64, screenH / 5);
    int bottomY = screenH - bottomH - 6;

    // Draw 6 icon placeholders (64x64) evenly spaced, values under each
    int icons = 6;
    int iconSize = 64;
    int gap = (screenW - icons * iconSize) / (icons + 1);
    int iconY = bottomY + 2;
    display.setFont(&FreeSans18pt7b);
    for (int i = 0; i < icons; i++) {
      int ix = gap + i * (iconSize + gap);
      display.drawRect(ix, iconY, iconSize, iconSize, GxEPD_BLACK);
      // value under icon
      int valY = iconY + iconSize + 20;
      String v;
      switch (i) {
        case 0: v = String(tempAir, 1) + "C"; break;
        case 1: v = String((int)co2); break;
        case 2: v = sunriseTime; break;
        case 3: v = sunsetTime; break;
        case 4: v = String((int)humidity) + "%"; break;
        case 5: v = String((int)pressure); break;
      }
      int16_t tbx, tby; uint16_t tbw, tbh;
      display.getTextBounds(v, ix + iconSize/2, valY, &tbx, &tby, &tbw, &tbh);
      display.setCursor(ix + iconSize/2 - tbw/2, valY + tbh/2);
      display.print(v);
    }
  } while (display.nextPage());
}

void sendToThingSpeak() {
  if (WiFi.status() != WL_CONNECTED) return;
  
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
  
  if (code > 0) Serial.println("Upload OK");
}

void setup() {
  Serial.begin(115200);
  
  for (int i = 0; i < HISTORY_SIZE; i++) {
    tempHistory[i] = 0;
    humidHistory[i] = 0;
    co2History[i] = 0;
  }
  
  initDisplay();
  initSensors();
  connectWiFi();
  
  // Initialize weather forecast arrays
  for (int i = 0; i < FORECAST_HOURS; i++) {
    forecastTemp[i] = 0;
    forecastRain[i] = 0;
  }
  
  // Fetch weather forecast on startup
  if (WiFi.status() == WL_CONNECTED) {
    fetchWeatherForecast();
  }
  
  delay(5000);
  lastUpdate = millis() - UPDATE_INTERVAL;
  lastWeatherUpdate = millis();
}

void loop() {
  // Update weather forecast every hour
  if (millis() - lastWeatherUpdate >= WEATHER_UPDATE_INTERVAL) {
    lastWeatherUpdate = millis();
    fetchWeatherForecast();
  }
  
  if (millis() - lastUpdate >= UPDATE_INTERVAL) {
    lastUpdate = millis();
    
    readSensors();
    updateDisplay();
    sendToThingSpeak();
  }
  
  delay(1000);
}
