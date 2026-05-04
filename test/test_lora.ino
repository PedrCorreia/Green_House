/*
 * LoRa RN2483 Module Test Script
 * 
 * Hardware:
 * - ESP32
 * - Microchip RN2483A (DrAzzy breakout)
 * 
 * Connections:
 * ESP32 GPIO 16 -> RN2483 RX (UART2 RX)
 * ESP32 GPIO 17 -> RN2483 TX (UART2 TX)
 * ESP32 GPIO 14 -> RN2483 RTS (LORA_RST)
 * 
 * Instructions:
 * 1. Upload this sketch.
 * 2. Open the Serial Monitor at 115200 baud.
 * 3. Ensure both 'Both NL & CR' are selected in the Serial Monitor.
 * 4. The script will boot the RN2483 module and output its version.
 * 5. You can type RN2483 commands (e.g., "sys get ver", "sys get default_env") in the 
 *    Serial Monitor, which will be forwarded to the module, and the response will 
 *    be printed back.
 */

#include <Arduino.h>

#define LORA_RX_PIN 16
#define LORA_TX_PIN 17
#define LORA_RST_PIN 14

// RN2483 Default Baud Rate
#define LORA_BAUD 57600

void setup() {
  // Initialize PC Serial Monitor
  Serial.begin(115200);
  while (!Serial) { ; }
  
  Serial.println("\n--- RN2483 LoRa Module Test ---");

  // Initialize Hardware Serial 2 for the LoRa module
  Serial2.begin(LORA_BAUD, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

  // Configure Reset Pin
  pinMode(LORA_RST_PIN, OUTPUT);
  
  // Hard reset the RN2483 module
  Serial.println("Resetting LoRa module...");
  digitalWrite(LORA_RST_PIN, LOW);
  delay(100);
  digitalWrite(LORA_RST_PIN, HIGH);
  delay(100);

  Serial.println("Waiting for RN2483 to boot...");
  
  // Wait for the startup message
  delay(500);
  while (Serial2.available()) {
    Serial.write(Serial2.read());
  }

  // Ask for version just in case auto-baud was needed or boot message was missed
  Serial.println("\nSending 'sys get ver'...");
  Serial2.print("sys get ver\r\n");
  delay(100);
  while (Serial2.available()) {
    Serial.write(Serial2.read());
  }

  Serial.println("\n\nReady. Type RN2483 commands and press enter.");
}

void loop() {
  // Read from ESP32 Serial Monitor and send to RN2483
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim(); // Remove whitespace/newline
    
    if (command.length() > 0) {
      Serial.print("Sending: ");
      Serial.println(command);
      
      // RN2483 commands must be terminated with \r\n
      Serial2.print(command + "\r\n");
    }
  }

  // Read from RN2483 and send to ESP32 Serial Monitor
  if (Serial2.available()) {
    Serial.write(Serial2.read());
  }
}
