#ifndef HOME_ASSISTANT_H
#define HOME_ASSISTANT_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "../include/config.h"

#define LOGGING_ENABLED false

void sendToHomeAssistant(float tempAir, float tempESP, float humidity, float co2, float pressure, float batteryVoltage) {
  if (WiFi.status() != WL_CONNECTED){
    #if LOGGING_ENABLED
      Serial.println("WiFi not connected, skipping Home Assistant upload");
    #endif
    return;
  }

  JsonDocument doc;
  doc["temperature"] = round(tempAir * 100) / 100.0;
  doc["temperature_esp"] = round(tempESP);
  doc["humidity"] = round(humidity * 100) / 100.0;
  doc["co2"] = round(co2);
  doc["pressure"] = round(pressure);
  doc["battery_voltage"] = round(batteryVoltage * 10000) / 10000.0;

  String jsonString;
  serializeJson(doc, jsonString);

  HTTPClient http;
  http.begin(HA_WEBHOOK_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(200);
  http.POST(jsonString);
  http.end();
}

#endif // HOME_ASSISTANT_H
