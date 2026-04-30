#define BLYNK_TEMPLATE_ID "TMPL4hwOonb-"
#define BLYNK_TEMPLATE_NAME "34346 Group Project"
#define BLYNK_AUTH_TOKEN "8hgm20lHCIImSiweKzr81MxVGDqoVwgh"

#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>

// =========================
// User-configurable settings
// =========================
#define WIFI_SSID       "stas iphone"
#define WIFI_PASSWORD   "KAPELERII"

#define MQTT_BROKER "broker.emqx.io"
#define MQTT_PORT   1883

#define TOPIC_CONTROL "esp32/myroom123/control"
#define TOPIC_STATUS  "esp32/myroom123/status"

#define LED_PIN         D4
#define PUBLISH_INTERVAL_MS 5000UL

// =========================
// Global objects
// =========================
WiFiClient espClient;
PubSubClient mqttClient(espClient);
BlynkTimer timer;

// =========================
// Timing / state
// =========================
unsigned long lastPublishTime = 0;
unsigned long lastMqttReconnectAttempt = 0;
const unsigned long mqttReconnectInterval = 3000UL;

bool ledState = false;

// =========================
// Function declarations
// =========================
void connectToWiFi();
void ensureWiFiConnected();
bool connectToMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void publishStatus();
void setLed(bool on);

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(LED_PIN, OUTPUT);
  setLed(false);

  Serial.println();
  Serial.println("=== ESP32 MQTT Bridge Starting ===");

  connectToWiFi();

  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connect();

  if (Blynk.connected()) {
    Serial.println("Blynk connected successfully");
  } else {
    Serial.println("Blynk NOT connected");
  }

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
}

// =========================
// Main loop
// =========================
void loop() {
  ensureWiFiConnected();

  Blynk.run();
  timer.run();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      unsigned long now = millis();
      if (now - lastMqttReconnectAttempt >= mqttReconnectInterval) {
        lastMqttReconnectAttempt = now;
        connectToMQTT();
      }
    } else {
      mqttClient.loop();

      unsigned long now = millis();
      if (now - lastPublishTime >= PUBLISH_INTERVAL_MS) {
        lastPublishTime = now;
        publishStatus();
      }
    }
  }
}

// =========================
// WiFi functions
// =========================
void connectToWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000UL) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connection timed out. Will retry in loop().");
  }
}

void ensureWiFiConnected() {
  static unsigned long lastWiFiRetry = 0;
  const unsigned long wifiRetryInterval = 5000UL;

  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  unsigned long now = millis();
  if (now - lastWiFiRetry >= wifiRetryInterval) {
    lastWiFiRetry = now;
    Serial.println("WiFi disconnected. Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

// =========================
// MQTT functions
// =========================
bool connectToMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  String clientId = "ESP32Client-";
  clientId += String(ESP.getChipId(), HEX);
  
  Serial.print("Connecting to MQTT broker ");
  Serial.print(MQTT_BROKER);
  Serial.print(":");
  Serial.println(MQTT_PORT);

  if (mqttClient.connect(clientId.c_str())) {
    Serial.println("MQTT connected");

    if (mqttClient.subscribe(TOPIC_CONTROL)) {
      Serial.print("Subscribed to: ");
      Serial.println(TOPIC_CONTROL);
    } else {
      Serial.println("Failed to subscribe to control topic");
    }

    mqttClient.publish(TOPIC_STATUS, "ESP32 connected to MQTT broker");
    return true;
  } else {
    Serial.print("MQTT connect failed, rc=");
    Serial.println(mqttClient.state());
    return false;
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String incomingMessage;

  for (unsigned int i = 0; i < length; i++) {
    incomingMessage += (char)payload[i];
  }

  Serial.print("Message received on topic [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(incomingMessage);

  if (String(topic) == TOPIC_CONTROL) {
    incomingMessage.trim();

    if (incomingMessage.equalsIgnoreCase("ON")) {
      setLed(true);
      mqttClient.publish(TOPIC_STATUS, "LED turned ON");
      Blynk.virtualWrite(V1, "LED ON");
    }
    else if (incomingMessage.equalsIgnoreCase("OFF")) {
      setLed(false);
      mqttClient.publish(TOPIC_STATUS, "LED turned OFF");
      Blynk.virtualWrite(V1, "LED OFF");
    }
    else {
      mqttClient.publish(TOPIC_STATUS, "Unknown command received");
    }
  }
}

void publishStatus() {
  if (!mqttClient.connected()) {
    return;
  }

  String message = "Hello from ESP32 | uptime(s): ";
  message += String(millis() / 1000);
  message += " | LED: ";
  message += (ledState ? "ON" : "OFF");

  mqttClient.publish(TOPIC_STATUS, message.c_str());

  Blynk.virtualWrite(V1, ledState ? "LED ON" : "LED OFF");
  Blynk.virtualWrite(V2, millis() / 1000);

  Serial.print("Published to ");
  Serial.print(TOPIC_STATUS);
  Serial.print(": ");
  Serial.println(message);
}

// =========================
// Helper functions
// =========================
void setLed(bool on) {
  ledState = on;
  digitalWrite(LED_PIN, on ? LOW : HIGH);

  Serial.print("LED is now ");
  Serial.println(on ? "ON" : "OFF");
}

// =========================
// Blynk button function
// =========================
BLYNK_WRITE(V0) {
  int value = param.asInt();

  Serial.print("Blynk button pressed, V0 = ");
  Serial.println(value);

  if (value == 1) {
    setLed(true);
    mqttClient.publish(TOPIC_STATUS, "LED turned ON from Blynk");
    Blynk.virtualWrite(V1, "LED ON");
  } else {
    setLed(false);
    mqttClient.publish(TOPIC_STATUS, "LED turned OFF from Blynk");
    Blynk.virtualWrite(V1, "LED OFF");
  }
}

BLYNK_CONNECTED() {
  Serial.println("Blynk connected");
}