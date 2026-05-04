/*
 * LoRa RN2483 Deep Sleep Sender Test
 * 
 * Hardware:
 * - ESP32
 * - Microchip RN2483A (DrAzzy breakout)
 * 
 * Instructions:
 * 1. Upload this sketch to your Sensor Node.
 * 2. Open the Serial Monitor at 115200 baud.
 * 3. The board will transmit a packet, configure the LoRa module 
 *    to sleep, and then put the ESP32 to sleep for 10 seconds.
 * 4. It will automatically wake up and repeat this process.
 * 
 * Note: To stop the loop, you will need to upload a different sketch.
 */

#include <Arduino.h>

#define LORA_RX_PIN 16
#define LORA_TX_PIN 17
#define LORA_RST_PIN 14

#define LORA_BAUD 57600

// Synthetic structure (13 bytes total)
struct SensorPayload {
  uint32_t deviceId; // 4 bytes
  int16_t  temp;     // 2 bytes
  uint8_t  hum;      // 1 byte
  uint16_t lux;      // 2 bytes
  uint16_t soil;     // 2 bytes
  uint8_t  leak;     // 1 byte
  uint8_t  bat;      // 1 byte
} __attribute__((packed));

RTC_DATA_ATTR int bootCount = 0; // Stored in RTC memory to survive deep sleep

const uint64_t uS_TO_S_FACTOR = 1000000;  // Conversion factor for micro seconds to seconds
const uint64_t TIME_TO_SLEEP  = 10;       // Deep sleep duration in seconds

String toHex(uint8_t val) {
  String out = String(val, HEX);
  if (out.length() < 2) out = "0" + out;
  return out;
}

void resetLoRa() {
  digitalWrite(LORA_RST_PIN, LOW);
  delay(100);
  digitalWrite(LORA_RST_PIN, HIGH);
  delay(500);
  while(Serial2.available()) Serial2.read(); // flush buffers
}

void sendCmdExpected(String cmd, String expected) {
  Serial.print("Sending: "); Serial.println(cmd);
  Serial2.print(cmd + "\r\n");
  delay(100);
  while(Serial2.available()) {
    String resp = Serial2.readStringUntil('\n');
    resp.trim();
    Serial.print("Resp: "); Serial.println(resp);
  }
}

void sendSyntheticData() {
  Serial.println("\nGenerating and sending synthetic payload...");
  
  // Prepare 13-bytes synthetic payload
  SensorPayload payload;
  payload.deviceId = 0xAA112233;   // Device ID
  payload.temp = 2450;             // Temp: 24.50 C
  payload.hum = 65;                // Humidity: 65%
  payload.lux = 1200;              // Lux: 1200
  payload.soil = 1500;             // Soil voltage: 1500
  payload.leak = 0;                // Leak: false
  payload.bat = 85;                // Battery: 85%

  // Serialize to hex string
  uint8_t* ptr = (uint8_t*)&payload;
  String hexPayload = "";
  for (int i = 0; i < sizeof(SensorPayload); i++) {
    hexPayload += toHex(ptr[i]);
  }

  Serial.print("Payload Hex: ");
  Serial.println(hexPayload);
  
  Serial2.print("radio tx " + hexPayload + "\r\n");
  delay(100);
  
  long startTime = millis();
  while (millis() - startTime < 3000) {
    if (Serial2.available()) {
      String resp = Serial2.readStringUntil('\n');
      resp.trim();
      if(resp.length() > 0) {
        Serial.println("  -> " + resp);
      }
      if (resp == "radio_tx_ok") break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }
  
  bootCount++;
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.printf("\n*** Woke up from Deep Sleep! (Boot %d) ***\n", bootCount);
  } else {
    Serial.printf("\n*** Cold Boot! (Boot %d) ***\n", bootCount);
  }

  Serial2.begin(LORA_BAUD, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  pinMode(LORA_RST_PIN, OUTPUT);
  resetLoRa();

  Serial.println("Initialising RN2483 for Point-to-Point (P2P)...");
  
  // Pause LoRaWAN MAC to use raw radio
  sendCmdExpected("mac pause", "4294967295"); 
  
  // Fire the message
  sendSyntheticData();
  
  // Put LoRa module to sleep
  Serial.println("Putting RN2483 to sleep...");
  Serial2.print("sys sleep ");
  Serial2.println(TIME_TO_SLEEP * 1000); 
  delay(100);

  // Put ESP32 to sleep
  Serial.printf("Putting ESP32 to sleep for %d seconds...\n", (int)TIME_TO_SLEEP);
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {
  // Execution never reaches here because esp_deep_sleep_start() halts the processor
  // and restarts from setup() when waking up.
}
