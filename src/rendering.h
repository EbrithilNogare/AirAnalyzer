#pragma once
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Arduino.h>

void updateDisplay(GxEPD2_BW<GxEPD2_397_GDEM0397T81, GxEPD2_397_GDEM0397T81::HEIGHT>& display,
                   float tempAir, float humidity, float co2, float pressure,
                   const String& sunriseTime, const String& sunsetTime,
                   const float* forecastTemp, const float* forecastRain, int forecastHours,
                   bool weatherDataValid);

void drawWeatherForecast(GxEPD2_BW<GxEPD2_397_GDEM0397T81, GxEPD2_397_GDEM0397T81::HEIGHT>& display,
                        const float* forecastTemp, const float* forecastRain, int forecastHours,
                        const String& sunriseTime, const String& sunsetTime, bool weatherDataValid);

void drawForecastGraph(GxEPD2_BW<GxEPD2_397_GDEM0397T81, GxEPD2_397_GDEM0397T81::HEIGHT>& display,
                      int x, int y, int w, int h, const float* data, int dataSize, float minVal, float maxVal);

void drawRainColumns(GxEPD2_BW<GxEPD2_397_GDEM0397T81, GxEPD2_397_GDEM0397T81::HEIGHT>& display,
                    int x, int y, int w, int h, const float* data, int dataSize, float maxVal);
