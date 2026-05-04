/*
 * LoRa RN2483 P2P Node & Gateway Synthetic Data Test
 * 
 * Hardware:
 * - ESP32
 * - Microchip RN2483A (DrAzzy breakout)
 * 
 * Instructions:
 * 1. Upload this sketch.
 * 2. Open the Serial Monitor at 115200 baud.
 * 3. Type 'S' to transmit a synthetic 13-byte sensor payload (Node mode).
 * 4. Type 'R' to enter continuous receive mode (Gateway mode).
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

enum RunMode { MODE_IDLE, MODE_SENDER, MODE_RECEIVER, MODE_SLEEP_SENDER, MODE_SLEEP_RECEIVER };
RTC_DATA_ATTR RunMode currentMode = MODE_IDLE; // Stored in RTC memory to survive deep sleep
RTC_DATA_ATTR int bootCount = 0;

// Time tracking for drift calculation across sleep cycles
RTC_DATA_ATTR uint64_t totalDriftUs = 0;
RTC_DATA_ATTR uint64_t lastRxTimestampUs = 0; 
unsigned long serialListenStartTime = 0;
unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 5000; // Send every 5 seconds
const uint64_t uS_TO_S_FACTOR = 1000000;  // Conversion factor for micro seconds to seconds
const uint64_t TIME_TO_SLEEP  = 10;       // Deep sleep duration in seconds

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }
  
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    if (currentMode == MODE_SLEEP_SENDER || currentMode == MODE_SLEEP_RECEIVER) {
      bootCount++;
      Serial.printf("\n*** Woke up from Deep Sleep! (Boot %d) ***\n", bootCount);
    }
  } else {
    // Only reset to IDLE if we didn't just consciously switch modes via the Serial menu
    if (currentMode != MODE_SLEEP_SENDER && currentMode != MODE_SLEEP_RECEIVER) {
      currentMode = MODE_IDLE;
      bootCount = 0;
    } else {
      Serial.println("\n*** Starting First Cycle of Deep Sleep Mode... ***");
    }
  }

  Serial2.begin(LORA_BAUD, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  pinMode(LORA_RST_PIN, OUTPUT);
  resetLoRa();

  if (currentMode != MODE_SLEEP_SENDER && currentMode != MODE_SLEEP_RECEIVER) {
    Serial.println("Initialising RN2483 for Point-to-Point (P2P)...");
  }
  
  // Pause LoRaWAN MAC to use raw radio
  sendCmdExpected("mac pause", "4294967295"); 
  
  // If in Deep Sleep Sender mode, fire the message and immediately sleep!
  if (currentMode == MODE_SLEEP_SENDER) {
    sendSyntheticData();
    
    Serial.println("Putting RN2483 to sleep...");
    Serial2.print("sys sleep ");
    Serial2.println(TIME_TO_SLEEP * 1000); // Put LoRa module to sleep
    delay(100);

    Serial.printf("Putting ESP32 to sleep for %d seconds...\n", (int)TIME_TO_SLEEP);
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    Serial.flush();
    esp_deep_sleep_start();
  }
  
  // If in Deep Sleep Receiver mode, start receiving but don't sleep immediately. 
  // We'll enter sleep in loop() after receiving or timing out.
  if (currentMode == MODE_SLEEP_RECEIVER) {
    Serial.println("\n*** SLEEP GATEWAY: Starting 6s Listen Window ***");
    startReceiving();
    serialListenStartTime = millis(); 
  }
  
  if (currentMode == MODE_IDLE) {
    printMenu();
  }
}

void enterGatewaySleep() {
  Serial.println("SLEEP GATEWAY: Putting RN2483 to sleep...");
  Serial2.print("sys sleep ");
  Serial2.println(TIME_TO_SLEEP * 1000); 
  delay(100);

  Serial.printf("SLEEP GATEWAY: Putting ESP32 to sleep for %d seconds...\n", (int)TIME_TO_SLEEP);
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {
  // Check for Serial Monitor input to switch modes
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'S' || c == 's') {
      currentMode = MODE_SENDER;
      Serial.println("\n*** Started CONTINUOUS SENDER Mode (Every 5s) ***");
      sendSyntheticData();
      lastSendTime = millis();
    } 
    else if (c == 'R' || c == 'r') {
      currentMode = MODE_RECEIVER;
      Serial.println("\n*** Started CONTINUOUS RECEIVER Mode ***");
      startReceiving();
    }
    else if (c == 'D' || c == 'd') {
      currentMode = MODE_SLEEP_SENDER;
      Serial.println("\n*** Started DEEP SLEEP SENDER Mode (10s interval) ***");
      
      Serial.println("Rebooting in 1s to start sleep cycle...");
      delay(1000);
      ESP.restart(); 
    }
    else if (c == 'G' || c == 'g') {
      currentMode = MODE_SLEEP_RECEIVER;
      Serial.println("\n*** Started DEEP SLEEP RECEIVER Mode (Gateway - 10s interval) ***");
      
      Serial.println("Rebooting in 1s to start sleep cycle...");
      delay(1000);
      ESP.restart(); 
    }
    else if (c == 'I' || c == 'i') {
      currentMode = MODE_IDLE;
      Serial.println("\n*** Stopped and IDLE ***");
      printMenu();
    }
  }

  // Automatically send data if in Sender mode
  if (currentMode == MODE_SENDER) {
    if (millis() - lastSendTime >= SEND_INTERVAL) {
      sendSyntheticData();
      lastSendTime = millis();
    }
  }

  // Check for incoming LoRa Radio messages (for Gateway mode)
  if (Serial2.available()) {
    String response = Serial2.readStringUntil('\n');
    response.trim();
    if (response.length() > 0) {
      Serial.print("[LoRa RX]: ");
      Serial.println(response);
      
      if (response.startsWith("radio_rx  ")) {
        decodePayload(response.substring(10)); // "radio_rx  " is 10 chars
        // Restart receiving if in continuous mode
        if (currentMode == MODE_RECEIVER) {
            startReceiving();
        } else if (currentMode == MODE_SLEEP_RECEIVER) {
            // In deep sleep receiver mode, data acquired. Go to sleep.
            enterGatewaySleep();
        }
      } 
      else if (response == "radio_err") {
        // radio_err happens on timeout or bad packet.
        if (currentMode == MODE_RECEIVER) {
            startReceiving();
        } else if (currentMode == MODE_SLEEP_RECEIVER) {
            Serial.println("SLEEP GATEWAY: Error/Timeout on RX.");
            // We could loop back to receive if we still have time in our window
        }
      }
    }
  }

  // Timeout logic for Sleep Gateway Mode if they stay awake too long waiting
  if (currentMode == MODE_SLEEP_RECEIVER) {
     if (millis() - serialListenStartTime > 6000) { // 6 Seconds max listening window
        Serial.println("\nSLEEP GATEWAY: 6s Listen Window expired. Returning to sleep to catch next cycle...");
        enterGatewaySleep();
     }
  }
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

void printMenu() {
  Serial.println("\n--- MENU ---");
  Serial.println("[S] Start Continuous Sender (Node - Active, 5s delay)");
  Serial.println("[D] Start Deep Sleep Sender (Node - Sleep, 10s delay)");
  Serial.println("[R] Start Continuous Receiver (Gateway - Active)");
  Serial.println("[G] Start Deep Sleep Receiver (Gateway - Sleep, 10s interval)");
  Serial.println("[I] Stop and return to Idle");
  Serial.println("------------");
}

String toHex(uint8_t val) {
  String out = String(val, HEX);
  if (out.length() < 2) out = "0" + out;
  return out;
}

void sendSyntheticData() {
  Serial.println("\n[S] Generating and sending synthetic payload...");
  
  // Prepare 13-bytes synthetic payload based on architecture doc
  SensorPayload payload;
  payload.deviceId = 0xAA112233;   // Device ID
  payload.temp = 2450;             // Temp: 24.50 C
  payload.hum = 65;                // Humidity: 65%
  payload.lux = 1200;              // Lux: 1200
  payload.soil = 1500;             // Soil voltage: 1500
  payload.leak = 0;                // Leak: false
  payload.bat = 85;                // Battery: 85%

  // Serialize to hex string (LoRa radio tx requires hex stream)
  uint8_t* ptr = (uint8_t*)&payload;
  String hexPayload = "";
  for (int i = 0; i < sizeof(SensorPayload); i++) {
    hexPayload += toHex(ptr[i]);
  }

  // Send over LoRa raw radio
  Serial.print("Payload Hex: ");
  Serial.println(hexPayload);
  
  Serial2.print("radio tx " + hexPayload + "\r\n");
  delay(100);
  
  // 'radio tx' returns 'ok' immediately, then 'radio_tx_ok' when done
  // Wait to capture these
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

void startReceiving() {
  Serial.println("\n[R] Switching to continuous receive mode...");
  // 0 means continuous receive. Module replies 'ok' then waits for 'radio_rx  <data>'
  Serial2.print("radio rx 0\r\n");
}

void decodePayload(String hexStr) {
  uint64_t currentRxTimeUs = esp_timer_get_time(); // Gets absolute uptime in microseconds since boot (survives sleep)
  
  Serial.println("\n--- Decoding payload ---");
  
  // Calculate and display Clock Drift if we have a previous packet
  if (lastRxTimestampUs > 0) {
    uint64_t deltaUs = currentRxTimeUs - lastRxTimestampUs;
    unsigned long deltaMs = deltaUs / 1000;
    
    // We assume the sender is in Deep Sleep mode (10s cycle) for calculation
    long expectedDelta = TIME_TO_SLEEP * 1000;
    long currentDriftMs = (long)deltaMs - expectedDelta;
    
    // Accumulate total drift to see macro clock sync shifting
    totalDriftUs += (deltaUs - (TIME_TO_SLEEP * 1000000));
    
    Serial.println("*** SYNCHRONIZATION DATA ***");
    Serial.printf("Time since last RX : %lu ms\n", deltaMs);
    Serial.printf("Expected interval  : %ld ms\n", expectedDelta);
    Serial.printf("Current Shift      : %ld ms (boot + radio overhead included)\n", currentDriftMs);
    Serial.printf("Total Clock Drift  : %ld ms (cumulative across sleep cycles!)\n", (long)(totalDriftUs / 1000));
    Serial.println("****************************");
  } else {
    totalDriftUs = 0; // Reset starting baseline
  }
  lastRxTimestampUs = currentRxTimeUs;

  if (hexStr.length() != sizeof(SensorPayload) * 2) {
    Serial.println("Warning: Received payload length doesn't match 13 bytes!");
  }
  
  SensorPayload payload;
  uint8_t* ptr = (uint8_t*)&payload;
  
  for (int i = 0; i < hexStr.length(); i += 2) {
    String byteStr = hexStr.substring(i, i + 2);
    ptr[i / 2] = (uint8_t) strtol(byteStr.c_str(), NULL, 16);
  }
  
  Serial.print("Device ID: 0x"); Serial.println(payload.deviceId, HEX);
  Serial.print("Temp: "); Serial.println(payload.temp);
  Serial.print("Hum:  "); Serial.print(payload.hum); Serial.println("%");
  Serial.print("Lux:  "); Serial.println(payload.lux);
  Serial.print("Soil: "); Serial.println(payload.soil);
  Serial.print("Leak: "); Serial.println(payload.leak);
  Serial.print("Bat:  "); Serial.print(payload.bat); Serial.println("%");
  Serial.println("------------------------");
}
