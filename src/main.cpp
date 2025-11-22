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

GxEPD2_BW<GxEPD2_397_GDEM0397T81, GxEPD2_397_GDEM0397T81::HEIGHT> display(GxEPD2_397_GDEM0397T81(EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN));
Adafruit_AHTX0 aht;
SensirionI2CScd4x scd4x;

const unsigned long UPDATE_INTERVAL = 20000;
const int HISTORY_SIZE = 24;
unsigned long lastUpdate = 0;

float tempAir = 0, humidity = 0, tempESP = 0, co2 = 0, pressure = 1000;
float tempHistory[HISTORY_SIZE];
float humidHistory[HISTORY_SIZE];
float co2History[HISTORY_SIZE];
int historyIndex = 0;

float getDummyPressure() {
  return 1000.0;
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

void updateDisplay() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    
    int weatherY = 160;
    display.drawLine(0, weatherY, 480, weatherY, GxEPD_BLACK);
    
    int contentY = weatherY + 20;
    int rowHeight = 140;
    
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
  
  delay(5000);
  lastUpdate = millis() - UPDATE_INTERVAL;
}

void loop() {
  if (millis() - lastUpdate >= UPDATE_INTERVAL) {
    lastUpdate = millis();
    
    readSensors();
    updateDisplay();
    sendToThingSpeak();
  }
  
  delay(1000);
}
