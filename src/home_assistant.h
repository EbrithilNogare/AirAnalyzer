#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "../include/config.h"

// Returns the HTTP status code, or a negative HTTPClient error. A negative result means the request
// never reached the server, which is the signal main.cpp uses to drop a stale cached IP lease.
int sendToHomeAssistant(float tempAir, float tempESP, float humidity, float co2, float pressure, float batteryVoltage, float tempSCD, float humiditySCD, int wifiRSSI) {
  if (WiFi.status() != WL_CONNECTED){
    return -1;
  }

  JsonDocument doc;
  doc["temperature"] = round(tempAir * 100) / 100.0;
  doc["temperature_esp"] = round(tempESP);
  doc["humidity"] = round(humidity * 100) / 100.0;
  doc["co2"] = round(co2);
  doc["pressure"] = round(pressure);
  doc["battery_voltage"] = round(batteryVoltage * 10000) / 10000.0;
  doc["temperature_scd"] = round(tempSCD * 100) / 100.0;
  doc["humidity_scd"] = round(humiditySCD * 100) / 100.0;
  doc["wifi_rssi"] = wifiRSSI;

  String jsonString;
  serializeJson(doc, jsonString);

HTTPClient http;
  http.begin(HA_WEBHOOK_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(1000);
  int code = http.POST(jsonString);
  http.end();
  return code;
}
