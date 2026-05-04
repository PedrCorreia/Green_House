#include <Arduino.h>

RTC_DATA_ATTR int bootCount = 0;
const uint64_t TIME_TO_SLEEP = 10; // 10 seconds
const uint64_t uS_TO_S_FACTOR = 1000000;

void setup() {
  Serial.begin(115200);
  delay(1000); // Wait for Serial to initialize after wake
  
  bootCount++;
  
  // Look at the timestamp the Serial Monitor attaches to this line!
  Serial.printf("PING:%d\n", bootCount);
  
  Serial.printf("Going to deep sleep for %llu seconds...\n", TIME_TO_SLEEP);
  Serial.flush();
  
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void loop() {
  // Never reached
}