// Desktop preview driver: renders the real rendering.cpp output to an image.
// Build with tools/preview/build.sh — no ESP hardware needed.
#include "rendering.h"
#include "forecast_data.h" // live snapshot; refresh via fetch_forecast.sh

int main() {
  DisplayType display;
  // main.cpp uses setRotation(2) to compensate for the panel being mounted
  // upside-down; rotation 0 gives the same layout but upright for a human preview.
  display.setRotation(0);

  updateDisplay(display,
                21.4f,   // tempAir
                47.0f,   // humidity
                812.0f,  // co2
                1013.0f, // pressure
                FC_SUNRISE, FC_SUNSET,
                FC_TEMP, FC_APPARENT, FC_RAIN,
                FC_HOURS, FC_START_HOUR,
                true,    // weatherDataValid
                0.35f,   // moonPhase
                78);     // batteryPercent

  display.savePPM("tools/preview/out.ppm");
  printf("wrote tools/preview/out.ppm\n");
  return 0;
}
