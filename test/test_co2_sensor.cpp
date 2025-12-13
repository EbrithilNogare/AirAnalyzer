#include <Arduino.h>
#include "	.h"
SCD4X co2;
double CO2 = 0, temperature = 0, humidity = 0;

void setup(){
	Serial.begin(115200);
	delay(1000);

	Wire.begin(6,7,100000);
	co2.begin(Wire);
	co2.setAmbientPressure(99900); // 999 hPa = 99900 Pa
	co2.startPeriodicMeasurement();
}

void loop(){
	while (co2.isDataReady() == false) {
		vTaskDelay(200 / portTICK_PERIOD_MS); //check every 200ms
		Serial.print(".");
	}
	if (co2.readMeasurement(CO2, temperature, humidity) == 0) {
		Serial.printf("\n%4.0f,%2.1f,%1.0f\n", CO2, temperature, humidity); //nice formatting of data
	} vTaskDelay(4750 / portTICK_PERIOD_MS); //new data available after approx 5 seconds
}