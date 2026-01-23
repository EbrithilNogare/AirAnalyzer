#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "../include/config.h"

void sendToHomeAssistant(float tempAir, float tempESP, float humidity, float co2, float pressure, float batteryVoltage, float tempSCD, float humiditySCD) {
  if (WiFi.status() != WL_CONNECTED){
    return;
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

  String jsonString;
  serializeJson(doc, jsonString);

HTTPClient http;
  http.begin(HA_WEBHOOK_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(1000);
  http.POST(jsonString);
  http.end();
}
