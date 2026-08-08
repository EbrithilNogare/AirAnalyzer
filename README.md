# 🌡️ AirAnalyzer

A DIY ESP32-based environmental monitor with an e-paper display!

|![Device](graphics/air%20analyzer.png)|![Internals](graphics/air%20analyzer%20in.png)|
|:--:|:--:|
|Outside|Inside|

## What it does

- Measures **temperature**, **humidity**, **CO2**, and **pressure**
- Fetches **weather forecast** from Open-Meteo API
- Shows **sunrise/sunset** times
- Uploads data to **ThingSpeak**
- Runs on **deep sleep** for low power consumption

## Hardware

- ESP32-C3
- SCD40 (CO2 sensor)
- SHT4x (temperature & humidity)
- BMP280 (pressure)
- 3.97" e-paper display

## Connections

```mermaid
---
config:
  layout: elk
---
flowchart LR
    ESP["ESP32-C3<br>Seeed Studio"] -- "3V3 - VCC<br>GND - GND<br>GPIO 6 - SDA<br>GPIO 7 - SCL<br>BAT+ - BAT+<br>BAT- - BAT-" --- Board["Board"]

    Board -- "VCC - VCC<br>GND - GND<br>SCL - SCL<br>SDA - SDA" --- SCD40["SCD40"] & SHT4x["SHT4x"] & BMP280["BMP280"]

    ESP -- "GPIO 20 - Base" --- Transistor["BC327 PNP Transistor"]
    Transistor -- "Collector - VCC" --- Display["E-Paper Display"]
    ESP -- "GPIO 9 - PWR" --- Display
    ESP -- "GPIO 3 - BUSY" --- Display
    ESP -- "GPIO 4 - RST" --- Display
    ESP -- "GPIO 5 - DC" --- Display
    ESP -- "GPIO 21 - CS" --- Display
    ESP -- "GPIO 8 - SCLK" --- Display
    ESP -- "GPIO 10 - DIN" --- Display
    ESP -- "GND - GND" --- Display
    ESP -- "3V3 - Emitter" --- Transistor

    ESP -- "BAT+ - BAT+" --- Charger["CJMCU-2557"]
    ESP -- "BAT- - BAT-" --- Charger
    Battery["LiPo Battery<br>500 mAh"] -- "Positive - BAT+" --- Charger
    Battery -- "Negative - BAT-" --- Charger
    Solar["Solar Panel"] -- "Positive - Input +" --- Charger
    Solar -- "Negative - Input -" --- Charger
    Solar -- "Zener diode (1N4733 5V1)" --- Solar

    ESP:::controller
    Board:::board
    SCD40:::sensor
    SHT4x:::sensor
    BMP280:::sensor
    Transistor:::transistor
    Display:::display
    Charger:::charger
    Battery:::battery
    Solar:::solar

    classDef controller stroke:#818cf8,fill:#eef2ff
    classDef board stroke:#fb923c,fill:#fff7ed
    classDef sensor stroke:#4ade80,fill:#f0fdf4
    classDef transistor stroke:#f87171,fill:#fef2f2
    classDef display stroke:#a78bfa,fill:#f5f3ff
    classDef charger stroke:#22d3ee,fill:#ecfeff
    classDef battery stroke:#facc15,fill:#fefce8
    classDef solar stroke:#fb923c,fill:#fff7ed
```

## Setup

1. Copy `include/config.example.h` to `include/config.h`
2. Add your WiFi credentials and ThingSpeak API key
3. Build & upload with PlatformIO

---

_Icons converted using [image2cpp](https://javl.github.io/image2cpp/)_
