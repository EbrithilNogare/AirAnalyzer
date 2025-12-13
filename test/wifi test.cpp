#include <Arduino.h>
#include <WiFi.h>
#include "../include/config.h"

// WiFi power levels available on ESP32
const wifi_power_t powerLevels[] = {
  WIFI_POWER_MINUS_1dBm,   // -1 dBm
  WIFI_POWER_2dBm,          // 2 dBm
  WIFI_POWER_5dBm,          // 5 dBm
  WIFI_POWER_7dBm,          // 7 dBm
  WIFI_POWER_8_5dBm,        // 8.5 dBm
  WIFI_POWER_11dBm,         // 11 dBm
  WIFI_POWER_13dBm,         // 13 dBm
  WIFI_POWER_15dBm,         // 15 dBm
  WIFI_POWER_17dBm,         // 17 dBm
  WIFI_POWER_18_5dBm,       // 18.5 dBm
  WIFI_POWER_19dBm,         // 19 dBm
  WIFI_POWER_19_5dBm        // 19.5 dBm (maximum)
};

const char* powerLevelNames[] = {
  "-1dBm", "2dBm", "5dBm", "7dBm", "8.5dBm", "11dBm",
  "13dBm", "15dBm", "17dBm", "18.5dBm", "19dBm", "19.5dBm"
};

const int NUM_POWER_LEVELS = sizeof(powerLevels) / sizeof(powerLevels[0]);

struct PowerLevelStats {
  int successfulConnects = 0;
  int unsuccessfulConnects = 0;
  unsigned long minConnectTime = ULONG_MAX;
  unsigned long maxConnectTime = 0;
  unsigned long totalConnectTime = 0;
};

PowerLevelStats stats[NUM_POWER_LEVELS];
int totalLoops = 0;
int currentPowerIndex = 0;

void setup() {
  Serial.begin(115200);
  while(!Serial) {
    delay(10);
  }
  
  Serial.println("\n\n========================================");
  Serial.println("  WiFi Signal Strength Test Program");
  Serial.println("========================================");
  Serial.print("Testing connection to: ");
  Serial.println(WIFI_SSID);
  Serial.println("Testing all power levels...\n");
  
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_MAX_MODEM);
}

bool testWiFiConnection(wifi_power_t powerLevel, unsigned long &connectTime) {
  // Fully disconnect and turn off WiFi
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  
  // Wait 5 seconds with WiFi off
  delay(5000);
  
  // Turn WiFi back on
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  delay(100);
  
  WiFi.setTxPower(powerLevel);
  
  unsigned long startTime = millis();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  // Try to connect for 10 seconds
  unsigned long timeout = 10000;
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeout) {
    delay(50);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    connectTime = millis() - startTime;
    return true;
  }
  
  return false;
}

void printStatistics() {
  Serial.println("\n========================================");
  Serial.print("COMPLETED LOOPS: ");
  Serial.println(totalLoops);
  Serial.println("========================================\n");
  
  Serial.println("Power Level | Success | Failed | Success% | Min(ms) | Avg(ms) | Max(ms)");
  Serial.println("------------+---------+--------+----------+---------+---------+--------");
  
  for (int i = 0; i < NUM_POWER_LEVELS; i++) {
    PowerLevelStats &s = stats[i];
    int totalAttempts = s.successfulConnects + s.unsuccessfulConnects;
    
    if (totalAttempts == 0) continue;
    
    float successPercent = (s.successfulConnects * 100.0) / totalAttempts;
    unsigned long avgTime = s.successfulConnects > 0 ? s.totalConnectTime / s.successfulConnects : 0;
    
    // Power level name
    Serial.printf("%-11s | ", powerLevelNames[i]);
    
    // Success count
    Serial.printf("%7d | ", s.successfulConnects);
    
    // Failed count
    Serial.printf("%6d | ", s.unsuccessfulConnects);
    
    // Success percentage
    Serial.printf("%7.1f%% | ", successPercent);
    
    // Min time
    if (s.successfulConnects > 0) {
      Serial.printf("%7lu | ", s.minConnectTime);
    } else {
      Serial.printf("    N/A | ");
    }
    
    // Avg time
    if (s.successfulConnects > 0) {
      Serial.printf("%7lu | ", avgTime);
    } else {
      Serial.printf("    N/A | ");
    }
    
    // Max time
    if (s.successfulConnects > 0) {
      Serial.printf("%7lu\n", s.maxConnectTime);
    } else {
      Serial.printf("    N/A\n");
    }
  }
  
  Serial.println("========================================\n");
}

void loop() {
  wifi_power_t currentPower = powerLevels[currentPowerIndex];
  const char* powerName = powerLevelNames[currentPowerIndex];
  
  Serial.print("Testing ");
  Serial.print(powerName);
  Serial.print("... ");
  
  unsigned long connectTime = 0;
  bool connected = testWiFiConnection(currentPower, connectTime);
  
  if (connected) {
    stats[currentPowerIndex].successfulConnects++;
    stats[currentPowerIndex].totalConnectTime += connectTime;
    
    if (connectTime < stats[currentPowerIndex].minConnectTime) {
      stats[currentPowerIndex].minConnectTime = connectTime;
    }
    if (connectTime > stats[currentPowerIndex].maxConnectTime) {
      stats[currentPowerIndex].maxConnectTime = connectTime;
    }
    
    Serial.print("SUCCESS (");
    Serial.print(connectTime);
    Serial.println(" ms)");
  } else {
    stats[currentPowerIndex].unsuccessfulConnects++;
    Serial.println("FAILED (timeout)");
  }
  
  // Move to next power level
  currentPowerIndex++;
  
  // If we completed all power levels, increment loop counter and print statistics
  if (currentPowerIndex >= NUM_POWER_LEVELS) {
    currentPowerIndex = 0;
    totalLoops++;
    printStatistics();
  }
  
  delay(500); // Small delay between tests
}
