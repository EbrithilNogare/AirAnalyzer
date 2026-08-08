# Home Assistant Setup Guide

This guide explains how to set up Home Assistant to receive sensor data from your AirAnalyzer device.

## Overview

The AirAnalyzer sends the following sensor data to Home Assistant:

- **temperature**: Air temperature from SHT4x sensor (°C)
- **temperature_esp**: ESP32 internal temperature (°C)
- **humidity**: Relative humidity from SHT4x sensor (%)
- **co2**: CO₂ concentration from SCD40 sensor (ppm)
- **pressure**: Atmospheric pressure from BMP280 sensor (hPa)
- **battery_voltage**: Battery voltage (V)
- **temperature_scd**: Air temperature from SCD40 sensor (°C)
- **humidity_scd**: Relative humidity from SCD40 sensor (%)
- **wifi_rssi**: WiFi signal strength (dBm)

## Setup Steps

### 1. Create Webhook Automation in Home Assistant

1. Go to **Settings** → **Automations & Scenes**
2. Click **+ Create Automation** → **Create new automation**
3. Click the **⋮** menu → **Edit in YAML**
4. Replace with this configuration:

```yaml
alias: AirAnalyzer Data Receiver
description: Receives sensor data from AirAnalyzer device
trigger:
  - platform: webhook
    webhook_id: air_analyzer
    allowed_methods:
      - POST
    local_only: true
condition: []
action:
  - event: air_analyzer_data_received
    event_data:
      temperature: "{{ trigger.json.temperature }}"
      temperature_esp: "{{ trigger.json.temperature_esp }}"
      humidity: "{{ trigger.json.humidity }}"
      co2: "{{ trigger.json.co2 }}"
      pressure: "{{ trigger.json.pressure }}"
      battery_voltage: "{{ trigger.json.battery_voltage }}"
      temperature_scd: "{{ trigger.json.temperature_scd }}"
      humidity_scd: "{{ trigger.json.humidity_scd }}"
      wifi_rssi: "{{ trigger.json.wifi_rssi }}"
mode: single
```

5. Click **Save**

### 2. Add Sensors to Configuration

Add the following to your `configuration.yaml`:

```yaml
# AirAnalyzer Sensors
template:
  - trigger:
      - platform: event
        event_type: air_analyzer_data_received
    sensor:
      - name: "Air Analyzer Temperature"
        unique_id: air_analyzer_temperature
        state: "{{ trigger.event.data.temperature }}"
        unit_of_measurement: "°C"
        device_class: temperature
        state_class: measurement

      - name: "Air Analyzer Humidity"
        unique_id: air_analyzer_humidity
        state: "{{ trigger.event.data.humidity }}"
        unit_of_measurement: "%"
        device_class: humidity
        state_class: measurement

      - name: "Air Analyzer CO₂"
        unique_id: air_analyzer_co2
        state: "{{ trigger.event.data.co2 }}"
        unit_of_measurement: "ppm"
        device_class: carbon_dioxide
        state_class: measurement

      - name: "Air Analyzer Pressure"
        unique_id: air_analyzer_pressure
        state: "{{ trigger.event.data.pressure }}"
        unit_of_measurement: "hPa"
        device_class: atmospheric_pressure
        state_class: measurement

      - name: "Air Analyzer Battery"
        unique_id: air_analyzer_battery
        state: "{{ trigger.event.data.battery_voltage }}"
        unit_of_measurement: "V"
        device_class: voltage
        state_class: measurement

      - name: "Air Analyzer ESP Temperature"
        unique_id: air_analyzer_esp_temperature
        state: "{{ trigger.event.data.temperature_esp }}"
        unit_of_measurement: "°C"
        device_class: temperature
        state_class: measurement

      - name: "Air Analyzer SCD Temperature"
        unique_id: air_analyzer_scd_temperature
        state: "{{ trigger.event.data.temperature_scd }}"
        unit_of_measurement: "°C"
        device_class: temperature
        state_class: measurement

      - name: "Air Analyzer SCD Humidity"
        unique_id: air_analyzer_scd_humidity
        state: "{{ trigger.event.data.humidity_scd }}"
        unit_of_measurement: "%"
        device_class: humidity
        state_class: measurement

      - name: "Air Analyzer WiFi RSSI"
        unique_id: air_analyzer_wifi_rssi
        state: "{{ trigger.event.data.wifi_rssi }}"
        unit_of_measurement: "dBm"
        device_class: signal_strength
        state_class: measurement
```

After adding this, restart Home Assistant.

### 3. Add Sensors to Dashboard

The sensors will appear as:

- `sensor.air_analyzer_temperature`
- `sensor.air_analyzer_humidity`
- `sensor.air_analyzer_co2`
- `sensor.air_analyzer_pressure`
- `sensor.air_analyzer_battery`
- `sensor.air_analyzer_esp_temperature`
- `sensor.air_analyzer_scd_temperature`
- `sensor.air_analyzer_scd_humidity`
- `sensor.air_analyzer_wifi_rssi`

1. Go to your dashboard
2. Click **Edit Dashboard** → **+ Add Card** → **Entities Card**
3. Add these entities to display your sensor data

## Testing

### Test the Webhook

You can test the webhook from your computer using curl:

```bash
curl -X POST http://homeassistant.local:8123/api/webhook/XXXXX -H "Content-Type: application/json" -d '{ "temperature": 22.5, "temperature_esp": 35.2, "humidity": 45.3, "co2": 650, "pressure": 1013, "battery_voltage": 4.15, "temperature_scd": 22.8, "humidity_scd": 44.7, "wifi_rssi": -62 }'
```

If successful, you should see the values update in your Home Assistant helpers.
