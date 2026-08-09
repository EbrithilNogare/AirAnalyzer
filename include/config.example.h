#pragma once

// Copy this file to config.h and fill in your actual credentials
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* THINGSPEAK_API_KEY = "YOUR_THINGSPEAK_API_KEY";
const char* HA_WEBHOOK_URL = "http://homeassistant.local:8123/api/webhook/{WEBHOOK_ID}";

// How often the device wakes up to measure and upload (display redraw runs every FULL_UPDATE_SECONDS, see main.cpp).
static constexpr uint32_t CYCLE_SECONDS = 1800;  // 30 minutes
static constexpr uint32_t FULL_UPDATE_SECONDS = 3600;  // redraw the display every 60 minutes

// The forecast comes from DWD ICON-D2, which publishes a new run every 3 h (00/03/06/09/12/15/18/21 UTC).
// Fetching on any other schedule just re-downloads identical numbers, so the device waits for a run to land.
static constexpr uint32_t WEATHER_RUN_INTERVAL_SECONDS = 10800;  // 3 h between ICON-D2 runs
static constexpr uint32_t WEATHER_RUN_AVAILABILITY_SECONDS = 8100;  // DWD finishes disseminating a run ~2 h 15 m after its reference time
static constexpr uint32_t WEATHER_FETCH_MARGIN_SECONDS = 300;  // Open-Meteo asks callers to wait a bit past that before requesting

// Hours of hourly forecast to request. Only FORECAST_DISPLAY_HOURS are drawn; the rest is slack so the
// graph can still start at the *current* hour when the newest run is already a few hours old.
// A run lands every 3 h and wake-ups sit on a 30 min grid, so cached data can be ~3.5 h old at redraw
// time -> 24 + 4. Lowering this below 28 makes the graph start in the past during the last hour of a run.
static constexpr int FORECAST_FETCH_HOURS = 28;
static constexpr int FORECAST_DISPLAY_HOURS = 24;

// The full black/white flash that clears e-ink ghosting costs an entire extra panel refresh, so it runs
// only every Nth redraw; the redraws in between are differential updates. Set to 1 to restore the old
// behaviour if ghosting builds up on your panel.
static constexpr uint32_t ANTI_GHOSTING_EVERY_N_UPDATES = 6;

// How often to re-sync the clock with NTP.
static constexpr uint32_t NTP_INTERVAL_SECONDS = 86400;  // 1 day

// POSIX TZ string (https://gnu.org/software/libc/manual/html_node/TZ-Variable.html); this one is Europe/Prague with automatic daylight saving time.
static const char* const TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";
