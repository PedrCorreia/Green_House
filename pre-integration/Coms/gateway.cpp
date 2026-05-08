/*

  Board: ESP32 (DevKit / WROOM)
  Role:  Gateway / receiver. Sits on WiFi + MQTT, talks to RN2483 over UART1.
         Sends wake pings to sensor nodes, receives 13-byte sensor payloads,
         publishes them as JSON to the dashboard, and tells nodes when the
         plant needs more light.

  Flow:
    boot -> WiFi -> MQTT -> init RN2483 -> loop:
      keep WiFi/MQTT alive, send periodic ping, listen for sensor reply,
      publish JSON, optionally send light command back.

  Test:
    1. Flash, open Serial Monitor at 115200.
    2. Wait for "WiFi connected" + "MQTT connected" + "Gateway ready".
    3. Subscribe to esp32/myroom123/status with mosquitto_sub or MQTT Explorer.
    4. Publish "PING" to esp32/myroom123/ping to force a wake-up ping.
    5. The sensor node should reply within a few seconds; JSON shows up.


*/

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <HardwareSerial.h>

// ---------- WiFi / MQTT ----------
#define WIFI_SSID       "!Klosteret"
#define WIFI_PASSWORD   "Vikar20,21"

#define MQTT_BROKER     "broker.emqx.io"
#define MQTT_PORT       1883

#define TOPIC_CONTROL   "esp32/myroom123/control"
#define TOPIC_STATUS    "esp32/myroom123/status"
#define TOPIC_PING      "esp32/myroom123/ping"

// ---------- Pins ----------
#define LED_PIN          2     
#define LORA_UART_RX_PIN 16    // ESP32 GPIO16 -> RN2483 TX
#define LORA_UART_TX_PIN 17    // ESP32 GPIO17 -> RN2483 RX
#define RN2483_RST_PIN   14

// ---------- LoRa radio settings (must match sensor node) ----------
#define LORA_BAUD_RATE   57600UL
#define LORA_FREQ        868100000UL   // working TX/RX pair uses 868.1 MHz
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
#define LORA_WDT         60000UL       // RX watchdog ms; 0 = no timeout

// ---------- Protocol ----------
#define LORA_PING_HEX            "50494E47"   // "PING" in ASCII hex
#define SENSOR_PAYLOAD_BYTES     13
#define SENSOR_PAYLOAD_HEX_CHARS 26
#define LIGHT_CMD_BYTES          6

// ---------- Timing ----------
#define PING_INTERVAL_MS         120000UL   // auto-ping every 2 min (test)
#define POST_PING_WAIT_MS        10000UL    // listen this long for a sensor reply
#define HEARTBEAT_INTERVAL_MS    60000UL
#define MQTT_RECONNECT_INTERVAL  3000UL
// Small gap before sending the light command, so the sensor node's
// post-TX RX window is definitely open by the time we transmit.
#define LIGHT_CMD_PRE_DELAY_MS   400

#define LIGHT_THRESHOLD_LUX      200
#define DESIRED_LUX_WHEN_DARK    500

WiFiClient    espClient;
PubSubClient  mqttClient(espClient);
HardwareSerial loraSerial(1);

bool loraReady = false;
bool ledState  = false;

unsigned long lastPingTime = 0;
unsigned long lastHeartbeatTime = 0;
unsigned long lastMqttReconnectAttempt = 0;

struct SensorData {
  uint32_t deviceId;
  float    temperature;
  uint8_t  humidity;
  uint16_t lux;
  uint16_t soilMoisture;
  bool     waterLeak;
  uint8_t  battery;
};


void connectToWiFi();
void ensureWiFiConnected();
bool connectToMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void publishHeartbeat();
void setLed(bool on);

bool   initLoRa();
String sendLoRaCommand(const String& cmd, int timeoutMs);
String readLoRaLine(int timeoutMs);
void   flushLoRaInput();
void   stopRx();

bool sendPing(const char* reason);
bool waitForSensorReply(int timeoutMs);
bool parseSensorPayload(const String& hexStr, SensorData& out);
void publishSensorData(const SensorData& d, bool needsLight);
bool sendLightCommand(uint32_t targetDeviceId, uint16_t desiredLux);

bool   hexToBytes(const String& hexStr, uint8_t* out, size_t outLen);
String bytesToHex(const uint8_t* data, size_t len);
int    hexNibble(char c);


// =================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(LED_PIN, OUTPUT);
  setLed(false);

  Serial.println();
  Serial.println("=== Greenhouse Gateway starting ===");

  connectToWiFi();

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setBufferSize(512);
  mqttClient.setCallback(mqttCallback);

  if (!initLoRa()) {
    Serial.println("RN2483 init failed. Halting (gateway must not run with broken radio).");
    while (true) { delay(1000); }
  }

  lastPingTime      = millis();
  lastHeartbeatTime = millis();
  Serial.println("Gateway ready.");
}

// Gateway never sleeps. Keep WiFi/MQTT alive, ping periodically,
// listen briefly for a reply after each ping.
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

      if (now - lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS) {
        lastHeartbeatTime = now;
        publishHeartbeat();
      }
    }
  }

  // Scheduled ping
  if (now - lastPingTime >= PING_INTERVAL_MS) {
    lastPingTime = now;
    sendPing("scheduled");
  }
}

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
    Serial.println("WiFi timeout — will keep retrying in loop().");
  }
}

void ensureWiFiConnected() {
  static unsigned long lastRetry = 0;
  if (WiFi.status() == WL_CONNECTED) return;

  unsigned long now = millis();
  if (now - lastRetry >= 5000UL) {
    lastRetry = now;
    Serial.println("WiFi dropped. Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

bool connectToMQTT() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String clientId = "ESP32Gateway-";
  clientId += String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF), HEX);

  Serial.print("MQTT connect to ");
  Serial.print(MQTT_BROKER);
  Serial.print(":");
  Serial.println(MQTT_PORT);

  if (mqttClient.connect(clientId.c_str())) {
    Serial.println("MQTT connected");
    mqttClient.subscribe(TOPIC_CONTROL);
    mqttClient.subscribe(TOPIC_PING);
    mqttClient.publish(TOPIC_STATUS, "ESP32 connected to MQTT broker");
    return true;
  }

  Serial.print("MQTT connect failed, rc=");
  Serial.println(mqttClient.state());
  return false;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();

  Serial.print("MQTT [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(msg);

  String t(topic);

  if (t == TOPIC_PING) {
    if (msg.equalsIgnoreCase("PING")) {
      bool ok = sendPing("manual MQTT");
      mqttClient.publish(TOPIC_STATUS,
        ok ? "Manual LoRa PING sent" : "Manual LoRa PING failed");
    } else {
      mqttClient.publish(TOPIC_STATUS, "Unknown ping command received; expected PING");
    }
  }
  else if (t == TOPIC_CONTROL) {
    if (msg.equalsIgnoreCase("ON")) {
      setLed(true);
      mqttClient.publish(TOPIC_STATUS, "LED turned ON");
    } else if (msg.equalsIgnoreCase("OFF")) {
      setLed(false);
      mqttClient.publish(TOPIC_STATUS, "LED turned OFF");
    } else {
      mqttClient.publish(TOPIC_STATUS, "Unknown command received");
    }
  }
}

void publishHeartbeat() {
  if (!mqttClient.connected()) return;

  String msg = "Hello from ESP32 | uptime(s): ";
  msg += String(millis() / 1000);
  msg += " | LED: ";
  msg += (ledState ? "ON" : "OFF");

  mqttClient.publish(TOPIC_STATUS, msg.c_str());
  Serial.print("Heartbeat: ");
  Serial.println(msg);
}

void setLed(bool on) {
  ledState = on;
  digitalWrite(LED_PIN, on ? HIGH : LOW);
  Serial.print("Gateway LED ");
  Serial.println(on ? "ON" : "OFF");
}


bool initLoRa() {
  pinMode(RN2483_RST_PIN, OUTPUT);
  digitalWrite(RN2483_RST_PIN, HIGH);

  loraSerial.begin(LORA_BAUD_RATE, SERIAL_8N1, LORA_UART_RX_PIN, LORA_UART_TX_PIN);
  loraSerial.setTimeout(2000);

  // Reset the module after UART is up so the first command lands cleanly.
  digitalWrite(RN2483_RST_PIN, LOW);
  delay(200);
  digitalWrite(RN2483_RST_PIN, HIGH);
  delay(500);

  flushLoRaInput();

  String banner = readLoRaLine(500);
  if (banner.length()) {
    Serial.print("RN2483 banner: ");
    Serial.println(banner);
  }

  String ver = sendLoRaCommand("sys get ver", 1500);
  Serial.print("RN2483 version: ");
  Serial.println(ver);

  // mac pause first — we want raw LoRa, not LoRaWAN MAC
  String mp = sendLoRaCommand("mac pause", 2000);
  if (mp.length() == 0 || mp == "invalid_param") {
    Serial.println("mac pause failed");
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
      Serial.print("  -> ");
      Serial.println(r.length() ? r : "<timeout>");
      return false;
    }
  }

  loraReady = true;
  return true;
}

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
      if (c == '\n') { line.trim(); return line; }
      line += c;
    }
    delay(1);
  }
  line.trim();
  return line;
}

void flushLoRaInput() {
  while (loraSerial.available()) loraSerial.read();
}


void stopRx() {
  loraSerial.print("radio rxstop\r\n");
  // Read up to two short lines: the rxstop ack and any trailing
  // radio_rx / radio_err that was already in flight.
  for (int i = 0; i < 2; i++) {
    String line = readLoRaLine(300);
    if (line.length() == 0) break;
    Serial.print("RN2483 (drain) => ");
    Serial.println(line);
  }
}



bool sendPing(const char* reason) {
  if (!loraReady) {
    Serial.println("sendPing: LoRa not ready");
    return false;
  }

  Serial.print("Sending PING (");
  Serial.print(reason);
  Serial.println(")");

  String cmd = String("radio tx ") + LORA_PING_HEX;
  String firstReply = sendLoRaCommand(cmd, 2000);

  if (firstReply != "ok") {
    Serial.print("Ping rejected: ");
    Serial.println(firstReply);
    return false;
  }

  // Second reply: radio_tx_ok when transmission finishes.
  String txDone = readLoRaLine(10000);
  Serial.print("RN2483 => ");
  Serial.println(txDone.length() ? txDone : "<timeout>");

  if (txDone != "radio_tx_ok") {
    Serial.println("TX did not complete cleanly.");
    return false;
  }
  waitForSensorReply(POST_PING_WAIT_MS);
  return true;
}

bool waitForSensorReply(int timeoutMs) {
  Serial.print("Listening for sensor reply (");
  Serial.print(timeoutMs);
  Serial.println(" ms)...");

  // radio rx 0 = listen until a packet arrives or wdt fires
  String first = sendLoRaCommand("radio rx 0", 1500);
  if (first != "ok") {
    Serial.println("RX mode not entered.");
    return false;
  }

  String line = readLoRaLine(timeoutMs);

  if (line.startsWith("radio_rx ")) {
    String hex = line.substring(9);
    hex.trim();
    Serial.print("RX payload hex: ");
    Serial.println(hex);

    SensorData d;
    if (!parseSensorPayload(hex, d)) {
      Serial.println("Bad payload, ignoring.");
      return false;
    }

    bool needsLight = (d.lux < LIGHT_THRESHOLD_LUX);
    publishSensorData(d, needsLight);

    if (needsLight) {
      // Give the sensor node a moment to switch from TX to RX before we transmit.
      delay(LIGHT_CMD_PRE_DELAY_MS);
      Serial.println("Lux below threshold -> sending light ON command.");
      sendLightCommand(d.deviceId, DESIRED_LUX_WHEN_DARK);
    } else {
      Serial.println("Light OK, no command sent.");
    }
    return true;
  }
  else if (line == "radio_err") {
    Serial.println("No reply (radio_err / timeout).");
    return false;
  }
  else {
    // Either timeout (empty line) or unexpected text. The RN2483 may still
    // be in RX mode, so explicitly stop it before we leave.
    Serial.print("RX window expired or unexpected line: ");
    Serial.println(line.length() ? line : "<timeout>");
    stopRx();
    return false;
  }
}


bool parseSensorPayload(const String& hexStr, SensorData& out) {
  String h = hexStr;
  h.trim();
  h.toUpperCase();

  if (h.length() != SENSOR_PAYLOAD_HEX_CHARS) {
    Serial.print("Payload wrong length: ");
    Serial.println(h.length());
    return false;
  }

  uint8_t b[SENSOR_PAYLOAD_BYTES];
  if (!hexToBytes(h, b, SENSOR_PAYLOAD_BYTES)) {
    Serial.println("Payload hex decode failed.");
    return false;
  }

  out.deviceId = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
                 ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];

  uint16_t tRaw = ((uint16_t)b[4] << 8) | b[5];
  out.temperature  = ((int16_t)tRaw) / 10.0f;
  out.humidity     = b[6];
  out.lux          = ((uint16_t)b[7] << 8) | b[8];
  out.soilMoisture = ((uint16_t)b[9] << 8) | b[10];
  out.waterLeak    = (b[11] != 0);
  out.battery      = b[12];
  return true;
}

void publishSensorData(const SensorData& d, bool needsLight) {
  char idStr[11];
  snprintf(idStr, sizeof(idStr), "0x%08lX", (unsigned long)d.deviceId);

  String msg = "{";
  msg += "\"type\":\"sensorData\",";
  msg += "\"deviceId\":\""; msg += idStr; msg += "\",";
  msg += "\"temperature\":"; msg += String(d.temperature, 1);
  msg += ",\"humidity\":";    msg += d.humidity;
  msg += ",\"lux\":";         msg += d.lux;
  msg += ",\"soilMoisture\":"; msg += d.soilMoisture;
  msg += ",\"waterLeak\":";   msg += (d.waterLeak ? "true" : "false");
  msg += ",\"battery\":";     msg += d.battery;
  msg += ",\"needsLight\":";  msg += (needsLight ? "true" : "false");
  msg += "}";

  Serial.print("Sensor: ");
  Serial.println(msg);

  if (mqttClient.connected()) {
    mqttClient.publish(TOPIC_STATUS, msg.c_str());
  } else {
    Serial.println("MQTT down — JSON only on Serial.");
  }
}


bool sendLightCommand(uint32_t targetDeviceId, uint16_t desiredLux) {
  if (!loraReady) return false;

  uint8_t b[LIGHT_CMD_BYTES];
  b[0] = (targetDeviceId >> 24) & 0xFF;
  b[1] = (targetDeviceId >> 16) & 0xFF;
  b[2] = (targetDeviceId >> 8)  & 0xFF;
  b[3] =  targetDeviceId        & 0xFF;
  b[4] = (desiredLux >> 8)      & 0xFF;
  b[5] =  desiredLux            & 0xFF;

  String hex = bytesToHex(b, LIGHT_CMD_BYTES);
  String cmd = String("radio tx ") + hex;

  String first = sendLoRaCommand(cmd, 2000);
  if (first != "ok") {
    Serial.print("Light cmd rejected: ");
    Serial.println(first);
    return false;
  }

  String done = readLoRaLine(10000);
  Serial.print("RN2483 => ");
  Serial.println(done.length() ? done : "<timeout>");
  return (done == "radio_tx_ok");
}



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

String bytesToHex(const uint8_t* data, size_t len) {
  static const char hx[] = "0123456789ABCDEF";
  String s;
  s.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    s += hx[(data[i] >> 4) & 0x0F];
    s += hx[data[i] & 0x0F];
  }
  return s;
}