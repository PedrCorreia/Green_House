/*
 * Node Device ID Utility
 * 
 * Instructions:
 * 1. Upload this sketch to your MCU
 * 2. Open the Serial monitor at 115200 baud
 * 3. The script will output the factory-programmed unique ID.
 *    Use this value as the "Device ID" parameter in your LoRa node tests and Blynk dashboard routing.
 */

#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }
  
  Serial.println("\n-----------------------------------");
  Serial.println("  Node Device ID Retrieval Utility");
  Serial.println("-----------------------------------");

#if defined(ESP8266)
  uint32_t chipId = ESP.getChipId();
  Serial.printf("MCU Type   : ESP8266\n");
  Serial.printf("Device ID  : %lu\n", chipId);
  Serial.printf("Hex Format : 0x%08X\n", chipId);

#elif defined(ESP32)
  uint64_t chipMac = ESP.getEfuseMac(); 
  
  // Extract the lower 32-bits to match your 4-byte uint32_t requirement from the Communications Plan
  uint32_t deviceId = (uint32_t)(chipMac >> 16); 

  Serial.printf("MCU Type   : ESP32\n");
  Serial.printf("Full MAC   : %04X%08X\n", (uint16_t)(chipMac >> 32), (uint32_t)chipMac);
  Serial.printf("Device ID  : %lu\n", deviceId);
  Serial.printf("Hex Format : 0x%08X\n", deviceId);

#else
  Serial.println("ERROR: Unknown MCU architecture.");
  Serial.println("This script supports ESP8266 and ESP32.");
#endif

  Serial.println("-----------------------------------");
  Serial.println("Use the 'Hex Format' value when populating your payload.deviceId in test_lora_p2p");
}

void loop() {
  // Do nothing
}
