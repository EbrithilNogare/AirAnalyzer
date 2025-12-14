#include "rendering.h"
#include "icons/temp.icon.h"
#include "icons/humidity.icon.h"
#include "icons/pressure.icon.h"
#include "icons/sunrise.icon.h"
#include "icons/sunset.icon.h"
#include "icons/co2.icon.h"
#include "icons/moon.icon.h"

void largeAntiGhosting(DisplayType& display) {
  display.fillScreen(GxEPD_WHITE);
  display.nextPage();
}

void smallAntiGhosting(DisplayType& display) {
  display.setPartialWindow(0, GxEPD2_397_GDEM0397T81::HEIGHT - 50, GxEPD2_397_GDEM0397T81::WIDTH_VISIBLE, 30);
  display.fillScreen(GxEPD_WHITE);
  display.nextPage();
}

inline void drawDashedHLine(DisplayType& display, int x1, int x2, int y, int onLen = 3, int offLen = 3) {
	for (int xx = x1; xx <= x2; xx += onLen + offLen) {
		int segW = std::min(onLen, x2 - xx + 1);
		if (segW > 0)
			display.drawLine(xx, y, xx + segW - 1, y, GxEPD_BLACK);
	}
}

inline void drawDashedVLine(DisplayType& display, int y1, int y2, int x, int onLen = 3, int offLen = 3) {
	for (int yy = y1; yy <= y2; yy += onLen + offLen) {
		int segH = std::min(onLen, y2 - yy + 1);
		if (segH > 0)
			display.drawLine(x, yy, x, yy + segH - 1, GxEPD_BLACK);
	}
}

const uint8_t ditherPatterns[8][4] = {
	{0b1111, 0b1111, 0b1111, 0b1111},
	{0b1111, 0b0111, 0b1111, 0b1101},
	{0b1010, 0b1111, 0b1010, 0b1111},
	{0b1010, 0b0101, 0b1010, 0b0101},
	{0b1010, 0b0000, 0b0101, 0b0000},
	{0b1000, 0b0000, 0b0010, 0b0000},
	{0b0000, 0b0100, 0b0000, 0b0000},
	{0b0000, 0b0000, 0b0000, 0b0000},
};

inline bool shouldDrawPixel(int x, int y, int ditherLevel) {
	if (ditherLevel >= 8) return false;
	if (ditherLevel < 0) ditherLevel = 0;
	int px = x % 4;
	int py = y % 4;
	return (ditherPatterns[ditherLevel][py] >> (3 - px)) & 1;
}

void drawForecastGraph(DisplayType& display, int x, int y, int w, int h, const float* data, int dataSize, float minVal, float maxVal) {
	float range = maxVal - minVal;
	if (range <= 0.001f) range = 1.0f;

	int zeroY = y + h - static_cast<int>(((0.0f - minVal) / range) * h);
	bool zeroInRange = (zeroY >= y && zeroY <= y + h);

	const int fadeDepth = 24;
	const int fadeSteps = 8;
	
	for (int i = 0; i < dataSize - 1; i++) {
		int x1 = x + i * w / dataSize;
		int x2 = x + (i + 1) * w / dataSize;
		int lineY = y + h - static_cast<int>(((data[i] - minVal) / range) * h);
		
		bool aboveZero = data[i] >= 0;
		
		for (int px = x1; px < x2; px++) {
			float t = static_cast<float>(px - x1) / static_cast<float>(x2 - x1);
			int nextLineY = y + h - static_cast<int>(((data[i + 1] - minVal) / range) * h);
			int currentY = lineY + static_cast<int>(t * (nextLineY - lineY));
			
			currentY = std::max(y, std::min(y + h - 1, currentY));
			
			if (aboveZero) {
				int trailEnd = zeroInRange ? std::min(zeroY, y + h - 1) : y + h - 1;
				trailEnd = std::min(trailEnd, currentY + fadeDepth);
				
				for (int py = currentY + 3; py <= trailEnd; py++) {
					int dist = py - currentY;
					int ditherLevel = (dist * fadeSteps) / fadeDepth;
					if (shouldDrawPixel(px, py, ditherLevel)) {
						display.drawPixel(px, py, GxEPD_BLACK);
					}
				}
			} else {
				int trailEnd = zeroInRange ? std::max(zeroY, y) : y;
				trailEnd = std::max(trailEnd, currentY - fadeDepth);
				
				for (int py = currentY - 3; py >= trailEnd; py--) {
					int dist = currentY - py;
					int ditherLevel = (dist * fadeSteps) / fadeDepth;
					if (shouldDrawPixel(px, py, ditherLevel)) {
						display.drawPixel(px, py, GxEPD_BLACK);
					}
				}
			}
		}
	}

	int firstLine = static_cast<int>(floor(minVal / 10.0f)) * 10;
	for (int t = firstLine; t <= static_cast<int>(ceil(maxVal)); t += 10) {
		float val = static_cast<float>(t);
		int yy = y + h - static_cast<int>(((val - minVal) / range) * h);
		if (yy < y || yy > y + h) continue;
		if (t == 0) {
			display.drawLine(x + 1, yy - 1, x + w - 2, yy - 1, GxEPD_BLACK);
			display.drawLine(x + 1, yy, x + w - 2, yy, GxEPD_BLACK);
			display.drawLine(x + 1, yy + 1, x + w - 2, yy + 1, GxEPD_BLACK);
		} else {
			display.drawLine(x + 1, yy, x + w - 2, yy, GxEPD_BLACK);
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

void drawRainColumns(DisplayType& display, int x, int y, int w, int h, const float* data, int dataSize, float maxVal) {
	int colWidth = std::max(1, w / dataSize);
	for (int i = 0; i < dataSize; i++) {
		float v = data[i];
		if (v <= 0) continue;
		int colHeight = static_cast<int>((v / maxVal) * h);
		if (colHeight < 1) colHeight = 1;
		int x1 = x + i * colWidth;
		int y1 = y + h - colHeight;
		display.fillRect(x1, y1, colWidth - 1, colHeight, GxEPD_BLACK);
	}
}

void drawWeatherForecast(DisplayType& display, const float* forecastTemp, const float* forecastRain, int forecastHours, int forecastStartHour, const String& sunriseTime, const String& sunsetTime, bool weatherDataValid) {
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
	int rainHeight = std::max(12, graphHeight / 3);
	int rainY = tempGraphY + graphHeight;
	
	drawForecastGraph(display, graphX, tempGraphY, graphWidth, graphHeight, forecastTemp, forecastHours, minTemp, maxTemp);

	display.setFont(&FreeSans12pt7b);
	int hourY = weatherY + 18;
	int lineEndY = rainY + rainHeight;
	
	for (int i = 0; i < forecastHours; i++) {
		int hour = (forecastStartHour + i) % 24;
		if (hour % 2 != 0) continue;
		int xx = graphX + i * graphWidth / forecastHours;
		
		String hlabel = String(hour);
		int16_t tbx, tby; uint16_t tbw, tbh;
		display.getTextBounds(hlabel, xx, hourY, &tbx, &tby, &tbw, &tbh);
		display.setCursor(xx - tbw / 2, hourY);
		display.print(hlabel);
		
		if (hour == 0 || hour == 12) {
			display.drawLine(xx - 1, tempGraphY + 1, xx + 1, lineEndY - 1, GxEPD_BLACK);
			display.drawLine(xx, tempGraphY + 1, xx, lineEndY - 1, GxEPD_BLACK);
			display.drawLine(xx + 1, tempGraphY + 1, xx + 1, lineEndY - 1, GxEPD_BLACK);
		} else {
			display.drawLine(xx, tempGraphY + 1, xx, lineEndY - 1, GxEPD_BLACK);
		}
	}

	display.setFont(&FreeSansBold18pt7b);
	int charWidth = 19;
	
	String maxTempStr = String(static_cast<int>(maxTemp));
	int maxLabelX = graphX + graphWidth - (maxTempStr.length() * charWidth);
	display.setCursor(maxLabelX, tempGraphY + 32);
	display.print(maxTempStr);
	
	String minTempStr = String(static_cast<int>(minTemp));
	int minLabelX = graphX + graphWidth - (minTempStr.length() * charWidth);
	display.setCursor(minLabelX, tempGraphY + graphHeight - 14);
	display.print(minTempStr);

	drawRainColumns(display, graphX, rainY, graphWidth, rainHeight, forecastRain, forecastHours, max(maxRain, 1.0f));

	display.setFont(&FreeSans18pt7b);
	String rainStr = String(static_cast<int>(ceil(maxRain)));
	int rainLabelX = graphX + graphWidth - (rainStr.length() * charWidth);
	display.setCursor(rainLabelX, rainY + 40);
	display.print(rainStr);
}

void updateDisplay(
		DisplayType& display,
		float tempAir,
		float humidity,
		float co2,
		float pressure,
		const String& sunriseTime,
		const String& sunsetTime,
		const float* forecastTemp,
		const float* forecastRain,
		int forecastHours,
		int forecastStartHour,
		bool weatherDataValid,
		float moonPhase
	) {
	display.setPartialWindow(0, 0, display.width(), display.height());
	display.firstPage();
	do {
		display.fillScreen(GxEPD_WHITE);
		drawWeatherForecast(display, forecastTemp, forecastRain, forecastHours, forecastStartHour, sunriseTime, sunsetTime, weatherDataValid);

		int screenW = display.width();
		int screenH = display.height();
		int bottomH = std::max(64, screenH / 5);
		int bottomY = screenH - bottomH - 16;

		int icons = 7;
		int iconSize = 56;
		int gap = (screenW - icons * iconSize) / (icons + 1);
		int iconY = bottomY + 2;
		display.setFont(&FreeSans18pt7b);
		
		for (int i = 0; i < icons; i++) {
			int ix = gap + i * (iconSize + gap);
			int valY = iconY + iconSize + 18;
			String v;
			switch (i) {
				case 0: v = String(tempAir, 1) + "C"; break;
				case 1: v = String(humidity, 0) + "%"; break;
				case 2: v = sunriseTime; break;
				case 3: v = " "; break;
				case 4: v = sunsetTime; break;
				case 5: v = String(co2,0); break;
				case 6: v = String(pressure,0); break;
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
				case 3: {
					int moonIconIndex = static_cast<int>(moonPhase * 24.0f) % 24;
					display.drawBitmap(iconDrawX, iconDrawY+25, epd_bitmap_allArray[moonIconIndex], iconW, iconH, GxEPD_BLACK);
					break;
				}
				case 4: display.drawBitmap(iconDrawX, iconDrawY, epd_bitmap_sunset, iconW, iconH, GxEPD_BLACK); break;
				case 5: display.drawBitmap(iconDrawX, iconDrawY, epd_bitmap_co2, iconW, iconH, GxEPD_BLACK); break;
				case 6: display.drawBitmap(iconDrawX, iconDrawY, epd_bitmap_pressure, iconW, iconH, GxEPD_BLACK); break;
			}
		}
	} while (display.nextPage());
}
