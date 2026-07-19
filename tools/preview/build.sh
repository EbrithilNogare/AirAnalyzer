#!/usr/bin/env bash
# Render the real rendering.cpp output to a PNG on your Mac — no ESP needed.
set -e
cd "$(dirname "$0")/../.."   # repo root

GFX=".pio/libdeps/main/Adafruit GFX Library"

# shim headers win over real Arduino/GxEPD2; real dir only supplies Fonts/*.
c++ -std=c++17 -O2 -w \
  -I tools/preview/shim \
  -I "$GFX" \
  -I src \
  tools/preview/preview.cpp src/rendering.cpp \
  -o tools/preview/preview

./tools/preview/preview
python3 tools/preview/ppm2png.py tools/preview/out.ppm tools/preview/out.png
echo "open tools/preview/out.png"
