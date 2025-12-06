#pragma once
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Arduino.h>

typedef GxEPD2_BW<GxEPD2_397_GDEM0397T81, GxEPD2_397_GDEM0397T81::HEIGHT> DisplayType;

void updateDisplay(DisplayType& display, float tempAir, float humidity, float co2, float pressure, const String& sunriseTime, const String& sunsetTime, const float* forecastTemp, const float* forecastRain, int forecastHours, int forecastStartHour, bool weatherDataValid, float moonPhase);

void drawWeatherForecast(DisplayType& display, const float* forecastTemp, const float* forecastRain, int forecastHours, int forecastStartHour, const String& sunriseTime, const String& sunsetTime, bool weatherDataValid);

void drawForecastGraph(DisplayType& display, int x, int y, int w, int h, const float* data, int dataSize, float minVal, float maxVal);

void drawRainColumns(DisplayType& display, int x, int y, int w, int h, const float* data, int dataSize, float maxVal);
