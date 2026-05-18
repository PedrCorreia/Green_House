#include <Arduino.h>
#include <HardwareSerial.h>

// ---------- RN2483 / LoRa pins ----------
// Use the same wiring style as the Gateway code.
// Change these only if your Light Node uses different pins.
#define LORA_UART_RX_PIN 18    // ESP32 RX <- RN2483 TX
#define LORA_UART_TX_PIN 19    // ESP32 TX -> RN2483 RX
#define RN2483_RST_PIN   23

// ---------- LoRa radio settings ----------
// Must match the Gateway.
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
#define LORA_RETRY_INTERVAL 5000UL

// ---------- Protocol ----------
// Packet format: 4 bytes device ID + 1 byte command/status.
// For this light node:
//   0000000501 = light ON or ACK OK
//   0000000500 = light OFF or ACK FAIL
const uint32_t lightNodeId = 5UL;

// command value
const uint8_t cmdOff = 0x00;
const uint8_t cmdOn  = 0x01;

// result value inside ACK packet
const uint8_t ackFail = 0x00;
const uint8_t ackOk   = 0x01;

// external LED pin
const int ledPin = 2;

// ACK packet format: [4 bytes device ID, 1 byte result]
uint8_t ack[5];

HardwareSerial loraSerial(1);
bool loraReady = false;
unsigned long lastLoRaRetryAttempt = 0;


// ---------- Function declarations ----------
bool initLoRa();
void retryLoRaIfNeeded();
String sendLoRaCommand(const String& cmd, int timeoutMs);
String readLoRaLine(int timeoutMs);
void flushLoRaInput();
String bytesToHex(const uint8_t* data, size_t len);

bool hexToBytes(const String& hexStr, uint8_t* out, size_t outLen);
int hexNibble(char c);
uint32_t getDeviceId(uint8_t packet[5]);

void setLed(bool on);
void makeAck(uint8_t result);
void sendAck();
void handleCmd(uint8_t cmd[5]);
void listenForCmd();


// ---------- LED control ----------
void setLed(bool on) {
  if (on) {
    digitalWrite(ledPin, HIGH);
    Serial.println("Light ON");
  } else {
    digitalWrite(ledPin, LOW);
    Serial.println("Light OFF");
  }
}

// build ACK packet after handling CMD
void makeAck(uint8_t result) {
  // Put this node's ID in the ACK so the Gateway knows who replied.
  ack[0] = (uint8_t)((lightNodeId >> 24) & 0xFF);
  ack[1] = (uint8_t)((lightNodeId >> 16) & 0xFF);
  ack[2] = (uint8_t)((lightNodeId >> 8) & 0xFF);
  ack[3] = (uint8_t)(lightNodeId & 0xFF);
  ack[4] = result;
}

// Send ACK back to the Gateway by LoRa.
void sendAck() {
  if (!loraReady) {
    Serial.println("ACK TX skipped: LoRa not ready.");
    return;
  }

  // Give Gateway a short time to switch from TX mode to RX mode.
  delay(300);

  String hexPayload = bytesToHex(ack, 5);
  String cmd = String("radio tx ") + hexPayload;

  Serial.print("Sending ACK by LoRa: ");
  Serial.println(hexPayload);

  String first = sendLoRaCommand(cmd, 2000);

  if (first != "ok") {
    Serial.println("ACK TX failed: RN2483 did not accept command.");
    Serial.print("ACK TX failed reason: ");
    Serial.println(first.length() ? first : "<timeout>");
    return;
  }

  String done = readLoRaLine(10000);

  Serial.print("RN2483 => ");
  Serial.println(done.length() ? done : "<timeout>");

  if (done == "radio_tx_ok") {
    Serial.println("ACK sent.");
  } else {
    Serial.println("ACK TX not completed.");
    Serial.print("ACK TX failed reason: ");
    Serial.println(done.length() ? done : "<timeout>");
  }
}

// handle the CMD packet
// format: [4 bytes device ID, 1 byte command value]
void handleCmd(uint8_t cmd[5]) {
  uint32_t deviceId = getDeviceId(cmd);

  // LoRa P2P packets can be heard by all nodes using the same radio settings.
  // If this packet is not for device ID 5, ignore it and do not send ACK FAIL.
  if (deviceId != lightNodeId) {
    Serial.print("Packet for other device ID: ");
    Serial.println(deviceId);
    return;
  }

  Serial.print("CMD value: 0x");
  Serial.println(cmd[4], HEX);

  // digitalWrite has no success return value.
  // ACK OK means this node received a valid CMD and executed the GPIO command.
  if (cmd[4] == cmdOn) {
    setLed(true);
    makeAck(ackOk);
    sendAck();
  }
  else if (cmd[4] == cmdOff) {
    setLed(false);
    makeAck(ackOk);
    sendAck();
  }
  else {
    // The packet is for this node, but the command value is not supported.
    Serial.println("Unknown CMD value for this light node.");
    makeAck(ackFail);
    sendAck();
  }
}


void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(ledPin, OUTPUT);
  setLed(false);

  Serial.println();
  Serial.println("=== Light Control Node starting ===");

  if (!initLoRa()) {
    Serial.println("RN2483 init failed. It will retry in loop().");
    loraReady = false;
  }

  Serial.println("Light Control Node running.");
  if (loraReady) {
    Serial.println("Waiting for CMD packet: 0000000501 = ON, 0000000500 = OFF");
  } else {
    Serial.println("LoRa is not ready yet. It will retry before listening.");
  }
}


void loop() {
  retryLoRaIfNeeded();

  if (!loraReady) {
    delay(50);
    return;
  }

  listenForCmd();
}


// ---------- Real LoRa receive part ----------
void listenForCmd() {
  if (!loraReady) {
    return;
  }

  String first = sendLoRaCommand("radio rx 0", 1500);

  if (first != "ok") {
    Serial.println("Failed to enter RX mode.");
    Serial.print("RX failed reason: ");
    Serial.println(first.length() ? first : "<timeout>");
    loraReady = false;
    delay(1000);
    return;
  }

  Serial.println("Listening for CMD...");

  String line = readLoRaLine(65000);

  if (line.startsWith("radio_rx ")) {
    String hexPayload = line.substring(9);
    hexPayload.trim();
    hexPayload.toUpperCase();

    Serial.print("Received HEX: ");
    Serial.println(hexPayload);

    // CMD packet should be 5 bytes = 10 HEX characters.
    // Wrong length may be a packet for another node, so ignore it quietly.
    if (hexPayload.length() != 10) {
      Serial.println("Wrong packet length. Ignoring.");
      return;
    }

    uint8_t cmd[5];

    if (!hexToBytes(hexPayload, cmd, 5)) {
      Serial.println("HEX decode failed. Ignoring.");
      return;
    }

    handleCmd(cmd);
  }
  else if (line == "radio_err") {
    Serial.println("No packet received. RX timeout.");
  }
  else {
    Serial.print("Unexpected RN2483 line: ");
    Serial.println(line.length() ? line : "<timeout>");
  }

  delay(200);
}


// ---------- LoRa init ----------
bool initLoRa() {
  loraReady = false;

  pinMode(RN2483_RST_PIN, OUTPUT);
  digitalWrite(RN2483_RST_PIN, HIGH);

  loraSerial.begin(LORA_BAUD_RATE, SERIAL_8N1, LORA_UART_RX_PIN, LORA_UART_TX_PIN);
  loraSerial.setTimeout(2000);

  // hard reset RN2483
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

  // raw LoRa P2P mode
  String mp = sendLoRaCommand("mac pause", 2000);
  if (mp.length() == 0 || mp == "invalid_param") {
    Serial.println("mac pause failed.");
    loraReady = false;
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
      loraReady = false;
      return false;
    }
  }

  loraReady = true;
  return true;
}

void retryLoRaIfNeeded() {
  if (loraReady) return;

  // Do not stop forever if the RN2483 is missing or not ready.
  // Keep retrying so the node can recover after wiring or power problems.
  unsigned long now = millis();
  if (now - lastLoRaRetryAttempt < LORA_RETRY_INTERVAL) return;

  lastLoRaRetryAttempt = now;
  Serial.println("LoRa init failed, retrying");

  if (initLoRa()) {
    Serial.println("LoRa reconnected");
    Serial.println("Waiting for CMD packet: 0000000501 = ON, 0000000500 = OFF");
  }
}

uint32_t getDeviceId(uint8_t packet[5]) {
  uint32_t id = 0;

  // Rebuild the 32-bit device ID from the first 4 bytes.
  id |= ((uint32_t)packet[0] << 24);
  id |= ((uint32_t)packet[1] << 16);
  id |= ((uint32_t)packet[2] << 8);
  id |= (uint32_t)packet[3];

  return id;
}


// ---------- RN2483 helper functions ----------
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

// ---------- HEX decode helpers ----------
int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool hexToBytes(const String& hexStr, uint8_t* out, size_t outLen) {
  if (hexStr.length() != outLen * 2) return false;

  for (size_t i = 0; i < outLen; i++) {
    int hi = hexNibble(hexStr.charAt(i * 2));
    int lo = hexNibble(hexStr.charAt(i * 2 + 1));

    if (hi < 0 || lo < 0) return false;

    out[i] = (uint8_t)((hi << 4) | lo);
  }

  return true;
}
