/*
 * Gateway MQTT -> LoRa CMD Test
 *
 * Board: ESP32 DevKit / WROOM
 * Module: RN2483
 *
 * Purpose:
 * - Connect WiFi + MQTT
 * - Receive ON / OFF from MQTT control topic
 * - Pack LoRa CMD packet:
 *     ON  -> 0201
 *     OFF -> 0200
 * - Send CMD to Light Control Node by RN2483 raw LoRa P2P
 *
 * MQTT test:
 * - Subscribe: esp32/myroom123/status
 * - Publish to: esp32/myroom123/control
 * - Payload: ON or OFF
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <HardwareSerial.h>

// ---------- WiFi / MQTT ----------
// Change these to your real WiFi. Phone hotspot is recommended for testing.
#define WIFI_SSID       "Zhe iphone"
#define WIFI_PASSWORD   "20011109"

#define MQTT_BROKER     "broker.emqx.io"
#define MQTT_PORT       1883

#define TOPIC_CONTROL   "esp32/myroom123/control"
#define TOPIC_STATUS    "esp32/myroom123/status"
#define TOPIC_PING      "esp32/myroom123/ping"

#define MQTT_RECONNECT_INTERVAL  3000UL

// ---------- Pins ----------
// Same style as the full Gateway code you sent before.
#define LORA_UART_RX_PIN 18    // ESP32 RX <- RN2483 TX
#define LORA_UART_TX_PIN 19    // ESP32 TX -> RN2483 RX
#define RN2483_RST_PIN   23

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

// If your Light Node already sends ACK by LoRa, set this to true.
// If your Light Node only controls LED and does not send ACK yet, keep false.
#define USE_ACK          false

WiFiClient espClient;
PubSubClient mqttClient(espClient);
HardwareSerial loraSerial(1);

bool loraReady = false;
unsigned long lastMqttReconnectAttempt = 0;

// ---------- Function declarations ----------
void connectToWiFi();
void ensureWiFiConnected();
bool connectToMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void publishStatus(const String& msg);

bool initLoRa();
String sendLoRaCommand(const String& cmd, int timeoutMs);
String readLoRaLine(int timeoutMs);
void flushLoRaInput();

bool sendLightCmd(bool turnOn);
bool sendRadioTxHex(const String& hexPayload, const char* label);
bool waitForAck();
String bytesToHex(const uint8_t* data, size_t len);

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=== Gateway MQTT -> LoRa CMD Test ===");

  connectToWiFi();

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setBufferSize(512);
  mqttClient.setCallback(mqttCallback);

  if (!initLoRa()) {
    Serial.println("RN2483 init failed. Stop here.");
    while (true) {
      delay(1000);
    }
  }

  connectToMQTT();

  Serial.println();
  Serial.println("Gateway ready.");
  Serial.println("MQTT control topic: " TOPIC_CONTROL);
  Serial.println("Publish payload: ON or OFF");
  Serial.println("Serial shortcut: type 1 = ON, 0 = OFF, p = PING");
}

void loop() {
  ensureWiFiConnected();

  unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      if (now - lastMqttReconnectAttempt >= MQTT_RECONNECT_INTERVAL) {
        lastMqttReconnectAttempt = now;
        connectToMQTT();
      }
    } else {
      mqttClient.loop();
    }
  }

  // Keep Serial manual test too. It does not affect MQTT test.
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

// ---------- WiFi / MQTT ----------
void connectToWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000UL) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi timeout. It will keep retrying in loop().");
  }
}

void ensureWiFiConnected() {
  static unsigned long lastRetry = 0;

  if (WiFi.status() == WL_CONNECTED) return;

  unsigned long now = millis();
  if (now - lastRetry >= 5000UL) {
    lastRetry = now;
    Serial.println("WiFi disconnected. Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

bool connectToMQTT() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String clientId = "ESP32GatewayCMD-";
  clientId += String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF), HEX);

  Serial.print("MQTT connecting to ");
  Serial.print(MQTT_BROKER);
  Serial.print(":");
  Serial.println(MQTT_PORT);

  if (mqttClient.connect(clientId.c_str())) {
    Serial.println("MQTT connected.");

    mqttClient.subscribe(TOPIC_CONTROL);
    mqttClient.subscribe(TOPIC_PING);

    publishStatus("Gateway connected to MQTT. Send ON/OFF to control topic.");
    return true;
  }

  Serial.print("MQTT connect failed, rc=");
  Serial.println(mqttClient.state());
  return false;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length);

  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  msg.trim();

  Serial.print("MQTT [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(msg);

  String t(topic);

  if (t == TOPIC_CONTROL) {
    if (msg.equalsIgnoreCase("ON") ||
        msg.equalsIgnoreCase("LIGHT ON") ||
        msg.equalsIgnoreCase("LIGHT_ON")) {

      bool ok = sendLightCmd(true);
      publishStatus(ok ? "Light ON CMD sent by LoRa" : "Light ON CMD failed");
    }
    else if (msg.equalsIgnoreCase("OFF") ||
             msg.equalsIgnoreCase("LIGHT OFF") ||
             msg.equalsIgnoreCase("LIGHT_OFF")) {

      bool ok = sendLightCmd(false);
      publishStatus(ok ? "Light OFF CMD sent by LoRa" : "Light OFF CMD failed");
    }
    else {
      publishStatus("Unknown control command. Use ON or OFF.");
    }
  }
  else if (t == TOPIC_PING) {
    if (msg.equalsIgnoreCase("PING")) {
      bool ok = sendRadioTxHex("50494E47", "PING");
      publishStatus(ok ? "LoRa PING sent" : "LoRa PING failed");
    } else {
      publishStatus("Unknown ping command. Use PING.");
    }
  }
}

void publishStatus(const String& msg) {
  Serial.print("STATUS: ");
  Serial.println(msg);

  if (mqttClient.connected()) {
    mqttClient.publish(TOPIC_STATUS, msg.c_str());
  }
}

// ---------- LoRa init ----------
bool initLoRa() {
  pinMode(RN2483_RST_PIN, OUTPUT);
  digitalWrite(RN2483_RST_PIN, HIGH);

  loraSerial.begin(LORA_BAUD_RATE, SERIAL_8N1, LORA_UART_RX_PIN, LORA_UART_TX_PIN);
  loraSerial.setTimeout(2000);

  // Hard reset RN2483.
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
  if (!loraReady) {
    Serial.println("LoRa not ready. Cannot send light CMD.");
    return false;
  }

  uint8_t packet[2];

  // Byte 0: message type. 0x02 means CMD packet.
  packet[0] = MSG_TYPE_CMD;

  // Byte 1: command value. 0x01 = ON, 0x00 = OFF.
  if (turnOn) {
    packet[1] = CMD_LIGHT_ON;
  } else {
    packet[1] = CMD_LIGHT_OFF;
  }

  String hexPayload = bytesToHex(packet, 2);

  Serial.print("Sending light CMD by LoRa: ");
  Serial.println(hexPayload);

  String cmd = String("radio tx ") + hexPayload;
  String first = sendLoRaCommand(cmd, 2000);

  if (first != "ok") {
    Serial.println("Light CMD failed: RN2483 did not accept the TX command.");
    return false;
  }

  String done = readLoRaLine(10000);

  Serial.print("RN2483 => ");
  Serial.println(done.length() ? done : "<timeout>");

  if (done != "radio_tx_ok") {
    Serial.println("Light CMD failed: TX was not completed.");
    return false;
  }

  Serial.println("Light CMD TX done.");

#if USE_ACK
  return waitForAck();
#else
  return true;
#endif
}

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

  return (done == "radio_tx_ok");
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
