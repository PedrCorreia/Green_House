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

// ---------- Protocol ----------
// packet type
const uint8_t cmdType = 0x02;
const uint8_t ackType = 0x03;

// command value
const uint8_t cmdOff = 0x00;
const uint8_t cmdOn  = 0x01;

// result value inside ACK packet
const uint8_t ackFail = 0x00;
const uint8_t ackOk   = 0x01;

// external LED pin
const int ledPin = 2;

// define an ACK packet
// format: [ackType, result]
uint8_t ack[2];

HardwareSerial loraSerial(1);
bool loraReady = false;


// ---------- Function declarations ----------
bool initLoRa();
String sendLoRaCommand(const String& cmd, int timeoutMs);
String readLoRaLine(int timeoutMs);
void flushLoRaInput();

bool hexToBytes(const String& hexStr, uint8_t* out, size_t outLen);
int hexNibble(char c);

void setLed(bool on);
void makeAck(uint8_t result);
void sendAck();
void handleCmd(uint8_t cmd[2]);
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
  ack[0] = ackType;
  ack[1] = result;
}

// currently only prints ACK to Serial for testing
// later this can be replaced by real LoRa ACK sending
void sendAck() {
  Serial.print("ACK packet: [0x");
  Serial.print(ack[0], HEX);
  Serial.print(", 0x");
  Serial.print(ack[1], HEX);
  Serial.println("]");

  // Give Gateway a short time to switch from TX mode to RX mode.
  delay(300);

  String hexPayload = bytesToHex(ack, 2);
  String cmd = String("radio tx ") + hexPayload;

  Serial.print("Sending ACK by LoRa: ");
  Serial.println(hexPayload);

  String first = sendLoRaCommand(cmd, 2000);

  if (first != "ok") {
    Serial.println("ACK TX failed: RN2483 did not accept command.");
    return;
  }

  String done = readLoRaLine(10000);

  Serial.print("RN2483 => ");
  Serial.println(done.length() ? done : "<timeout>");

  if (done == "radio_tx_ok") {
    Serial.println("ACK sent.");
  } else {
    Serial.println("ACK TX not completed.");
  }
}

// handle the CMD packet
// format: [cmdType, cmdValue]
void handleCmd(uint8_t cmd[2]) {
  Serial.print("CMD packet: [0x");
  Serial.print(cmd[0], HEX);
  Serial.print(", 0x");
  Serial.print(cmd[1], HEX);
  Serial.println("]");

  // first byte must be CMD type
  if (cmd[0] != cmdType) {
    Serial.println("Wrong packet type.");
    makeAck(ackFail);
    sendAck();
    return;
  }

  // second byte check
  if (cmd[1] == cmdOn) {
    setLed(true);
    makeAck(ackOk);
    sendAck();
  }
  else if (cmd[1] == cmdOff) {
    setLed(false);
    makeAck(ackOk);
    sendAck();
  }
  else {
    Serial.println("Unknown CMD value.");
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
    Serial.println("RN2483 init failed. Stop here.");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("Light Control Node ready.");
  Serial.println("Waiting for CMD packet: 0201 = ON, 0200 = OFF");
}


void loop() {
  listenForCmd();
}


// ---------- Real LoRa receive part ----------
void listenForCmd() {
  String first = sendLoRaCommand("radio rx 0", 1500);

  if (first != "ok") {
    Serial.println("Failed to enter RX mode.");
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

    // CMD packet should be 2 bytes = 4 HEX characters
    if (hexPayload.length() != 4) {
      Serial.println("Wrong CMD length. Expected 4 HEX chars.");
      makeAck(ackFail);
      sendAck();
      return;
    }

    uint8_t cmd[2];

    if (!hexToBytes(hexPayload, cmd, 2)) {
      Serial.println("HEX decode failed.");
      makeAck(ackFail);
      sendAck();
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