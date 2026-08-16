#pragma once

// Copy this file to config.h and fill in your actual credentials
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* THINGSPEAK_API_KEY = "YOUR_THINGSPEAK_API_KEY";
const char* HA_WEBHOOK_URL = "http://homeassistant.local:8123/api/webhook/{WEBHOOK_ID}";

// How often the device wakes up to measure and upload (display redraw runs every FULL_UPDATE_SECONDS, see main.cpp).
static constexpr uint32_t CYCLE_SECONDS = 1800;  // 30 minutes
static constexpr uint32_t FULL_UPDATE_SECONDS = 3600;  // redraw the display every 60 minutes

// Forecast fetch grid: slots INTERVAL apart counted from ANCHOR, fetched at the first wake-up at or
// after each slot. DWD ICON-D2 produces a run every 3 h (00/03/06/09/12/15/18/21 UTC) and Open-Meteo
// is serving it ~2 h 20 m later, so 02:35 UTC gets each run about as fresh as it can be had; any other
// schedule re-downloads identical numbers. An anchor landing before publication is harmless, it just
// gets the previous run. UTC so the grid does not move with daylight saving.
static constexpr uint32_t WEATHER_FETCH_INTERVAL_SECONDS = 10800;  // every 3 h
static constexpr uint32_t WEATHER_FETCH_ANCHOR_UTC_SECONDS = 2 * 3600 + 35 * 60;  // 02:35 UTC

// FETCH is larger than DISPLAY so the graph can still start at the current hour when the cached
// forecast is a few hours old; below 27 it starts in the past late in a fetch interval.
static constexpr int FORECAST_FETCH_HOURS = 28;
static constexpr int FORECAST_DISPLAY_HOURS = 24;

// How often to re-sync the clock with NTP. main.cpp additionally measures the deep-sleep oscillator
// error against these syncs and pre-compensates, so this does not have to be frequent to stay accurate.
static constexpr uint32_t NTP_INTERVAL_SECONDS = 86400;  // 1 day

// POSIX TZ string (https://gnu.org/software/libc/manual/html_node/TZ-Variable.html); this one is Europe/Prague with automatic daylight saving time.
static const char* const TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";
