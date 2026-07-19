// Minimal Arduino shim for local (desktop) rendering preview.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <string>
#include <algorithm>

#ifndef PROGMEM
#define PROGMEM
#endif
#define pgm_read_byte(addr) (*(const unsigned char*)(addr))

inline void delay(unsigned long) {}

// Arduino's global max/min (used as bare max()/min() in rendering.cpp).
template <class T> inline T max(T a, T b) { return a > b ? a : b; }
template <class T> inline T min(T a, T b) { return a < b ? a : b; }

// Minimal Arduino String backed by std::string.
class String {
public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& c) : s(c) {}
  String(int v) { char b[32]; snprintf(b, sizeof(b), "%d", v); s = b; }
  String(long v) { char b[32]; snprintf(b, sizeof(b), "%ld", v); s = b; }
  String(unsigned v) { char b[32]; snprintf(b, sizeof(b), "%u", v); s = b; }
  String(float v, int dec = 2) { char b[48]; snprintf(b, sizeof(b), "%.*f", dec, v); s = b; }
  String(double v, int dec = 2) { char b[48]; snprintf(b, sizeof(b), "%.*f", dec, v); s = b; }

  unsigned length() const { return (unsigned)s.size(); }
  const char* c_str() const { return s.c_str(); }
  char charAt(int i) const { return s[i]; }
  char operator[](int i) const { return s[i]; }

  String operator+(const String& o) const { return String(s + o.s); }
  String operator+(const char* o) const { return String(s + (o ? o : "")); }
  String& operator+=(const String& o) { s += o.s; return *this; }
};
inline String operator+(const char* a, const String& b) { return String(std::string(a) + b.s); }
