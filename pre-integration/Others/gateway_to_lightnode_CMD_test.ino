/*
 * Gateway CMD Sender Test
 *
 * Board: ESP32 DevKit / WROOM
 * Module: RN2483
 *
 * Purpose:
 * - Test Gateway -> Light Control Node LoRa CMD sending
 * - No WiFi
 * - No MQTT
 * - No ACK yet
 *
 * Serial Monitor:
 * - Type '1' to send light ON CMD  -> 0201
 * - Type '0' to send light OFF CMD -> 0200
 * - Type 'p' to send PING          -> 50494E47
 */

#include <Arduino.h>
#include <HardwareSerial.h>

// ---------- Pins ----------
// Change these three pins based on your real wiring.
#define LORA_UART_RX_PIN 16    // ESP32 RX <- RN2483 TX
#define LORA_UART_TX_PIN 17    // ESP32 TX -> RN2483 RX
#define RN2483_RST_PIN   14

// ---------- LoRa radio settings ----------
// These MUST match the Light Control Node.
#define LORA_BAUD_RATE   57600UL
#define LORA_FREQ        868100000UL
#define LORA_PWR         14
#define LORA_SF          "sf12"
#define LORA_AFCBW       "41.7"
#define LORA_RXBW        125
#define LORA_PRLEN       8
#define LORA_CRC         "on"
#define LORA_IQI         "off"
#define LORA_CR          "4/5"
#define LORA_SYNC        "12"
#define LORA_BW          125
#define LORA_WDT         60000UL

// ---------- Protocol ----------
#define MSG_TYPE_CMD     0x02
#define CMD_LIGHT_OFF    0x00
#define CMD_LIGHT_ON     0x01

HardwareSerial loraSerial(1);

bool loraReady = false;

// ---------- Function declarations ----------
bool initLoRa();
String sendLoRaCommand(const String& cmd, int timeoutMs);
String readLoRaLine(int timeoutMs);
bool waitForAck();
void flushLoRaInput();

bool sendLightCmd(bool turnOn);
bool sendRadioTxHex(const String& hexPayload, const char* label);
String bytesToHex(const uint8_t* data, size_t len);

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=== Gateway CMD Sender Test ===");

  if (!initLoRa()) {
    Serial.println("RN2483 init failed. Stop here.");
    while (true) {
      delay(1000);
    }
  }

  Serial.println();
  Serial.println("Gateway CMD sender ready.");
  Serial.println("Type:");
  Serial.println("  1 = send light ON  CMD 0201");
  Serial.println("  0 = send light OFF CMD 0200");
  Serial.println("  p = send PING 50494E47");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();

    if (c == '1') {
      sendLightCmd(true);
    }
    else if (c == '0') {
      sendLightCmd(false);
    }
    else if (c == 'p' || c == 'P') {
      sendRadioTxHex("50494E47", "PING");
    }
  }
}

// ---------- LoRa init ----------
bool initLoRa() {
  pinMode(RN2483_RST_PIN, OUTPUT);
  digitalWrite(RN2483_RST_PIN, HIGH);

  loraSerial.begin(LORA_BAUD_RATE, SERIAL_8N1, LORA_UART_RX_PIN, LORA_UART_TX_PIN);
  loraSerial.setTimeout(2000);

  // Hard reset RN2483
  digitalWrite(RN2483_RST_PIN, LOW);
  delay(200);
  digitalWrite(RN2483_RST_PIN, HIGH);
  delay(500);

  flushLoRaInput();

  String banner = readLoRaLine(500);
  if (banner.length() > 0) {
    Serial.print("RN2483 banner: ");
    Serial.println(banner);
  }

  String ver = sendLoRaCommand("sys get ver", 1500);
  Serial.print("RN2483 version: ");
  Serial.println(ver);

  // Disable LoRaWAN MAC layer, use raw LoRa P2P.
  String mp = sendLoRaCommand("mac pause", 2000);
  if (mp.length() == 0 || mp == "invalid_param") {
    Serial.println("mac pause failed.");
    return false;
  }

  String cmds[] = {
    "radio set mod lora",
    String("radio set freq ")  + String(LORA_FREQ),
    String("radio set pwr ")   + String(LORA_PWR),
    String("radio set sf ")    + String(LORA_SF),
    String("radio set afcbw ") + String(LORA_AFCBW),
    String("radio set rxbw ")  + String(LORA_RXBW),
    String("radio set prlen ") + String(LORA_PRLEN),
    String("radio set crc ")   + String(LORA_CRC),
    String("radio set iqi ")   + String(LORA_IQI),
    String("radio set cr ")    + String(LORA_CR),
    String("radio set wdt ")   + String(LORA_WDT),
    String("radio set sync ")  + String(LORA_SYNC),
    String("radio set bw ")    + String(LORA_BW)
  };

  for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
    String r = sendLoRaCommand(cmds[i], 2000);

    if (r != "ok") {
      Serial.print("RN2483 rejected: ");
      Serial.print(cmds[i]);
      Serial.print(" -> ");
      Serial.println(r.length() ? r : "<timeout>");
      return false;
    }
  }

  loraReady = true;
  return true;
}

// ---------- Send light CMD ----------
bool sendLightCmd(bool turnOn) {
  uint8_t packet[2];

  // First byte: message type.
  // 0x02 means this packet is a command packet.
  packet[0] = 0x02;

  // Second byte: command value.
  // 0x01 means light ON.
  // 0x00 means light OFF.
  if (turnOn) {
    packet[1] = 0x01;
  } else {
    packet[1] = 0x00;
  }

  // Convert the 2-byte packet into HEX string.
  // Example: [0x02, 0x01] becomes "0201".
  String hexPayload = bytesToHex(packet, 2);

  // Send the HEX payload through RN2483.
  String cmd = String("radio tx ") + hexPayload;

  String first = sendLoRaCommand(cmd, 2000);

  if (first != "ok") {
    Serial.println("Light CMD failed: RN2483 did not accept the TX command.");
    return false;
  }

  String done = readLoRaLine(10000);

  if (done == "radio_tx_ok") {
    Serial.println("Light CMD sent.");
    return true;
  } else {
    Serial.println("Light CMD failed: TX was not completed.");
    return false;
  }
}

// Send one raw LoRa packet by HEX payload.
// Example:
//   0201 = light ON
//   0200 = light OFF
bool sendRadioTxHex(const String& hexPayload, const char* label) {
  if (!loraReady) {
    Serial.println("LoRa not ready.");
    return false;
  }

  Serial.print("Sending ");
  Serial.print(label);
  Serial.print(": ");
  Serial.println(hexPayload);

  String cmd = String("radio tx ") + hexPayload;

  String first = sendLoRaCommand(cmd, 2000);

  if (first != "ok") {
    Serial.print("TX rejected by RN2483: ");
    Serial.println(first.length() ? first : "<timeout>");
    return false;
  }

  String done = readLoRaLine(10000);

  Serial.print("RN2483 => ");
  Serial.println(done.length() ? done : "<timeout>");

if (done == "radio_tx_ok") {
  Serial.println("Light CMD sent.");
  return waitForAck();
} else {
  Serial.println("Light CMD failed.");
  return false;
}
}

bool waitForAck() {
  Serial.println("Waiting for ACK...");

  String first = sendLoRaCommand("radio rx 0", 1500);

  if (first != "ok") {
    Serial.println("Gateway failed to enter RX mode.");
    return false;
  }

  String line = readLoRaLine(5000);

  if (line.startsWith("radio_rx ")) {
    String hexPayload = line.substring(9);
    hexPayload.trim();
    hexPayload.toUpperCase();

    Serial.print("ACK received HEX: ");
    Serial.println(hexPayload);

    if (hexPayload == "0301") {
      Serial.println("ACK OK: command received and executed.");
      return true;
    }
    else if (hexPayload == "0300") {
      Serial.println("ACK FAIL: node received command but failed to handle it.");
      return false;
    }
    else {
      Serial.println("Unknown ACK packet.");
      return false;
    }
  }
  else if (line == "radio_err") {
    Serial.println("No ACK received.");
    return false;
  }
  else {
    Serial.print("Unexpected ACK response: ");
    Serial.println(line.length() ? line : "<timeout>");
    return false;
  }
}


// ---------- RN2483 command helpers ----------
String sendLoRaCommand(const String& cmd, int timeoutMs) {
  Serial.print("RN2483 <= ");
  Serial.println(cmd);

  loraSerial.print(cmd);
  loraSerial.print("\r\n");

  String resp = readLoRaLine(timeoutMs);

  Serial.print("RN2483 => ");
  Serial.println(resp.length() ? resp : "<timeout>");

  return resp;
}

String readLoRaLine(int timeoutMs) {
  String line;
  unsigned long start = millis();

  while (millis() - start < (unsigned long)timeoutMs) {
    while (loraSerial.available()) {
      char c = (char)loraSerial.read();

      if (c == '\r') continue;

      if (c == '\n') {
        line.trim();
        return line;
      }

      line += c;
    }

    delay(1);
  }

  line.trim();
  return line;
}

void flushLoRaInput() {
  while (loraSerial.available()) {
    loraSerial.read();
  }
}

// ---------- Convert bytes to HEX string ----------
String bytesToHex(const uint8_t* data, size_t len) {
  static const char hexChars[] = "0123456789ABCDEF";

  String s;
  s.reserve(len * 2);

  for (size_t i = 0; i < len; i++) {
    s += hexChars[(data[i] >> 4) & 0x0F];
    s += hexChars[data[i] & 0x0F];
  }

  return s;
}