#include "rendering.h"
#include "icons/temp.icon.h"
#include "icons/humidity.icon.h"
#include "icons/pressure.icon.h"
#include "icons/sunrise.icon.h"
#include "icons/sunset.icon.h"
#include "icons/co2.icon.h"

namespace {
inline void drawDashedHLine(GxEPD2_BW<GxEPD2_397_GDEM0397T81, GxEPD2_397_GDEM0397T81::HEIGHT>& display, int x1, int x2, int y, int onLen = 3, int offLen = 3) {
	for (int xx = x1; xx <= x2; xx += onLen + offLen) {
		int segW = std::min(onLen, x2 - xx + 1);
		if (segW > 0) display.drawLine(xx, y, xx + segW - 1, y, GxEPD_BLACK);
	}
}

inline void drawDashedVLine(GxEPD2_BW<GxEPD2_397_GDEM0397T81, GxEPD2_397_GDEM0397T81::HEIGHT>& display, int y1, int y2, int x, int onLen = 3, int offLen = 3) {
	for (int yy = y1; yy <= y2; yy += onLen + offLen) {
		int segH = std::min(onLen, y2 - yy + 1);
		if (segH > 0) display.drawLine(x, yy, x, yy + segH - 1, GxEPD_BLACK);
	}
}
}

void drawForecastGraph(GxEPD2_BW<GxEPD2_397_GDEM0397T81, GxEPD2_397_GDEM0397T81::HEIGHT>& display,
					  int x, int y, int w, int h, const float* data, int dataSize, float minVal, float maxVal) {
	float range = maxVal - minVal;
	if (range <= 0.001f) range = 1.0f;

	int firstLine = static_cast<int>(floor(minVal / 10.0f)) * 10;
	for (int t = firstLine; t <= static_cast<int>(ceil(maxVal)); t += 10) {
		float val = static_cast<float>(t);
		int yy = y + h - static_cast<int>(((val - minVal) / range) * h);
		if (yy < y || yy > y + h) continue;
		if (t == 0) {
			display.drawLine(x + 1, yy, x + w - 2, yy, GxEPD_BLACK);
		} else {
			drawDashedHLine(display, x + 1, x + w - 2, yy);
		}
	}

	for (int i = 1; i < dataSize; i++) {
		int x1 = x + (i - 1) * w / dataSize;
		int y1 = y + h - static_cast<int>(((data[i - 1] - minVal) / range) * h);
		int x2 = x + i * w / dataSize;
		int y2 = y + h - static_cast<int>(((data[i] - minVal) / range) * h);
		for (int off = -2; off <= 2; off++) {
			display.drawLine(x1, y1 + off, x2, y2 + off, GxEPD_BLACK);
		}
	}
}

void drawRainColumns(GxEPD2_BW<GxEPD2_397_GDEM0397T81, GxEPD2_397_GDEM0397T81::HEIGHT>& display,
					int x, int y, int w, int h, const float* data, int dataSize, float maxVal) {
	int colWidth = std::max(1, w / dataSize);
	for (int i = 0; i < dataSize; i++) {
		float v = data[i];
		if (v <= 0) continue;
		int colHeight = static_cast<int>((v / maxVal) * h);
		if (colHeight < 1) colHeight = 1;
		int x1 = x + i * colWidth;
		int y1 = y;
		display.fillRect(x1, y1, colWidth - 1, colHeight, GxEPD_BLACK);
	}
}

void drawWeatherForecast(GxEPD2_BW<GxEPD2_397_GDEM0397T81, GxEPD2_397_GDEM0397T81::HEIGHT>& display,
						const float* forecastTemp, const float* forecastRain, int forecastHours,
						int forecastStartHour, const String& sunriseTime, const String& sunsetTime, bool weatherDataValid) {
	if (!weatherDataValid) return;
	int screenW = display.width();
	int screenH = display.height();

	int weatherY = 8;
	int graphX = 4;
	int graphWidth = screenW - 8;
	int graphHeight = (screenH * 50) / 100;

	float minTemp = forecastTemp[0], maxTemp = forecastTemp[0];
	float maxRain = 0;
	for (int i = 0; i < forecastHours; i++) {
		if (forecastTemp[i] < minTemp) minTemp = forecastTemp[i];
		if (forecastTemp[i] > maxTemp) maxTemp = forecastTemp[i];
		if (forecastRain[i] > maxRain) maxRain = forecastRain[i];
	}
	minTemp = floor(minTemp);
	maxTemp = ceil(maxTemp);
	if (minTemp == maxTemp) { minTemp -= 1; maxTemp += 1; }

	int tempGraphY = weatherY + 28;
	drawForecastGraph(display, graphX, tempGraphY, graphWidth, graphHeight, forecastTemp, forecastHours, minTemp, maxTemp);

	// Draw vertical lines and hour labels only at even hours
	display.setFont(&FreeSans12pt7b);
	int hourY = weatherY + 18;
	for (int i = 0; i < forecastHours; i++) {
		int hour = (forecastStartHour + i) % 24;
		if (hour % 2 != 0) continue; // Only even hours
		int xx = graphX + i * graphWidth / forecastHours;
		
		// Draw hour label
		String hlabel = String(hour);
		int16_t tbx, tby; uint16_t tbw, tbh;
		display.getTextBounds(hlabel, xx, hourY, &tbx, &tby, &tbw, &tbh);
		display.setCursor(xx - tbw / 2, hourY);
		display.print(hlabel);
		
		// Draw vertical line (thick for 00:00 and 12:00, dashed for others)
		if (hour == 0 || hour == 12) {
			display.drawLine(xx, tempGraphY + 1, xx, tempGraphY + graphHeight - 1, GxEPD_BLACK);
		} else {
			drawDashedVLine(display, tempGraphY + 2, tempGraphY + graphHeight - 2, xx);
		}
	}

	display.setFont(&FreeSansBold18pt7b);
	int labelX = graphX + graphWidth - 28;
	display.setCursor(labelX, tempGraphY + 32);
	display.print(String(static_cast<int>(maxTemp)));
	display.setCursor(labelX, tempGraphY + graphHeight - 14);
	display.print(String(static_cast<int>(minTemp)));

	int rainHeight = std::max(12, graphHeight / 3);
	int rainY = tempGraphY + graphHeight;
	drawRainColumns(display, graphX, rainY, graphWidth, rainHeight, forecastRain, forecastHours, max(maxRain, 1.0f));

	display.setFont(&FreeSans18pt7b);
	display.setCursor(labelX, rainY + 40);
	display.print(String(static_cast<int>(ceil(maxRain))));
}

void updateDisplay(GxEPD2_BW<GxEPD2_397_GDEM0397T81, GxEPD2_397_GDEM0397T81::HEIGHT>& display,
				   float tempAir, float humidity, float co2, float pressure,
				   const String& sunriseTime, const String& sunsetTime,
				   const float* forecastTemp, const float* forecastRain, int forecastHours,
				   int forecastStartHour, bool weatherDataValid) {
	display.setFullWindow();
	display.firstPage();
	do {
		display.fillScreen(GxEPD_WHITE);
		drawWeatherForecast(display, forecastTemp, forecastRain, forecastHours, forecastStartHour, sunriseTime, sunsetTime, weatherDataValid);

		int screenW = display.width();
		int screenH = display.height();
		int bottomH = std::max(64, screenH / 5);
		int bottomY = screenH - bottomH - 16;

		int icons = 6;
		int iconSize = 64;
		int gap = (screenW - icons * iconSize) / (icons + 1);
		int iconY = bottomY + 2;
		display.setFont(&FreeSans18pt7b);
		for (int i = 0; i < icons; i++) {
			int ix = gap + i * (iconSize + gap);
			int valY = iconY + iconSize + 20;
			String v;
			switch (i) {
				case 0: v = String(tempAir, 1) + "C"; break;
				case 1: v = String(static_cast<int>(humidity)) + "%"; break;
				case 2: v = sunriseTime; break;
				case 3: v = sunsetTime; break;
				case 4: v = String(static_cast<int>(co2)); break;
				case 5: v = String(static_cast<int>(pressure)); break;
			}
			display.setFont(&FreeSansBold18pt7b);
			int16_t tbx, tby; uint16_t tbw, tbh;
			display.getTextBounds(v, ix + iconSize / 2, valY, &tbx, &tby, &tbw, &tbh);
			display.setCursor(ix + iconSize / 2 - tbw / 2, valY + tbh / 2);
			display.print(v);
			display.setFont(&FreeSans18pt7b);

			const int iconW = 64;
			const int iconH = 64;
			int iconDrawX = ix + (iconSize - iconW) / 2;
			int iconDrawY = iconY + (iconSize - iconH) / 2;
			switch (i) {
				case 0: display.drawBitmap(iconDrawX, iconDrawY, temp_icon_bits, iconW, iconH, GxEPD_BLACK); break;
				case 1: display.drawBitmap(iconDrawX, iconDrawY, epd_bitmap_humidity, iconW, iconH, GxEPD_BLACK); break;
				case 2: display.drawBitmap(iconDrawX, iconDrawY, epd_bitmap_sunrise, iconW, iconH, GxEPD_BLACK); break;
				case 3: display.drawBitmap(iconDrawX, iconDrawY, epd_bitmap_sunset, iconW, iconH, GxEPD_BLACK); break;
				case 4: display.drawBitmap(iconDrawX, iconDrawY, epd_bitmap_co2, iconW, iconH, GxEPD_BLACK); break;
				case 5: display.drawBitmap(iconDrawX, iconDrawY, epd_bitmap_pressure, iconW, iconH, GxEPD_BLACK); break;
			}
		}
	} while (display.nextPage());
}
