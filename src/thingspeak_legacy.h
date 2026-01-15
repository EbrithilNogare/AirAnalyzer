#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "../include/config.h"

#define LOGGING_ENABLED false

// Original ThingSpeak upload function
// Data mapping:
// field1 = tempAir (temperature from AHT sensor)
// field2 = tempESP (ESP32 internal temperature)
// field3 = humidity (from AHT sensor)
// field4 = co2 (from SCD40 sensor)
// field5 = pressure (from BMP280 sensor)
// field6 = batteryVoltage
void sendToThingSpeak(float tempAir, float tempESP, float humidity, float co2, float pressure, float batteryVoltage) {
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
