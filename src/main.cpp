#include "PinConfig.h"
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_BMP280.h>
#include <SensirionI2CScd4x.h>
#include <GxEPD2_BW.h>
#include <ArduinoJson.h>
#include <esp_sleep.h>
#include <time.h>

#include "../include/config.h"
#include "rendering.h"
#include "home_assistant.h"



DisplayType display(GxEPD2_397_GDEM0397T81(EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN));
Adafruit_SHT4x sht4;
Adafruit_BMP280 bmp;
SensirionI2cScd4x scd4x;

float tempAir = NAN, humidity = NAN, tempESP = NAN, pressure = NAN, batteryVoltage = NAN, co2 = NAN, moonPhase = 0;
float tempSCD = NAN, humiditySCD = NAN;
int wifiRSSI = 0;


RTC_DATA_ATTR float rtc_forecastTemp[FORECAST_FETCH_HOURS];
RTC_DATA_ATTR float rtc_forecastApparentTemp[FORECAST_FETCH_HOURS];
RTC_DATA_ATTR float rtc_forecastRain[FORECAST_FETCH_HOURS];
RTC_DATA_ATTR char rtc_sunriseTimeStr[6] = "--:--";
RTC_DATA_ATTR char rtc_sunsetTimeStr[6] = "--:--";
RTC_DATA_ATTR int rtc_forecastStartHour = 0;
RTC_DATA_ATTR int rtc_forecastValidHours = 0;      // how many hours the last fetch actually returned
RTC_DATA_ATTR uint32_t rtc_forecastStartEpoch = 0; // epoch of forecast index 0, used to re-align the graph on the current hour
RTC_DATA_ATTR uint32_t rtc_lastWeatherRunEpoch = 0;  // reference time of the ICON-D2 run already fetched
RTC_DATA_ATTR bool rtc_weatherDataValid = false;
RTC_DATA_ATTR uint32_t rtc_bootCount = 0;
RTC_DATA_ATTR uint32_t rtc_ntpBaseEpoch = 0;            // Unix epoch at last NTP sync
RTC_DATA_ATTR uint64_t rtc_elapsedUs = 0;               // Microseconds elapsed since NTP sync
RTC_DATA_ATTR uint64_t rtc_priorCycleDurationUs = 0;    // Previous cycle total duration (processing + sleep)
RTC_DATA_ATTR int rtc_batteryPercent = 0;  // battery percentage shown on display
RTC_DATA_ATTR uint32_t rtc_lastFullUpdateEpoch = 0;  // Epoch of last display refresh
RTC_DATA_ATTR uint32_t rtc_displayUpdateCount = 0;   // drives the anti-ghosting flash cadence

// Last known-good association, replayed on the next wake to skip the channel scan and DHCP
RTC_DATA_ATTR uint8_t rtc_wifiBssid[6] = {0};
RTC_DATA_ATTR uint8_t rtc_wifiChannel = 0;
RTC_DATA_ATTR uint32_t rtc_wifiIp = 0;
RTC_DATA_ATTR uint32_t rtc_wifiGateway = 0;
RTC_DATA_ATTR uint32_t rtc_wifiSubnet = 0;
RTC_DATA_ATTR uint32_t rtc_wifiDns = 0;
RTC_DATA_ATTR bool rtc_wifiCacheValid = false;
RTC_DATA_ATTR uint8_t rtc_netFailStreak = 0;  // consecutive cycles where a POST never reached the server

// ################################ Moon Phase #################################

// Returns moon phase as percentage: 0.0 = new moon, 0.5 = full moon, 1.0 = new moon
void getMoonPhase(uint32_t epochTime) {
  const uint32_t FULL_MOON_REF = 1763614318;
  const uint32_t LUNAR_CYCLE = 2551443; // 29.53 days in seconds
  uint32_t elapsed = epochTime - FULL_MOON_REF;
  moonPhase = (float)(elapsed % LUNAR_CYCLE) / (float)LUNAR_CYCLE;
}

// ################################ Sensors ####################################

bool shtOk = false, bmpOk = false;

// Sleep the CPU while a sensor works; radios are still off at this point in the cycle so light sleep is safe
void lightSleepMs(uint32_t ms) {
  esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
  esp_light_sleep_start();
}

bool i2cDeviceResponds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

// A slave interrupted mid-transfer (brownout, reset) can hold SDA low forever; no ESP reset fixes that, only clocking the stale bits out
void recoverI2CBus() {
  Wire.end();
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  pinMode(I2C_SCL_PIN, OUTPUT_OPEN_DRAIN);
  digitalWrite(I2C_SCL_PIN, HIGH);
  for (int i = 0; i < 9 && digitalRead(I2C_SDA_PIN) == LOW; i++) {
    digitalWrite(I2C_SCL_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(I2C_SCL_PIN, HIGH);
    delayMicroseconds(5);
  }
  pinMode(I2C_SDA_PIN, OUTPUT_OPEN_DRAIN);
  digitalWrite(I2C_SDA_PIN, LOW);
  delayMicroseconds(5);
  digitalWrite(I2C_SDA_PIN, HIGH);  // STOP condition (SDA low->high while SCL high) releases the bus
  delayMicroseconds(5);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);
  delay(2);
}

void initSensors() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);
  delay(2);

  if (!i2cDeviceResponds(SHT4x_DEFAULT_ADDR)) recoverI2CBus();

  shtOk = sht4.begin(&Wire);
  if (!shtOk) {
    recoverI2CBus();
    shtOk = sht4.begin(&Wire);
  }
  if (shtOk) {
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);
  }

  bmpOk = bmp.begin(BMP280_ADDRESS_ALT);

  scd4x.begin(Wire, SCD40_I2C_ADDR_62);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(POWER_SENSING_PIN, INPUT);
}

// The SCD40 keeps its periodic mode across ESP resets and start-while-measuring is a forbidden command, so always stop first (sensor responds again 500 ms after stop)
bool startSCD() {
  scd4x.stopPeriodicMeasurement();
  lightSleepMs(500);

  if (scd4x.startPeriodicMeasurement() == 0) return true;

  scd4x.reinit();  // self-heal: reload sensor settings and retry once
  lightSleepMs(30);
  return scd4x.startPeriodicMeasurement() == 0;
}

// First sample arrives after the ~5 s signal update interval; sleep most of it, then poll data-ready (a read before that would just NACK)
void finishSCD(bool scdStarted) {
  if (!scdStarted) return;

  lightSleepMs(4600);
  bool dataReady = false;
  for (int i = 0; i < 10 && !(scd4x.getDataReadyStatus(dataReady) == 0 && dataReady); i++) {
    lightSleepMs(250);
  }

  if (dataReady) {
    uint16_t co2Raw;
    float t, h;
    if (scd4x.readMeasurement(co2Raw, t, h) == 0 && co2Raw != 0) {
      co2 = co2Raw;
      tempSCD = t;
      humiditySCD = h;
    }
  }

  scd4x.stopPeriodicMeasurement();  // no 500 ms wait needed, the next command is a full cycle away

  if (co2 > 10000) co2 = NAN;
}

void readSensorBMP() {
  if (!bmpOk) bmpOk = bmp.begin(BMP280_ADDRESS_ALT);  // self-heal from a failed init
  if (!bmpOk) return;

  bmp.setSampling(Adafruit_BMP280::MODE_FORCED,
                  Adafruit_BMP280::SAMPLING_X1,  // temperature
                  Adafruit_BMP280::SAMPLING_X4,  // pressure (reduced from X16 for power savings)
                  Adafruit_BMP280::FILTER_OFF);
  bmp.takeForcedMeasurement();  // Wake, measure, return to sleep
  float hPa = bmp.readPressure() / 100.0f;

  if (isnan(hPa) || hPa < 300 || hPa > 1200) return;  // pressure stays NaN

  pressure = hPa;
  scd4x.setAmbientPressure((uint32_t)(hPa * 100));  // allowed while the SCD40 measures
}

void readSensorSHT() {
  if (!shtOk) return;

  sensors_event_t hum, temp;
  bool ok = sht4.getEvent(&hum, &temp);
  if (!ok) {  // self-heal: soft reset (sensor ready again within 1 ms) and retry once
    sht4.reset();
    delay(2);
    ok = sht4.getEvent(&hum, &temp);
  }
  if (!ok) return;  // getEvent leaves the structs untouched on failure, never read them then

  tempAir = temp.temperature;
  humidity = hum.relative_humidity;
  if (humidity < 0 || humidity > 100) humidity = NAN;
  if (tempAir < -40 || tempAir > 100) tempAir = NAN;
}

void readSensorBatteryVoltage(){
  uint32_t batteryVoltageSum = 0;
  for (int i = 0; i < BATTERY_AVERAGE_SAMPLES; i++) {
    batteryVoltageSum += analogReadMilliVolts(POWER_SENSING_PIN);
  }
  batteryVoltage = (batteryVoltageSum / static_cast<float>(BATTERY_AVERAGE_SAMPLES)) * VOLTAGE_DIVIDER_RATIO / 1000.0;
}

void readSensors() {
  readSensorBatteryVoltage();
  tempESP = temperatureRead();
  bool scdStarted = startSCD();
  readSensorBMP();  // runs during the SCD40 warm-up and feeds it the ambient pressure
  readSensorSHT();
  finishSCD(scdStarted);
}

// ############################### Internet ####################################

const int WIFI_FAST_TIMEOUT_MS = 4000;   // a replayed association normally completes in ~300 ms
const int WIFI_FULL_TIMEOUT_MS = 10000;

bool waitForWiFi(int timeoutMs) {
  for (int i = 0; i < timeoutMs && WiFi.status() != WL_CONNECTED; i+=50){
    delay(50);
  }
  return WiFi.status() == WL_CONNECTED;
}

void applyRadioSettings() {
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_5dBm);
  WiFi.setSleep(WIFI_PS_NONE);
}

void cacheWiFiParams() {
  const uint8_t* bssid = WiFi.BSSID();
  uint32_t ip = (uint32_t)WiFi.localIP();
  if (bssid == nullptr || ip == 0 || WiFi.channel() == 0) return;

  memcpy(rtc_wifiBssid, bssid, sizeof(rtc_wifiBssid));
  rtc_wifiChannel = (uint8_t)WiFi.channel();
  rtc_wifiIp = ip;
  rtc_wifiGateway = (uint32_t)WiFi.gatewayIP();
  rtc_wifiSubnet = (uint32_t)WiFi.subnetMask();
  rtc_wifiDns = (uint32_t)WiFi.dnsIP();
  rtc_wifiCacheValid = true;
}

// A cold association costs ~1.3 s because the radio scans all 13 channels and then runs DHCP.
// Replaying the last working BSSID, channel and lease skips both. Anything that goes wrong -
// AP moved channel, roamed to another BSSID, lease expired - falls back to a full scan in the
// same wake and re-learns, so the cache is self-healing and config.h stays untouched.
bool connectWiFi() {
  WiFi.persistent(false);  // the cache lives in RTC memory; no need to burn a flash write every boot
  applyRadioSettings();

  if (rtc_wifiCacheValid) {
    WiFi.config(IPAddress(rtc_wifiIp), IPAddress(rtc_wifiGateway), IPAddress(rtc_wifiSubnet), IPAddress(rtc_wifiDns));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, rtc_wifiChannel, rtc_wifiBssid);
    if (waitForWiFi(WIFI_FAST_TIMEOUT_MS)) return true;

    rtc_wifiCacheValid = false;
    WiFi.disconnect(true);
    delay(10);
    applyRadioSettings();
  }

  const IPAddress unset((uint32_t)0);
  WiFi.config(unset, unset, unset, unset);  // a zeroed config is what the core reads as "use DHCP"
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  if (!waitForWiFi(WIFI_FULL_TIMEOUT_MS)) return false;

  cacheWiFiParams();
  return true;
}

void disconnectWiFi() {
  WiFi.disconnect(true, false);  // radio off, but keep the credentials the cache was learned from
  WiFi.mode(WIFI_OFF);
}

// Reference time of the newest ICON-D2 run Open-Meteo should already be serving at `epoch`.
// Runs sit on a fixed 3 h UTC grid and the Unix epoch is UTC-aligned, so plain integer division
// lands exactly on 00/03/06/... UTC.
uint32_t latestAvailableWeatherRun(uint32_t epoch) {
  uint32_t ready = WEATHER_RUN_AVAILABILITY_SECONDS + WEATHER_FETCH_MARGIN_SECONDS;
  if (epoch < ready) return 0;
  return ((epoch - ready) / WEATHER_RUN_INTERVAL_SECONDS) * WEATHER_RUN_INTERVAL_SECONDS;
}

void fetchWeatherForecast(uint32_t currentEpoch) {
  if (WiFi.status() != WL_CONNECTED) return;

  String url = String("https://api.open-meteo.com/v1/forecast?latitude=50.06&longitude=14.419998"
                      "&timezone=Europe%2FBerlin&forecast_days=2"
                      "&hourly=temperature_2m,apparent_temperature,rain,snowfall"
                      "&daily=sunset,sunrise&models=icon_d2&forecast_hours=") + FORECAST_FETCH_HOURS;

  HTTPClient http;
  http.begin(url);

  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      http.end();
      return;
    }
    
    // Parse hourly temperature and rain
    JsonArray tempArray = doc["hourly"]["temperature_2m"];
    JsonArray apparentTempArray = doc["hourly"]["apparent_temperature"];
    JsonArray rainArray = doc["hourly"]["rain"];
    JsonArray snowArray = doc["hourly"]["snowfall"];
    JsonArray timeArray = doc["hourly"]["time"];
    
    if (timeArray.size() > 0) {
      String firstTime = timeArray[0].as<String>();
      rtc_forecastStartHour = firstTime.substring(11, 13).toInt();
    }

    int hours = 0;
    for (int i = 0; i < FORECAST_FETCH_HOURS && i < (int)tempArray.size(); i++) {
      rtc_forecastTemp[i] = tempArray[i];
      rtc_forecastApparentTemp[i] = apparentTempArray[i] | rtc_forecastTemp[i];
      // Combine rain and snowfall (snowfall in cm, convert to mm equivalent)
      float rain = rainArray[i] | 0.0f;
      float snow = snowArray[i] | 0.0f;
      rtc_forecastRain[i] = rain + (snow * 10.0f);  // 1cm snow ≈ 10mm water
      hours++;
    }
    if (hours == 0) {
      http.end();
      return;
    }
    rtc_forecastValidHours = hours;
    // The API returns the hourly series starting at the current hour, so index 0 maps to the top of it
    rtc_forecastStartEpoch = currentEpoch - (currentEpoch % 3600UL);
    rtc_lastWeatherRunEpoch = latestAvailableWeatherRun(currentEpoch);

    // Parse sunrise and sunset times
    if (doc["daily"]["sunrise"][0]) {
      String sunriseStr = doc["daily"]["sunrise"][0].as<String>();
      String sunriseShort = sunriseStr.substring(11, 16); // Extract HH:MM
      sunriseShort.toCharArray(rtc_sunriseTimeStr, 6);
    }
    if (doc["daily"]["sunset"][0]) {
      String sunsetStr = doc["daily"]["sunset"][0].as<String>();
      String sunsetShort = sunsetStr.substring(11, 16); // Extract HH:MM
      sunsetShort.toCharArray(rtc_sunsetTimeStr, 6);
    }
    rtc_weatherDataValid = true;
  }

  http.end();
}

// ################################ Display ####################################

void initDisplay1() {
  // BC327 is PNP transistor - LOW turns it ON (provides power to display)
  pinMode(EPD_TRANSISTOR_PIN, OUTPUT);
  digitalWrite(EPD_TRANSISTOR_PIN, LOW);  // Turn on transistor to power display
  
  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, HIGH);
}

void initDisplay2() {
  display.init(115200, false, 2, false);
  display.setRotation(2); // landscape
  display.setTextColor(GxEPD_BLACK);
}

void turnOffDisplay() {
  display.powerOff();
  digitalWrite(EPD_PWR_PIN, LOW);
  pinMode(EPD_CS_PIN, INPUT);
  pinMode(EPD_DC_PIN, INPUT);
  pinMode(EPD_SCK_PIN, INPUT);
  pinMode(EPD_MOSI_PIN, INPUT);
  pinMode(EPD_RST_PIN, INPUT);
  pinMode(EPD_BUSY_PIN, INPUT);
  digitalWrite(EPD_TRANSISTOR_PIN, LOW);
}

// ################################ Time #####################################

uint32_t getCurrentEpoch() {
  return rtc_ntpBaseEpoch + (uint32_t)(rtc_elapsedUs / 1000000ULL);
}

bool syncNTP() {
  uint32_t oldEpoch = getCurrentEpoch();

  configTzTime(TIMEZONE, "pool.ntp.org");
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5000)) return false;

  uint32_t newEpoch = (uint32_t)mktime(&timeinfo);
  int32_t timeDrift = rtc_ntpBaseEpoch == 0 ? 0 : (int32_t)(newEpoch - oldEpoch);

  rtc_ntpBaseEpoch = newEpoch;
  rtc_elapsedUs = 0;

  rtc_lastFullUpdateEpoch += timeDrift;

  return true;
}

// ####################### Home Assistant task ###############################

volatile bool haSendDone = false;
volatile int haSendResult = 0;

void haSendTask(void* param) {
  haSendResult = sendToHomeAssistant(tempAir, tempESP, humidity, co2, pressure, batteryVoltage, tempSCD, humiditySCD, wifiRSSI);
  haSendDone = true;
  vTaskDelete(NULL);
}

// Associating is not the same as being reachable: a cached lease that the router has since handed to
// another device still reports WL_CONNECTED while every request times out. Nothing else would ever
// clear that, so a few silent cycles in a row drop the cache and force a fresh DHCP round.
void noteNetworkResult(int httpCode) {
  if (httpCode > 0) {
    rtc_netFailStreak = 0;
    return;
  }
  if (rtc_netFailStreak < 255) rtc_netFailStreak++;
  if (rtc_netFailStreak >= 3) {
    rtc_wifiCacheValid = false;
    rtc_netFailStreak = 0;
  }
}

// ################################ Setup ####################################

void setup() {
  rtc_bootCount++;

  // Restore timezone after deep sleep reset
  setenv("TZ", TIMEZONE, 1);
  tzset();

  // Account for elapsed time (processing + sleep) of the previous cycle
  rtc_elapsedUs += rtc_priorCycleDurationUs;
  rtc_priorCycleDurationUs = 0;

  if (rtc_bootCount == 1) {
    delay(10000); // Wait for possible upload
  }

  uint32_t currentEpoch = getCurrentEpoch();
  const uint32_t EPSILON = 10;  // 10 s tolerance for timer imprecision

  // Measure everything before WiFi turns on, so radio current peaks can't disturb the readings (battery voltage especially)
  initSensors();
  readSensors();

  bool wifiConnected = connectWiFi();

  bool needsNtpSync = (rtc_ntpBaseEpoch == 0) || (currentEpoch - rtc_ntpBaseEpoch >= NTP_INTERVAL_SECONDS);
  if (needsNtpSync && wifiConnected) {
    if (syncNTP()) currentEpoch = getCurrentEpoch();
  }

  // Decided after the NTP sync so a corrected clock is used. The two cadences are independent: the
  // display redraws hourly off cached data, while the forecast is only re-fetched once a new ICON-D2
  // run is actually on the server.
  bool firstRun = (rtc_bootCount == 1) || !rtc_weatherDataValid;
  bool needsDisplayUpdate = firstRun || (currentEpoch + EPSILON - rtc_lastFullUpdateEpoch >= FULL_UPDATE_SECONDS);
  bool needsWeatherFetch = firstRun || (latestAvailableWeatherRun(currentEpoch) > rtc_lastWeatherRunEpoch);

  haSendDone = true;
  haSendResult = 0;
  if (wifiConnected) {
    wifiRSSI = WiFi.RSSI();
    if (needsWeatherFetch) {
      // Send to Home Assistant in parallel with the weather fetch
      haSendDone = false;
      if (xTaskCreate(haSendTask, "haSend", 8192, NULL, 1, NULL) != pdPASS) {
        haSendResult = sendToHomeAssistant(tempAir, tempESP, humidity, co2, pressure, batteryVoltage, tempSCD, humiditySCD, wifiRSSI);
        haSendDone = true;
      }
      fetchWeatherForecast(currentEpoch);
    } else {
      haSendResult = sendToHomeAssistant(tempAir, tempESP, humidity, co2, pressure, batteryVoltage, tempSCD, humiditySCD, wifiRSSI);
    }
  }

  // Everything on the network is done here. With WIFI_PS_NONE the receiver never sleeps, so leaving
  // the radio associated through a multi-second panel refresh was costing ~70 mA for nothing.
  for (int i = 0; i < 5000 && !haSendDone; i += 50) delay(50);
  if (wifiConnected) noteNetworkResult(haSendResult);
  disconnectWiFi();

  if (needsDisplayUpdate) {
    initDisplay1();
    delay(100);  // the rail used to come up before the WiFi phase; it now needs its own settling time
    initDisplay2();

    // The flash is a whole extra full-panel refresh; the redraws in between are differential updates
    if (ANTI_GHOSTING_EVERY_N_UPDATES <= 1 || rtc_displayUpdateCount % ANTI_GHOSTING_EVERY_N_UPDATES == 0) {
      largeAntiGhosting(display);
    }
    rtc_displayUpdateCount++;

    // Cached forecast can be a few hours old, so start the graph at the current hour instead of at
    // the hour the data was fetched. The extra hours requested from the API are exactly this slack.
    int forecastOffset = 0;
    if (rtc_forecastStartEpoch != 0 && currentEpoch > rtc_forecastStartEpoch) {
      forecastOffset = (int)((currentEpoch - rtc_forecastStartEpoch) / 3600UL);
    }
    int maxOffset = max(rtc_forecastValidHours - FORECAST_DISPLAY_HOURS, 0);
    forecastOffset = constrain(forecastOffset, 0, maxOffset);
    int forecastHours = min(FORECAST_DISPLAY_HOURS, rtc_forecastValidHours - forecastOffset);

    getMoonPhase(currentEpoch);
    rtc_batteryPercent = constrain((int)((batteryVoltage - 3.3f) / (4.1f - 3.3f) * 100.0f), 0, 99);
    updateDisplay(
      display,
      tempAir,
      humidity,
      co2,
      pressure,
      rtc_sunriseTimeStr,
      rtc_sunsetTimeStr,
      rtc_forecastTemp + forecastOffset,
      rtc_forecastApparentTemp + forecastOffset,
      rtc_forecastRain + forecastOffset,
      forecastHours,
      (rtc_forecastStartHour + forecastOffset) % 24,
      rtc_weatherDataValid && forecastHours > 0,
      moonPhase,
      rtc_batteryPercent
    );
    rtc_lastFullUpdateEpoch = currentEpoch;

    turnOffDisplay();
  }

  // Sleep the remainder of the cycle so wake-ups stay on a CYCLE_SECONDS cadence
  int32_t sleepSeconds = min(max((int32_t)CYCLE_SECONDS - (int32_t)(millis() / 1000), (int32_t)30), (int32_t)3600);

  uint64_t sleepUs = (uint64_t)sleepSeconds * 1000000ULL;
  rtc_priorCycleDurationUs = (uint64_t)millis() * 1000ULL + sleepUs;

  if (rtc_bootCount == 1) {
    delay(1000); // Send data over Serial
  }

  esp_sleep_enable_timer_wakeup(sleepUs);
  esp_deep_sleep_start();
}

void loop() {
  // Not used, all logic in setup for deep sleep cycle
}
