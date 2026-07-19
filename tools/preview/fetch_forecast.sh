#!/usr/bin/env bash
# Fetch the live Open-Meteo forecast (same URL the device uses) and bake it
# into tools/preview/forecast_data.h so the preview renders real data.
set -e
cd "$(dirname "$0")"
URL="https://api.open-meteo.com/v1/forecast?latitude=50.06&longitude=14.419998&timezone=Europe%2FBerlin&forecast_days=1&hourly=temperature_2m,apparent_temperature,rain,snowfall&daily=sunset,sunrise&forecast_hours=24&models=icon_d2"
curl -s "$URL" -o forecast.json
python3 - <<'PY'
import json
d = json.load(open('forecast.json'))
h = d['hourly']; n = len(h['time'])
def arr(name, vals):
    return "const float %s[%d] = {%s};" % (name, n, ", ".join("%.2ff" % (v or 0) for v in vals))
rain = [(h['rain'][i] or 0) + (h['snowfall'][i] or 0) * 10.0 for i in range(n)]
out = [
    "// Auto-generated live forecast snapshot from Open-Meteo (icon_d2).",
    "// Source time: %s  (refresh with tools/preview/fetch_forecast.sh)" % h['time'][0],
    "#pragma once",
    "#define FC_HOURS %d" % n,
    arr("FC_TEMP", h['temperature_2m']),
    arr("FC_APPARENT", h['apparent_temperature']),
    arr("FC_RAIN", rain),
    "#define FC_START_HOUR %d" % int(h['time'][0][11:13]),
    '#define FC_SUNRISE "%s"' % d['daily']['sunrise'][0][11:16],
    '#define FC_SUNSET "%s"' % d['daily']['sunset'][0][11:16],
]
open('forecast_data.h', 'w').write("\n".join(out) + "\n")
print("updated forecast_data.h  (%d hours from %s)" % (n, h['time'][0]))
PY
