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
#include <Fonts/FreeSans9pt7b.h>
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
  display.setRotation(3);
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

void drawForecastGraph(int x, int y, int w, int h, float* data, int dataSize, float minVal, float maxVal) {
  display.drawRect(x, y, w, h, GxEPD_BLACK);

  // Guard against degenerate range
  float range = maxVal - minVal;
  if (range <= 0.001) range = 1.0;

  // Draw horizontal 'grey' grid lines every 5 degrees (e.g. -5, 0, 5, 10...)
  // We approximate grey by drawing a dashed line (less ink than solid line).
  int firstLine = (int)ceil((minVal) / 5.0) * 5; // first multiple of 5 >= minVal
  for (int t = firstLine; t <= (int)maxVal; t += 5) {
    float val = (float)t;
    // compute y position for this temperature value
    int yy = y + h - (int)((val - minVal) / range * h);
    if (yy < y || yy > y + h) continue;
    // dashed horizontal: segments of 3 pixels on, 3 pixels off
    for (int xx = x + 1; xx < x + w - 1; xx += 6) {
      int segW = min(3, x + w - 1 - xx);
      display.drawLine(xx, yy, xx + segW - 1, yy, GxEPD_BLACK);
    }
  }

  // If 0 degrees is inside the range, draw a solid horizontal line for 0°C
  if (minVal <= 0.0 && maxVal >= 0.0) {
    int y0 = y + h - (int)((0.0 - minVal) / range * h);
    if (y0 >= y && y0 <= y + h) {
      display.drawLine(x + 1, y0, x + w - 2, y0, GxEPD_BLACK);
      display.drawLine(x + 1, y0 + 1, x + w - 2, y0 + 1, GxEPD_BLACK);
    }
  }

  // Draw vertical 'grey' grid lines every 2 hours (0,2,4...)
  // position at each hour index; dashed to appear lighter
  for (int i = 0; i < dataSize; i++) {
    if ((i % 2) != 0) continue; // only every 2 hours
    int xx = x + i * w / dataSize;
    if (xx < x || xx > x + w) continue;
    // dashed vertical: segments of 3 pixels on, 3 pixels off
    for (int yy = y + 2; yy < y + h - 2; yy += 6) {
      int segH = min(3, y + h - 2 - yy);
      display.drawLine(xx, yy, xx, yy + segH - 1, GxEPD_BLACK);
    }
  }

  // Solid vertical lines for midnight (hour 0) and midday (hour 12) if present
  if (dataSize > 0) {
    if (0 < dataSize) {
      int xx0 = x + 0 * w / dataSize;
      if (xx0 >= x && xx0 <= x + w) {
        display.drawLine(xx0, y + 1, xx0, y + h - 2, GxEPD_BLACK);
        display.drawLine(xx0 + 1, y + 1, xx0 + 1, y + h - 2, GxEPD_BLACK);
      }
    }
    if (12 < dataSize) {
      int xx12 = x + 12 * w / dataSize;
      if (xx12 >= x && xx12 <= x + w) {
        display.drawLine(xx12, y + 1, xx12, y + h - 2, GxEPD_BLACK);
        display.drawLine(xx12 + 1, y + 1, xx12 + 1, y + h - 2, GxEPD_BLACK);
      }
    }
  }

  // Draw the forecast polyline (solid, slightly thickened)
  for (int i = 1; i < dataSize; i++) {
    int x1 = x + (i - 1) * w / dataSize;
    int y1 = y + h - (int)((data[i-1] - minVal) / range * h);
    int x2 = x + i * w / dataSize;
    int y2 = y + h - (int)((data[i] - minVal) / range * h);
    display.drawLine(x1, y1 + 2, x2, y2 + 2, GxEPD_BLACK);
    display.drawLine(x1, y1 + 1, x2, y2 + 1, GxEPD_BLACK);
    display.drawLine(x1, y1    , x2, y2    , GxEPD_BLACK);
    display.drawLine(x1, y1 - 1, x2, y2 - 1, GxEPD_BLACK);
    display.drawLine(x1, y1 - 2, x2, y2 - 2, GxEPD_BLACK);
  }
}

void drawRainColumns(int x, int y, int w, int h, float* data, int dataSize, float maxVal) {
  display.drawRect(x, y, w, h, GxEPD_BLACK);
  
  int colWidth = w / dataSize;
  for (int i = 0; i < dataSize; i++) {
    if (data[i] > 0) {
      int colHeight = (data[i] / maxVal) * h;
      if (colHeight > 0) {
        int x1 = x + i * colWidth;
        int y1 = y + h - colHeight;
        display.fillRect(x1, y1, colWidth - 1, colHeight, GxEPD_BLACK);
      }
    }
  }
}

void drawWeatherForecast() {
  if (!weatherDataValid) return;
  
  int weatherY = 10;
  int graphHeight = 180;  // Tripled from 60
  int graphWidth = 480;   // Full width
  
  // Find min/max for temperature
  float minTemp = forecastTemp[0], maxTemp = forecastTemp[0];
  float maxRain = 0;
  for (int i = 0; i < FORECAST_HOURS; i++) {
    if (forecastTemp[i] < minTemp) minTemp = forecastTemp[i];
    if (forecastTemp[i] > maxTemp) maxTemp = forecastTemp[i];
    if (forecastRain[i] > maxRain) maxRain = forecastRain[i];
  }
  
  // Add some padding to ranges
  minTemp = floor(minTemp - 1);
  maxTemp = ceil(maxTemp + 1);
  if (maxRain < 0.5) maxRain = 0.5;
  
  // Draw sunrise/sunset info
  display.setFont(&FreeSans9pt7b);
  display.setCursor(10, weatherY + 15);
  display.print("Sunrise: ");
  display.print(sunriseTime);
  display.setCursor(240, weatherY + 15);
  display.print("Sunset: ");
  display.print(sunsetTime);
  
  // Draw temperature forecast graph
  display.setCursor(10, weatherY + 45);
  display.print("Temp");
  drawForecastGraph(10, weatherY + 50, graphWidth, graphHeight, forecastTemp, FORECAST_HOURS, minTemp, maxTemp);
  
  // Draw labels on right side
  display.setCursor(420, weatherY + 60);
  display.print(String((int)maxTemp) + "C");
  display.setCursor(420, weatherY + 220);
  display.print(String((int)minTemp) + "C");
  
  // Draw rain forecast columns (1/3 height)
  int rainHeight = 60;
  display.setCursor(10, weatherY + 255);
  display.print("Rain");
  drawRainColumns(10, weatherY + 260, graphWidth, rainHeight, forecastRain, FORECAST_HOURS, maxRain);
  
  if (maxRain > 0) {
    display.setCursor(420, weatherY + 265);
    display.print(String(maxRain, 1) + "mm");
  }
}

void updateDisplay() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    
    // Draw weather forecast at top
    drawWeatherForecast();
    
    int weatherY = 340;
    display.drawLine(0, weatherY, 480, weatherY, GxEPD_BLACK);
    
    int contentY = weatherY + 20;
    int rowHeight = 100;
    
    display.setFont(&FreeSansBold18pt7b);
    display.setCursor(20, contentY + 40);
    display.print("Temp: ");
    display.print(tempAir, 1);
    display.print("C");
    
    drawGraph(300, contentY + 10, 160, 60, tempHistory, 15, 30);
    
    display.setCursor(20, contentY + rowHeight + 40);
    display.print("Hum: ");
    display.print(humidity, 1);
    display.print("%");
    
    drawGraph(300, contentY + rowHeight + 10, 160, 60, humidHistory, 20, 80);
    
    display.setCursor(20, contentY + 2*rowHeight + 40);
    display.print("CO2: ");
    display.print((int)co2);
    display.print("ppm");
    
    drawGraph(300, contentY + 2*rowHeight + 10, 160, 60, co2History, 400, 2000);
    
    display.setFont(&FreeSans9pt7b);
    display.setCursor(20, 780);
    display.print("Pressure: ");
    display.print((int)pressure);
    display.print(" hPa | ESP: ");
    display.print(tempESP, 1);
    display.print("C");
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
