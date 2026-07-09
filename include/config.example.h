#pragma once

// Copy this file to config.h and fill in your actual credentials
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* THINGSPEAK_API_KEY = "YOUR_THINGSPEAK_API_KEY";
const char* HA_WEBHOOK_URL = "http://homeassistant.local:8123/api/webhook/{WEBHOOK_ID}";

// How often the device wakes up to measure and upload (weather fetch + display redraw run every FULL_UPDATE_SECONDS, see main.cpp).
static constexpr uint32_t CYCLE_SECONDS = 1800;  // 30 minutes
static constexpr uint32_t FULL_UPDATE_SECONDS = 3600;  // fetch weather + redraw display every 60 minutes

// How often to re-sync the clock with NTP.
static constexpr uint32_t NTP_INTERVAL_SECONDS = 86400;  // 1 day

// POSIX TZ string (https://gnu.org/software/libc/manual/html_node/TZ-Variable.html); this one is Europe/Prague with automatic daylight saving time.
static const char* const TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";
