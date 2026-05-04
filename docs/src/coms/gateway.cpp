#include <Arduino.h>

// --- Hardware Pin Definitions ---
// Matches ESP32 DevKit V1 connections to RN2483
#define LORA_RX_PIN 16
#define LORA_TX_PIN 17
#define LORA_RST_PIN 14
#define LORA_BAUD 57600

// --- Payload Definition ---
// Must match EXACTLY with the Sensor Node struct.
// __attribute__((packed)) prevents memory padding.
struct SensorPayload {
  uint32_t deviceId; // 4 bytes
  int16_t  temp;     // 2 bytes 
  uint8_t  hum;      // 1 byte
  uint16_t lux;      // 2 bytes
  uint16_t soil;     // 2 bytes
  uint8_t  leak;     // 1 byte
  uint8_t  bat;      // 1 byte
} __attribute__((packed));

// --- Sleep Configuration ---
// Gateway MUST have the exact same sleep time as the Sensors
const uint64_t T_DS = 20; 
const uint64_t uS_TO_S = 1000000ULL;

// --- Window Configuration ---
// Gateway stays awake listening for 6 seconds. 
// Because of ESP32 RTC drift, the gateway and sensors might wake up slightly out of sync.
// Staying awake for a 6s "window" ensures the gateway catches the sensor's message.
const uint32_t RX_WINDOW_MS = 6000; 

void setup() {
  // 1. Initialize Serials
  Serial.begin(115200);
  Serial2.begin(LORA_BAUD, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

  // --- RN2483 Hardware Reset ---
  pinMode(LORA_RST_PIN, OUTPUT);
  digitalWrite(LORA_RST_PIN, LOW);
  delay(100);
  digitalWrite(LORA_RST_PIN, HIGH);
  delay(500);

  Serial.println("\n[GATEWAY] Waking up...");

  // --- LoRa Configuration ---
  // Pause LoRaWAN MAC to use raw radio
  Serial2.print("mac pause\r\n");
  delay(100);
  
  // Clear any garbage data out of the Serial buffer before we start listening
  while(Serial2.available()) Serial2.read(); 

  // --- Enter RX Window ---
  Serial.printf("[GATEWAY] Entering RX Window for %d seconds...\n", RX_WINDOW_MS/1000);
  
  // 'radio rx 0' tells the module to enter continuous receive mode until a packet is found
  Serial2.print("radio rx 0\r\n"); 
  
  uint32_t startRxTime = millis();
  bool packetReceived = false;

  // Keep looping and checking Serial2 until our 6-second window expires
  while (millis() - startRxTime < RX_WINDOW_MS) {
    if (Serial2.available()) {
      String response = Serial2.readStringUntil('\n');
      response.trim();
      
      // When the RN2483 receives raw radio data, it prints "radio_rx  <HexData>"
      if (response.startsWith("radio_rx  ")) {
        packetReceived = true;
        
        // Extract just the hex string (skip the first 10 characters "radio_rx  ")
        String hexData = response.substring(10);
        Serial.println("[GATEWAY] LoRa packet received: " + hexData);
        
        // --- Decode Data ---
        // Convert the incoming Hex string back into our C++ Struct.
        SensorPayload received;
        uint8_t* ptr = (uint8_t*)&received;
        
        // Loop through the hex string 2 characters at a time (1 byte)
        for (int i = 0; i < sizeof(SensorPayload) && i * 2 < hexData.length(); i++) {
          String byteStr = hexData.substring(i * 2, i * 2 + 2);
          // strtol converts a hex string (base 16) into an integer
          ptr[i] = (uint8_t) strtol(byteStr.c_str(), NULL, 16);
        }

        // --- Print Decoded Values ---
        Serial.println("\n--- DECODED PAYLOAD ---");
        // Print Device ID in standard Hex format
        Serial.printf("Device ID: 0x%08X\n", received.deviceId); 
        // Temperature is transmitted as an integer (e.g. 245), divide by 10 to get float (24.5)
        Serial.printf("Temp: %.1f C\n", received.temp / 10.0);
        Serial.printf("Humidity: %d%%\n", received.hum);
        Serial.printf("Lux: %d\n", received.lux);
        Serial.printf("Soil: %d\n", received.soil);
        Serial.printf("Battery: %d%%\n", received.bat);
        Serial.println("-----------------------\n");
        
        // We received a packet, so break out of the listening loop early
        break; 
      }
    }
  }

  // If the 6-second window passed and we never flipped the boolean...
  if (!packetReceived) {
    Serial.println("[GATEWAY] Timeout: No sensor packet received in window.");
  }

  // --- Put RN2483 to Deep Sleep ---
  // Put module to sleep to save power
  Serial2.print("sys sleep ");
  Serial2.println(T_DS * 1000); 
  delay(100);

  // --- Put ESP32 to Deep Sleep ---
  Serial.printf("[GATEWAY] Cycle complete. Going to deep sleep for %llu seconds.\n", T_DS);
  Serial.flush();
  
  // Set wakeup timer and sleep
  esp_sleep_enable_timer_wakeup(T_DS * uS_TO_S);
  esp_deep_sleep_start();
}

void loop() {
  // Never reached due to deep sleep
}