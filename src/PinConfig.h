#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

/**
 * GPIO config
**/
#define POWER_SENSING_PIN  2  // Power sniffing

#define EPD_TRANSISTOR_PIN 20  // BC327 transistor control for display power
#define EPD_PWR_PIN    9  // Power control
#define EPD_BUSY_PIN   3  // Busy signal from display
#define EPD_RST_PIN    4  // Reset
#define EPD_DC_PIN     5  // Data/Command
#define EPD_CS_PIN    21  // Chip Select
#define EPD_SCK_PIN    8  // SPI Clock (standard ESP32C3 SPI)
#define EPD_MOSI_PIN  10  // SPI MOSI (standard ESP32C3 SPI)

// I2C pins for sensors (AHT30, SDC40)
#define I2C_SDA_PIN    6  // I2C SDA
#define I2C_SCL_PIN    7  // I2C SCL

#define BATTERY_AVERAGE_SAMPLES 64  // Number of samples to average for battery voltage reading
#define VOLTAGE_DIVIDER_RATIO 1.983f // Voltage divider ratio for battery voltage measurement

#endif
