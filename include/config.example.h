#pragma once

// Copy this file to config.h and fill in your actual credentials
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* THINGSPEAK_API_KEY = "YOUR_THINGSPEAK_API_KEY";
const char* HA_WEBHOOK_URL = "http://homeassistant.local:8123/api/webhook/{WEBHOOK_ID}";

// How often the device wakes up to measure and upload (display redraw runs every FULL_UPDATE_SECONDS, see main.cpp).
static constexpr uint32_t CYCLE_SECONDS = 1800;  // 30 minutes
static constexpr uint32_t FULL_UPDATE_SECONDS = 3600;  // redraw the display every 60 minutes

// When to fetch the forecast. Slots are WEATHER_FETCH_INTERVAL_SECONDS apart, counted from
// WEATHER_FETCH_ANCHOR_UTC_SECONDS (seconds past midnight UTC); the fetch runs at the first wake-up
// at or after each slot. The anchor is UTC on purpose - it must not move with daylight saving.
//
// The default grid is 02:30 / 05:30 / 08:30 ... UTC. It is picked to sit just after the forecast is
// published: the source is DWD ICON-D2, which produces a run every 3 h (00/03/06/09/12/15/18/21 UTC)
// and finishes disseminating each one roughly 2 h 15 m later, and Open-Meteo asks callers to wait a
// little past that. The extra ~15 min is headroom for clock drift between NTP syncs. Fetching on any
// other schedule just re-downloads identical numbers.
//
// For a 00:06 / 03:06 / 06:06 ... grid instead, set the anchor to 0 * 3600 + 6 * 60. Any anchor works;
// one that lands before publication simply gets the previous run, i.e. data up to 3 h older.
static constexpr uint32_t WEATHER_FETCH_INTERVAL_SECONDS = 10800;  // every 3 h
static constexpr uint32_t WEATHER_FETCH_ANCHOR_UTC_SECONDS = 2 * 3600 + 30 * 60;  // 02:30 UTC

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

// How often to re-sync the clock with NTP. The deep-sleep timer runs off the internal 150 kHz RC
// oscillator, which is only good to a couple of percent, so the wake-up grid slides against real time
// between syncs. Syncing every 3 h keeps that under a minute and costs almost nothing, because the
// radio is already associated on those cycles anyway. main.cpp additionally measures the oscillator
// error at each sync and pre-compensates the following sleeps.
static constexpr uint32_t NTP_INTERVAL_SECONDS = 10800;  // 3 hours

// POSIX TZ string (https://gnu.org/software/libc/manual/html_node/TZ-Variable.html); this one is Europe/Prague with automatic daylight saving time.
static const char* const TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";
