// Mock replacement for GxEPD2_BW that renders into an in-memory framebuffer
// instead of driving a real e-paper panel. Lets rendering.cpp compile & run on a PC.
#pragma once
#include <Arduino.h>
#include "gfxfont.h"
#include <vector>
#include <cstring>

// e-paper colors (match GxEPD).
#define GxEPD_BLACK 0x0000
#define GxEPD_WHITE 0xFFFF

// Panel descriptor mirroring GxEPD2_397_GDEM0397T81 constants used by rendering.cpp.
struct GxEPD2_397_GDEM0397T81 {
  static const uint16_t WIDTH = 800;
  static const uint16_t WIDTH_VISIBLE = 800;
  static const uint16_t HEIGHT = 480;
};

template <class PANEL, int PAGE_HEIGHT>
class GxEPD2_BW {
public:
  int _w = PANEL::WIDTH;
  int _h = PANEL::HEIGHT;
  int rotation = 0;
  std::vector<uint8_t> fb; // 1 byte per pixel, 0=black 255=white
  const GFXfont* font = nullptr;
  int cursorX = 0, cursorY = 0;
  uint16_t textColor = GxEPD_BLACK;
  int pageCount = 0;

  GxEPD2_BW() { fb.assign((size_t)_w * _h, 255); }

  int width() const { return (rotation & 1) ? _h : _w; }
  int height() const { return (rotation & 1) ? _w : _h; }
  void setRotation(int r) { rotation = r & 3; }

  // --- page lifecycle: run the do/while body exactly once ---
  void setPartialWindow(int, int, int, int) {}
  void setFullWindow() {}
  void firstPage() { pageCount = 0; }
  bool nextPage() { return (++pageCount) < 1; }
  void init(...) {}
  void powerOff() {}
  void hibernate() {}

  void setTextColor(uint16_t c) { textColor = c; }
  void setFont(const GFXfont* f) { font = f; }
  void setCursor(int x, int y) { cursorX = x; cursorY = y; }

  // --- primitives ---
  inline void setPix(int x, int y, uint16_t color) {
    if (x < 0 || y < 0 || x >= width() || y >= height()) return;
    // map rotated logical coords back into physical buffer
    int px = x, py = y;
    switch (rotation) {
      case 1: px = _w - 1 - y; py = x; break;
      case 2: px = _w - 1 - x; py = _h - 1 - y; break;
      case 3: px = y;          py = _h - 1 - x; break;
      default: px = x; py = y; break;
    }
    if (px < 0 || py < 0 || px >= _w || py >= _h) return;
    fb[(size_t)py * _w + px] = (color == GxEPD_BLACK) ? 0 : 255;
  }

  void drawPixel(int x, int y, uint16_t color) { setPix(x, y, color); }

  void fillScreen(uint16_t color) {
    uint8_t v = (color == GxEPD_BLACK) ? 0 : 255;
    std::memset(fb.data(), v, fb.size());
  }

  void fillRect(int x, int y, int w, int h, uint16_t color) {
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) setPix(x + i, y + j, color);
  }

  void drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
      setPix(x0, y0, color);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }

  void drawBitmap(int x, int y, const unsigned char* bitmap, int w, int h, uint16_t color) {
    int rowBytes = (w + 7) / 8;
    for (int j = 0; j < h; j++) {
      for (int i = 0; i < w; i++) {
        uint8_t byte = bitmap[j * rowBytes + (i >> 3)];
        if (byte & (0x80 >> (i & 7))) setPix(x + i, y + j, color);
      }
    }
  }

  // --- text (Adafruit GFX custom-font algorithm) ---
  void drawCharGlyph(int& cx, int cy, char c) {
    if (!font) return;
    if (c < font->first || c > font->last) return;
    const GFXglyph* g = &font->glyph[c - font->first];
    const uint8_t* bmp = font->bitmap;
    uint16_t bo = g->bitmapOffset;
    uint8_t bits = 0, bit = 0;
    for (int yy = 0; yy < g->height; yy++) {
      for (int xx = 0; xx < g->width; xx++) {
        if (!(bit++ & 7)) bits = bmp[bo++];
        if (bits & 0x80) setPix(cx + g->xOffset + xx, cy + g->yOffset + yy, textColor);
        bits <<= 1;
      }
    }
    cx += g->xAdvance;
  }

  void print(const String& s) {
    for (char c : s.s) drawCharGlyph(cursorX, cursorY, c);
  }
  void print(int v) { print(String(v)); }
  void print(const char* c) { print(String(c)); }

  void getTextBounds(const String& str, int x, int y,
                     int16_t* x1, int16_t* y1, uint16_t* w, uint16_t* h) {
    int minx = 100000, miny = 100000, maxx = -100000, maxy = -100000;
    int cx = x, cy = y;
    if (font) {
      for (char c : str.s) {
        if (c < font->first || c > font->last) continue;
        const GFXglyph* g = &font->glyph[c - font->first];
        int gx1 = cx + g->xOffset, gy1 = cy + g->yOffset;
        int gx2 = gx1 + g->width, gy2 = gy1 + g->height;
        if (gx1 < minx) minx = gx1;
        if (gy1 < miny) miny = gy1;
        if (gx2 > maxx) maxx = gx2;
        if (gy2 > maxy) maxy = gy2;
        cx += g->xAdvance;
      }
    }
    if (maxx < minx) { minx = x; miny = y; maxx = x; maxy = y; }
    *x1 = (int16_t)minx; *y1 = (int16_t)miny;
    *w = (uint16_t)(maxx - minx); *h = (uint16_t)(maxy - miny);
  }

  // --- export to a PPM (P6) file, easily viewed/converted ---
  void savePPM(const char* path) {
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", _w, _h);
    for (size_t i = 0; i < fb.size(); i++) {
      uint8_t v = fb[i];
      fputc(v, f); fputc(v, f); fputc(v, f);
    }
    fclose(f);
  }
};
