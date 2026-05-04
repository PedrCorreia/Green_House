#include <Arduino.h>

// --- Hardware Pin Definitions ---
// These match the ESP32 DevKit V1 connections to the RN2483 LoRa breakout.
#define LORA_RX_PIN 16   // ESP32 RX (connects to RN2483 TX)
#define LORA_TX_PIN 17   // ESP32 TX (connects to RN2483 RX)
#define LORA_RST_PIN 14  // Pin to physically reset the LoRa module
#define LORA_BAUD 57600  // Default baud rate for RN2483

// --- Payload Definition ---
// This struct defines the exact 13-byte data packet specified in the Coms Plan.
// __attribute__((packed)) is CRITICAL here: it tells the C++ compiler NOT to 
// add empty "padding" bytes between variables to align them in memory.
// Without it, the compiler might make this struct 16 bytes instead of 13.
struct SensorPayload {
  uint32_t deviceId; // 4 bytes: [Type Flag (1 byte)][Unique Hardware ID (3 bytes)]
  int16_t  temp;     // 2 bytes: Temperature (multiplied by 10 to send as int, e.g., 245 = 24.5C)
  uint8_t  hum;      // 1 byte:  Humidity percentage
  uint16_t lux;      // 2 bytes: Light intensity
  uint16_t soil;     // 2 bytes: Soil moisture raw ADC value
  uint8_t  leak;     // 1 byte:  Boolean/0-1 for water leakage
  uint8_t  bat;      // 1 byte:  Battery percentage
} __attribute__((packed));

// --- Sleep Configuration ---
const uint64_t T_DS = 20; // Deep Sleep cycle time in seconds (Set to 4*60*60 for 4-hour deployment)
const uint64_t uS_TO_S = 1000000ULL; // Multiplier to convert seconds to microseconds for the ESP32 timer

void setup() {
  // 1. Initialize Serial monitor to PC (for debugging)
  Serial.begin(115200);
  
  // 2. Initialize Hardware Serial2 to talk to the RN2483 LoRa module
  Serial2.begin(LORA_BAUD, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  
  // --- RN2483 Hardware Reset ---
  // To ensure the module doesn't get stuck in a weird state between sleep cycles,
  // we perform a hard reset by pulling its RESET pin LOW, then HIGH.
  pinMode(LORA_RST_PIN, OUTPUT);
  digitalWrite(LORA_RST_PIN, LOW);
  delay(100); // Hold reset low for 100ms
  digitalWrite(LORA_RST_PIN, HIGH);
  delay(500); // Give the module 500ms to boot up completely

  Serial.println("\n[SENSOR] Waking up...");

  // --- LoRa Configuration ---
  // The RN2483 is a LoRaWAN module, meaning it's designed to talk to cell towers.
  // We just want simple Node-to-Gateway Point-to-Point (P2P) radio.
  // Sending "mac pause" disables the LoRaWAN stack and allows us to use raw radio commands.
  Serial2.print("mac pause\r\n");
  delay(100);

  // --- Mock Sensor Readings ---
  // Here we fill our payload struct with dummy data. 
  // In the real code, you would read from your DHT22 and Analog pins here.
  SensorPayload payload;
  payload.deviceId = 0x010000A1; // 0x01 = Sensor type, 0000A1 = Node ID
  payload.temp = 245;            // 24.5 °C
  payload.hum = 55;              // 55%
  payload.lux = 3000;            // 3000 lux
  payload.soil = 2048;           // Mid-level soil moisture
  payload.leak = 0;              // 0 = false (No leak)
  payload.bat = 85;              // 85% battery remaining

  // --- Data Serialization ---
  // The RN2483's "radio tx" command requires data to be sent as a continuous HEX string 
  // (e.g., "A1B2C3..."). We cannot just send raw binary bytes over Serial.
  String hexPayload = "";
  uint8_t* ptr = (uint8_t*)&payload; // Create a byte-pointer pointing to the start of our struct
  for (int i = 0; i < sizeof(SensorPayload); i++) {
    String out = String(ptr[i], HEX); // Convert each byte to a HEX character
    if (out.length() < 2) out = "0" + out; // Ensure single digits have a leading zero (e.g., 'F' -> '0F')
    hexPayload += out;
  }

  // --- Transmit Data ---
  Serial.println("[SENSOR] Transmitting payload: " + hexPayload);
  Serial2.print("radio tx " + hexPayload + "\r\n"); // Send command to LoRa module
  
  // Wait up to 3 seconds for the module to finish transmitting.
  // It will reply with "ok" immediately, and then "radio_tx_ok" when the packet actually leaves the antenna.
  long startTime = millis();
  while (millis() - startTime < 3000) {
    if (Serial2.available()) {
      String resp = Serial2.readStringUntil('\n'); // Read module response
      resp.trim(); // Remove \r and \n characters
      Serial.println("  -> " + resp);
      if (resp == "radio_tx_ok") break; // Transmission complete, break out of the wait loop
    }
  }

  // --- Put RN2483 to Deep Sleep ---
  // Tell the LoRa module to sleep for T_DS milliseconds. 
  // This drastically reduces power consumption from ~15mA down to ~3uA.
  Serial2.print("sys sleep ");
  Serial2.println(T_DS * 1000); 
  delay(100); // Give the command time to reach the module before the ESP32 turns off

  // --- Put ESP32 to Deep Sleep ---
  Serial.printf("[SENSOR] Deep sleeping for %llu seconds.\n", T_DS);
  Serial.flush(); // Ensure all Serial.print messages are fully sent to the PC before sleeping
  
  // Configure the internal RTC timer to wake the ESP32 up after T_DS.
  esp_sleep_enable_timer_wakeup(T_DS * uS_TO_S);
  
  // Power down the main CPU. 
  // When it wakes up, it behaves exactly like pressing the "Reset" button (it starts over at setup()).
  esp_deep_sleep_start();
}

void loop() {
  // This is intentionally left empty.
  // Because esp_deep_sleep_start() halts the processor and forces a full reboot upon waking,
  // the code will never actually reach the loop() function.
}