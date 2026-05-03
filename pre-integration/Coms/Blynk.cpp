/*
    Gateway firmware for the greenhouse IoT project.

    What this does:
    - Connects to WiFi and keeps the connection alive
    - Talks to a Microchip RN2483 LoRa module over UART to receive sensor data
    - Decodes the binary sensor payload (temperature, humidity, lux, soil, leak, battery)
    - Publishes the data as JSON over MQTT so the browser dashboard can display it
    - Also forwards data to Blynk (virtual pins — ask the Blynk person before changing those)
    - Sends a "wake up" ping over LoRa every X minutes to tell sleeping nodes to transmit
    - Can also send that ping manually if you press the button in the browser

    Hardware:
    - ESP32 (the board the LoRa module talks to)
    - RN2483 wiring is defined in the pin constants below
*/

#define BLYNK_TEMPLATE_ID "TMPL4hwOonb-"                         
#define BLYNK_TEMPLATE_NAME "34346 Group Project"                
#define BLYNK_AUTH_TOKEN "8hgm20lHCIImSiweKzr81MxVGDqoVwgh"     

#include <Arduino.h>
#include <PubSubClient.h>

//Headers for the files we need, basically same as the assignment we ded
  #include <WiFi.h>
  #include <BlynkSimpleEsp32.h>
  #include <HardwareSerial.h>


//settings for everything, im still using localhost, we can host it somewhere maybe
#define WIFI_SSID       "!Klosteret"                              
#define WIFI_PASSWORD   "Vikar20,21"                              

#define MQTT_BROKER "broker.emqx.io"                              
#define MQTT_PORT   1883                                          

#define TOPIC_CONTROL "esp32/myroom123/control"                   
#define TOPIC_STATUS  "esp32/myroom123/status"                    // Mqtt channel fot dashboard to listen.
#define TOPIC_PING    "esp32/myroom123/ping"                      //Pings

#define LED_PIN        2                                         
#define LED_ON_LEVEL   HIGH                                      
#define LED_OFF_LEVEL  LOW                                       


// ============================================================
// HOW OFTEN TO PUBLISH SENSOR DATA
// Change this value to control the publish rate.
// 30000   = every 30 seconds  (good for testing)
// 300000  = every 5 minutes   (reasonable for real use)
// 3600000 = every hour        (power-saving mode)
// For the greenhouse we'll probably want 5-10 minutes when deployed.
// ============================================================
#define PUBLISH_INTERVAL_MS 30000UL

// Heartbeat is just a thing so we can see its fine
#define HEARTBEAT_INTERVAL_MS 60000UL

// ============================================================
// HOW OFTEN TO SEND THE AUTOMATIC WAKE-UP PING
// This broadcasts over LoRa to tell sleeping sensor nodes to wake up and transmit.
// 120000    = every 2 minutes  (use this when testing so you don't sit around waiting)
// 21600000  = every 6 hours    (the real greenhouse schedule from the protocol doc)
// ============================================================
#define PING_INTERVAL_MS 120000UL  // 2 minutes for testing. Change to 21600000UL (6 hours) for deployment.

// How long to wait for a sensor node response after sending a ping, in ms.
// Nodes need time to wake up, read sensors, and build their packet.
// 10 seconds is generous — adjust based on how fast the sensor nodes actually respond.
#define POST_PING_WAIT_MS 10000UL

#define LORA_UART_RX_PIN 18                                       
#define LORA_UART_TX_PIN 19                                       
#define RN2483_RST_PIN   23                                       
#define LORA_BAUD_RATE   57600UL                                  // RN2483 UART baud rate

//Basically what we did in the assignment just one place so easy to change
#define LORA_FREQ   865000000UL                                  
#define LORA_PWR    14                                           
#define LORA_SF     "sf12"                                       
#define LORA_AFCBW  "41.7"                                       
#define LORA_RXBW   125                                          
#define LORA_PRLEN  8                                            
#define LORA_CRC    "on"                                         
#define LORA_IQI    "off"                                        
#define LORA_CR     "4/5"                                        
#define LORA_WDT    60000UL                                      
#define LORA_SYNC   "12"                                         
#define LORA_BW     125                                           

#define LORA_PING_HEX "50494E47"                                  // Four-byte wake ping is ASCII "PING" in hex
#define SENSOR_PAYLOAD_BYTES 13                                   
#define SENSOR_PAYLOAD_HEX_CHARS 26                               

// useMockData exists so the browser dashboard and MQTT path can be tested before the LoRa nodes are finished.
// Set this to false when the RN2483, antenna, and real sensor nodes are actually connected.
bool useMockData = true;


WiFiClient espClient;                                             
PubSubClient mqttClient(espClient);                               
BlynkTimer timer;                                                  
HardwareSerial loraSerial(1); // UART1 for RN2483. UART0 stays free for USB Serial Monitor logs, which you will absolutely need at 2am.


//This is the protocol done in a struct so easily readable
struct SensorData {
  uint32_t deviceId;                                               // Full 32-bit device ID. Top byte also tells us whether this is a sensor or light node.
  uint8_t nodeTypeFlag;                                            // MSB of device ID: 0x01 = sensor node, 0x02 = light node.
  String nodeType;                                                 // Readable label for the dashboard JSON: "sensor", "light", or "unknown".
  float temperature;                                               // Temperature in °C. Radio sends tenths as int16, then we divide by 10 here.
  uint8_t humidity;                                                // Relative humidity in percent, 0-100.
  uint16_t lux;                                                    // Raw lux value from the light sensor/node firmware.
  uint16_t soilMoisture;                                           // Raw ADC reading. Keep uncalibrated here; calibration belongs in the sensor logic/report.
  bool waterLeak;                                                  // false = dry, true = leak detected.
  uint8_t battery;                                                 // Battery percentage, 0-100.
};


unsigned long lastPublishTime = 0;                                
unsigned long lastMqttReconnectAttempt = 0;
const unsigned long mqttReconnectInterval = 3000UL;                // MQTT reconnect delay in ms. Lower = more aggressive reconnect attempts.

unsigned long lastMockPublishTime = 0;                             
unsigned long lastPingTime = 0; 
unsigned long lastHeartbeatTime = 0;

bool ledState = false;                                             
bool loraReady = false;                                            
bool loraInRxMode = false;                                         
String loraLineBuffer;                                             
SensorData lastSensorData;                                         // Last decoded sensor reading. Useful if we later want local buffering/history.

//random functions that just makes everything works, most are found online from different sources, some are from the assignment, and some are new for the LoRa parsing and MQTT publishing.
void connectToWiFi();
void ensureWiFiConnected();
bool connectToMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void publishStatus();
void setLed(bool on);


//lots of helper functions see when they are made for more info, but basically just to keep the main setup and loop clean and readable, and to break down the complex LoRa parsing into smaller steps.
bool initLoRa();
String sendLoRaCommand(String cmd, int timeoutMs);
String readLoRaLine(int timeoutMs);
void enterLoRaRx();
void pollLoRaSerial();
void handleLoRaLine(String line);
bool parseHexPayload(String hexStr);
bool hexStringToBytes(String hexStr, uint8_t* out, size_t outLen);
int hexNibble(char c);
String bytesToHex(const uint8_t* data, size_t len);
String decodeNodeType(uint8_t nodeTypeFlag);
void printSensorData(const SensorData& data);
void publishSensorData(const SensorData& data);
void handleMockData();
void handleAutomaticPing();
bool sendLoRaPing(const char* reason);
void waitForPostPingPayload();
void formatDeviceId(uint32_t deviceId, char* out, size_t outSize);


// if the RN2483 is missing or misconfigured, and the logs before that help debug it.

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
  mqttClient.setBufferSize(512); // default 256 is too close to our JSON payload size
  mqttClient.setCallback(mqttCallback);

  if (!initLoRa()) {
    Serial.println("Fatal RN2483 setup error. Halting so the gateway does not run with a misconfigured radio.");
    while (true) {
      delay(1000);
    }
  }

  lastPingTime = millis();
  lastPublishTime  = millis();
  lastHeartbeatTime = millis();
}


// Main loop. Order matters here:
// 1. Keep WiFi and MQTT alive
// 2. Run the heartbeat on its own slow timer
// 3. Run mock data if useMockData is true (has its own internal timer)
// 4. If using real LoRa: poll UART for incoming packets and handle automatic pings
// The two paths (mock vs real LoRa) are mutually exclusive — handleMockData() returns
// immediately when useMockData is false, and pollLoRaSerial() returns immediately when true.
void loop() {
  ensureWiFiConnected();

  Blynk.run();
  timer.run();

  unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      if (now - lastMqttReconnectAttempt >= mqttReconnectInterval) {
        lastMqttReconnectAttempt = now;
        connectToMQTT();
      }
    } else {
      mqttClient.loop();

      // Heartbeat — slow keep-alive so you can see the gateway is still alive in the MQTT log
      if (now - lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS) {
        lastHeartbeatTime = now;
        publishStatus();
      }
    }
  }

  handleMockData();

  if (!useMockData) {
    pollLoRaSerial();
    handleAutomaticPing();
  }
}

//obvious 
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

//lora can work withouth wifi, so we dont block everything if wifi is down, 
//but we do want to keep trying to reconnect in case it comes back.
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

//obvious just connects to mqtt
//and all 3 channels we use for this
bool connectToMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  String clientId = "ESP32Client-";

  uint64_t chipId = ESP.getEfuseMac();
  clientId += String((uint32_t)(chipId & 0xFFFFFFFF), HEX);

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

    if (mqttClient.subscribe(TOPIC_PING)) {
      Serial.print("Subscribed to: ");
      Serial.println(TOPIC_PING);
    } else {
      Serial.println("Failed to subscribe to ping topic");
    }

    mqttClient.publish(TOPIC_STATUS, "ESP32 connected to MQTT broker");
    return true;
  } else {
    Serial.print("MQTT connect failed, rc=");
    Serial.println(mqttClient.state());
    return false;
  }
}

//this basically handles a lot of the MQTT commands we expect to get from the dashboard, 
//like turning the LED on/off and sending a manual ping, and it also logs any unexpected messages for debugging.
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
  else if (String(topic) == TOPIC_PING) {
    incomingMessage.trim();

    if (incomingMessage.equalsIgnoreCase("PING")) {
      bool sent = sendLoRaPing("manual MQTT ping");
      if (sent && useMockData) {
        mqttClient.publish(TOPIC_STATUS, "Manual LoRa PING simulated in mock mode");
        // In mock mode, immediately publish a fresh fake reading so the dashboard
        // actually updates when the ping button is pressed — otherwise it feels broken.
        lastMockPublishTime = 0; // force handleMockData() to fire on the next loop tick
      } else {
        mqttClient.publish(TOPIC_STATUS, sent ? "Manual LoRa PING sent" : "Manual LoRa PING failed");
      }
    } else {
      mqttClient.publish(TOPIC_STATUS, "Unknown ping command received; expected PING");
    }
  }
}


//obvious, just sends the info to the dashboard and mqtt, and also updates the virtual pins for Blynk.
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

//inits lora in either mock mode or real mode. In mock mode it just prints a message and returns true so the rest of the code can run without the RN2483. 
//In real mode, it does the full UART setup and radio configuration, returning true if everything worked or false if there was a problem.
bool initLoRa() {
  if (useMockData) {
    Serial.println("Mock data mode is ON. RN2483 init and real LoRa RX are skipped for offline dashboard/MQTT testing.");
    Serial.println("Set useMockData = false when the RN2483, antenna, and sensor nodes are connected.");
    return true;
  }

  pinMode(RN2483_RST_PIN, OUTPUT);
  digitalWrite(RN2483_RST_PIN, HIGH);

  loraSerial.begin(LORA_BAUD_RATE, SERIAL_8N1, LORA_UART_RX_PIN, LORA_UART_TX_PIN);
  loraSerial.setTimeout(1000);

  while (loraSerial.available()) {
    loraSerial.read();
  }

  // The RN2483 can boot into a weird half-ready state, especially if power comes up slowly.
  // Reset it after UART is already running so the first real command hits a clean firmware prompt.
  Serial.println("Resetting RN2483 after UART init...");
  digitalWrite(RN2483_RST_PIN, LOW);
  delay(200);
  digitalWrite(RN2483_RST_PIN, HIGH);
  delay(500);

  while (loraSerial.available()) {
    loraSerial.read();
  }

  // mac pause has to come first because the RN2483 normally wants to run its LoRaWAN MAC.
  // We are doing raw point-to-point LoRa, so we pause LoRaWAN before touching radio settings.
  String response = sendLoRaCommand("mac pause", 2000);
  if (response.length() == 0 || response == "invalid_param") {
    Serial.println("RN2483 error: mac pause failed. Raw LoRa mode is not safe to use.");
    return false;
  }

  // these commands are the complete radio recipe. all sensor/light nodes need matching values.
  // basically what we did in the assignment but with more settings to match the protocol doc 
  // and make it more robust against interference and packet loss. 
  // the settings are defined in the constants at the top for easy tweaking and testing.
  String radioCommands[] = {
    "radio set mod lora",
    String("radio set freq ") + String(LORA_FREQ),
    String("radio set pwr ") + String(LORA_PWR),
    String("radio set sf ") + String(LORA_SF),
    String("radio set afcbw ") + String(LORA_AFCBW),
    String("radio set rxbw ") + String(LORA_RXBW),
    String("radio set prlen ") + String(LORA_PRLEN),
    String("radio set crc ") + String(LORA_CRC),
    String("radio set iqi ") + String(LORA_IQI),
    String("radio set cr ") + String(LORA_CR),
    String("radio set wdt ") + String(LORA_WDT),
    String("radio set sync ") + String(LORA_SYNC),
    String("radio set bw ") + String(LORA_BW)
  };

  //some random error handling for the radio commands, if any of them fail we print an error and return false so the main setup() 
  //can halt the sketch and avoid running with a misconfigured radio. thought it would be fine to add, mostly stole from online examlples
  const size_t commandCount = sizeof(radioCommands) / sizeof(radioCommands[0]);
  for (size_t i = 0; i < commandCount; i++) {
    response = sendLoRaCommand(radioCommands[i], 2000);

    if (response.length() == 0) {
      Serial.print("RN2483 error: no response to config command: ");
      Serial.println(radioCommands[i]);
      return false;
    }

    if (response == "invalid_param") {
      Serial.print("RN2483 error: invalid_param for config command: ");
      Serial.println(radioCommands[i]);
      return false;
    }

    if (response != "ok") {
      Serial.print("RN2483 error: expected ok, got '");
      Serial.print(response);
      Serial.print("' for command: ");
      Serial.println(radioCommands[i]);
      return false;
    }
  }

  loraReady = true;
  enterLoRaRx();
  Serial.println("RN2483 ready, listening...");
  return true;
}

/*
    sendLoRaCommand(cmd, timeoutMs)
    Sends one text command to the RN2483 and waits for one response line.
    Commands must end in CRLF, so this helper adds \r\n for us.
    Returns the response as a String, or an empty String if the module timed out.
*/
String sendLoRaCommand(String cmd, int timeoutMs) {
  Serial.print("RN2483 <= ");
  Serial.println(cmd);

  loraSerial.print(cmd);
  loraSerial.print("\r\n");

  String response = readLoRaLine(timeoutMs);

  Serial.print("RN2483 => ");
  if (response.length() == 0) {
    Serial.println("<timeout/no response>");
  } else {
    Serial.println(response);
  }

  return response;
}

/*
    readLoRaLine(timeoutMs)
    Reads characters from RN2483 UART until a newline arrives or the timeout expires.
    RN2483 responses end with \r\n, so \r is ignored and \n means the line is complete.
    Returns the trimmed line so the rest of the code can compare against "ok", "radio_err", etc.
*/
String readLoRaLine(int timeoutMs) {
  String line;
  unsigned long start = millis();

  while (millis() - start < (unsigned long)timeoutMs) {
    while (loraSerial.available()) {
      char c = (char)loraSerial.read();

      if (c == '\r') {
        continue;
      }

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

/*
    enterLoRaRx()
    Puts the RN2483 back into receive mode using radio rx 0.
    The annoying bit: RN2483 does NOT keep listening after a packet or watchdog timeout.
    That is why this gets called after every radio_rx and radio_err event.
*/
void enterLoRaRx() {
  if (!loraReady || useMockData) {
    return;
  }

  // radio rx 0 means listen until a packet arrives or the RN2483 watchdog gives radio_err.
  // If we forget to call this again after an event, the gateway just sits there deaf.
  Serial.println("RN2483 <= radio rx 0");
  loraSerial.print("radio rx 0\r\n");
  loraInRxMode = true;
}

// This should be called frequently in loop() to check for incoming data from the RN2483.
void pollLoRaSerial() {
  if (!loraReady || useMockData) {
    return;
  }

  while (loraSerial.available()) {
    char c = (char)loraSerial.read();

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      String line = loraLineBuffer;
      loraLineBuffer = "";
      line.trim();
      handleLoRaLine(line);
    } else {
      loraLineBuffer += c;

      if (loraLineBuffer.length() > 160) {
        Serial.println("RN2483 line buffer overflow; dropping partial line so the parser can recover.");
        loraLineBuffer = "";
      }
    }
  }
}

/*
    handleLoRaLine(line)
    Deals with one complete line from the RN2483.
    radio_rx means a packet arrived and the hex payload should be parsed.
    radio_err usually means the RX watchdog timed out or CRC failed.
    Either way, RX must be started again afterwards or we stop listening.
*/
void handleLoRaLine(String line) {
  if (line.length() == 0) {
    return;
  }

  Serial.print("RN2483 => ");
  Serial.println(line);

  if (line.startsWith("radio_rx ")) {
    loraInRxMode = false;
    String hexPayload = line.substring(String("radio_rx ").length());
    hexPayload.trim();
    parseHexPayload(hexPayload);
    enterLoRaRx();
  }
  else if (line == "radio_err") {
    loraInRxMode = false;
    Serial.println("RN2483 RX watchdog fired or CRC failed; no usable packet received. Returning to RX.");
    enterLoRaRx();
  }
  else if (line == "ok") {
    Serial.println("RN2483 acknowledged command.");
  }
  else if (line == "radio_tx_ok") {
    Serial.println("RN2483 reports TX complete.");
  }
  else {
    Serial.println("RN2483 sent an unhandled line. Leaving RX state unchanged for safety.");
  }
}

bool sendLoRaPing(const char* reason) {
  Serial.print("LoRa PING requested: ");
  Serial.println(reason);

  if (useMockData) {
    Serial.println("Mock data mode is ON; treating PING as simulated and not touching the RN2483 UART.");
    return true;
  }

  if (!loraReady) {
    Serial.println("Cannot send LoRa PING: RN2483 is not ready.");
    return false;
  }

  // RN2483 cannot transmit while radio rx 0 is active. Stop RX, send the ping,
  // then jump straight back into RX so we do not miss the next node transmission.
  //found onlinehopefully this works
  String stopResponse = sendLoRaCommand("radio rxstop", 1500);
  loraInRxMode = false;

  if (stopResponse.startsWith("radio_rx ")) {
    Serial.println("Packet arrived while stopping RX for ping; parsing it before TX.");
    String hexPayload = stopResponse.substring(String("radio_rx ").length());
    hexPayload.trim();
    parseHexPayload(hexPayload);
  } else if (stopResponse == "radio_err") {
    Serial.println("RX ended with radio_err while preparing ping; TX can continue from idle radio state.");
  } else if (stopResponse.length() == 0) {
    Serial.println("No rxstop response. Trying TX anyway; if the module is still in RX, radio tx will fail clearly.");
  } else if (stopResponse != "ok") {
    Serial.print("Unexpected rxstop response before ping: ");
    Serial.println(stopResponse);
  }

  String txCommand = String("radio tx ") + String(LORA_PING_HEX);
  String accepted = sendLoRaCommand(txCommand, 2000);

  if (accepted != "ok") {
    Serial.print("RN2483 rejected ping TX command. Response: ");
    Serial.println(accepted.length() ? accepted : "<empty>");
    enterLoRaRx();
    return false;
  }

  String txResult = readLoRaLine(10000);
  Serial.print("RN2483 => ");
  Serial.println(txResult.length() ? txResult : "<timeout/no tx-complete response>");

  bool success = (txResult == "radio_tx_ok");
  if (!success) {
    Serial.println("Ping TX did not complete cleanly. Check antenna, frequency duty limits, and RN2483 state.");
    enterLoRaRx();
    return false;
  }

  waitForPostPingPayload();
  return true;
}

/*
    waitForPostPingPayload()
    After a ping is sent, this listens on the RN2483 for up to POST_PING_WAIT_MS
    for a sensor node to respond with a payload.
    If a packet arrives it gets parsed and published exactly like a normal receive.
    If nothing arrives (radio_err or timeout) it just returns — normal background RX takes over.
    This is what makes the manual ping button actually useful: you press it and
    within a few seconds the dashboard updates with fresh data.
*/
void waitForPostPingPayload() {
  if (!loraReady || useMockData) {
    return;
  }

  Serial.println("Post-ping RX window open. Waiting for sensor node response...");

  // Put radio back into RX so a response can come in
  loraSerial.print("radio rx 0\r\n");
  loraInRxMode = true;

  String response = readLoRaLine(POST_PING_WAIT_MS);

  if (response.startsWith("radio_rx ")) {
    loraInRxMode = false;
    Serial.println("Post-ping payload received.");
    String hexPayload = response.substring(String("radio_rx ").length());
    hexPayload.trim();
    parseHexPayload(hexPayload);
    // Re-enter background RX after handling the payload
    enterLoRaRx();
  } else if (response == "radio_err") {
    loraInRxMode = false;
    Serial.println("Post-ping RX window expired with no response. Back to background RX.");
    enterLoRaRx();
  } else {
    loraInRxMode = false;
    Serial.print("Post-ping RX got unexpected response: ");
    Serial.println(response.length() ? response : "<timeout>");
    enterLoRaRx();
  }
}

//just sends the ping every X minutes to wake up the nodes, and also publishes a status message to MQTT so the dashboard can show when it happens.
void handleAutomaticPing() {
  if (useMockData || !loraReady) {
    return;
  }

  unsigned long now = millis();
  if (now - lastPingTime >= PING_INTERVAL_MS) {
    lastPingTime = now;
    bool sent = sendLoRaPing("scheduled wake ping");

    if (mqttClient.connected()) {
      mqttClient.publish(TOPIC_STATUS, sent ? "Automatic LoRa PING sent" : "Automatic LoRa PING failed");
    }
  }
}

//transforms the hex payload from the RN2483 into the SensorData struct, and then prints it and publishes it to MQTT and Blynk.
bool parseHexPayload(String hexStr) {
  hexStr.trim();
  hexStr.toUpperCase();

  if (hexStr.length() != SENSOR_PAYLOAD_HEX_CHARS) {
    Serial.print("Bad LoRa payload length. Expected 26 hex chars for 13 bytes, got ");
    Serial.print(hexStr.length());
    Serial.print(": ");
    Serial.println(hexStr);
    return false;
  }

  uint8_t bytes[SENSOR_PAYLOAD_BYTES];
  if (!hexStringToBytes(hexStr, bytes, SENSOR_PAYLOAD_BYTES)) {
    Serial.print("Bad LoRa payload hex; dropping packet: ");
    Serial.println(hexStr);
    return false;
  }

  SensorData data;
  data.deviceId = ((uint32_t)bytes[0] << 24) |
                  ((uint32_t)bytes[1] << 16) |
                  ((uint32_t)bytes[2] << 8) |
                  (uint32_t)bytes[3];
  data.nodeTypeFlag = bytes[0];
  data.nodeType = decodeNodeType(data.nodeTypeFlag);

  // Temperature is sent as int16 in tenths of a degree instead of float.
  // That keeps the radio packet small and avoids float byte-order weirdness between devices.
  uint16_t tempRaw = ((uint16_t)bytes[4] << 8) | (uint16_t)bytes[5];
  data.temperature = ((int16_t)tempRaw) / 10.0f;

  data.humidity = bytes[6];
  data.lux = ((uint16_t)bytes[7] << 8) | (uint16_t)bytes[8];


  data.soilMoisture = ((uint16_t)bytes[9] << 8) | (uint16_t)bytes[10];
  data.waterLeak = (bytes[11] != 0);
  data.battery = bytes[12];

  lastSensorData = data;
  printSensorData(data);
  publishSensorData(data);
  return true;
}

//obvious, translates the hex string into raw bytes. Returns false if the string is not valid hex or has the wrong length.
bool hexStringToBytes(String hexStr, uint8_t* out, size_t outLen) {
  if (hexStr.length() != outLen * 2) {
    return false;
  }

  for (size_t i = 0; i < outLen; i++) {
    int hi = hexNibble(hexStr.charAt(i * 2));
    int lo = hexNibble(hexStr.charAt(i * 2 + 1));

    if (hi < 0 || lo < 0) {
      return false;
    }

    out[i] = (uint8_t)((hi << 4) | lo);
  }

  return true;
}


int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}


String bytesToHex(const uint8_t* data, size_t len) {
  const char hexChars[] = "0123456789ABCDEF";
  String hex;
  hex.reserve(len * 2);

  for (size_t i = 0; i < len; i++) {
    hex += hexChars[(data[i] >> 4) & 0x0F];
    hex += hexChars[data[i] & 0x0F];
  }

  return hex;
}

String decodeNodeType(uint8_t nodeTypeFlag) {
  if (nodeTypeFlag == 0x01) {
    return "sensor";
  }

  if (nodeTypeFlag == 0x02) {
    return "light";
  }

  return "unknown";
}

//just for serial eye candy
void printSensorData(const SensorData& data) {
  char deviceIdText[11];
  formatDeviceId(data.deviceId, deviceIdText, sizeof(deviceIdText));

  Serial.println("Parsed LoRa sensor payload:");
  Serial.print("  Device ID: ");
  Serial.println(deviceIdText);
  Serial.print("  Node type: ");
  Serial.println(data.nodeType);
  Serial.print("  Temperature: ");
  Serial.print(data.temperature, 1);
  Serial.println(" C");
  Serial.print("  Humidity: ");
  Serial.print(data.humidity);
  Serial.println(" %");
  Serial.print("  Lux: ");
  Serial.println(data.lux);
  Serial.print("  Soil moisture ADC: ");
  Serial.println(data.soilMoisture);
  Serial.print("  Water leakage: ");
  Serial.println(data.waterLeak ? "LEAK" : "dry");
  Serial.print("  Battery: ");
  Serial.print(data.battery);
  Serial.println(" %");
}

/*
    Blynk writes are left as obvious TODOs until the virtual pins are agreed.
*/
void publishSensorData(const SensorData& data) {
  char deviceIdText[11];
  formatDeviceId(data.deviceId, deviceIdText, sizeof(deviceIdText));

  String message = "{";
  message += "\"type\":\"sensorData\",";
  message += "\"deviceId\":\"";
  message += deviceIdText;
  message += "\",";
  message += "\"nodeType\":\"";
  message += data.nodeType;
  message += "\",";
  message += "\"temperature\":";
  message += String(data.temperature, 1);
  message += ",\"humidity\":";
  message += String(data.humidity);
  message += ",\"lux\":";
  message += String(data.lux);
  message += ",\"soilMoisture\":";
  message += String(data.soilMoisture);
  message += ",\"waterLeak\":";
  message += (data.waterLeak ? "true" : "false");
  message += ",\"battery\":";
  message += String(data.battery);
  message += "}";

  if (mqttClient.connected()) {
    mqttClient.publish(TOPIC_STATUS, message.c_str());

    Serial.print("Published sensor JSON to ");
    Serial.print(TOPIC_STATUS);
    Serial.print(": ");
    Serial.println(message);
  } else {
    Serial.println("MQTT not connected; parsed reading kept only in Serial output.");
  }

  // ================================================================
  // TODO — BLYNK VIRTUAL PINS
  // These are commented out because we haven't agreed on pin numbers yet.
  // When you're ready to hook this up, uncomment the lines below and
  // replace VX with the actual virtual pin number from the Blynk dashboard.
  // Don't just pick random pins — check with whoever set up the Blynk template.
  // ================================================================
  // Blynk.virtualWrite(V3, data.temperature);        // temperature in °C
  // Blynk.virtualWrite(V4, data.humidity);           // humidity %
  // Blynk.virtualWrite(V5, data.lux);                // lux (raw)
  // Blynk.virtualWrite(V6, data.soilMoisture);       // soil ADC (uncalibrated)
  // Blynk.virtualWrite(V7, data.waterLeak ? 1 : 0);  // 0 = dry, 1 = leak
  // Blynk.virtualWrite(V8, data.battery);            // battery %
}

/*
    handleMockData()
    Generates fake greenhouse sensor data while useMockData is true.
    This lets us test MQTT, the browser dashboard, JSON format, and Blynk placeholders
    before the real sensor nodes exist. It returns immediately when mock mode is off.
*/
void handleMockData() {
  if (!useMockData) {
    return;
  }

  unsigned long now = millis();
  if (now - lastMockPublishTime < PUBLISH_INTERVAL_MS) {
    return;
  }
  lastMockPublishTime = now;

  // ================================================================
  // MOCK DATA — THIS IS FAKE. SET useMockData = false WHEN REAL NODES ARE READY.
  // ================================================================
  // This generates a pretend sensor reading so you can test the whole pipeline
  // (MQTT publish, browser dashboard, Blynk writes) without needing actual hardware.
  // The fake values are hardcoded below. They go through the exact same
  // parseHexPayload() function as real data, so if mock works, real should too.
  // ================================================================
  uint8_t mockPayload[SENSOR_PAYLOAD_BYTES] = {
    0x01, 0xAA, 0xBB, 0xCC, // device ID 0x01AABBCC, sensor node
    0x00, 0xE1,             // 22.5 C in tenths
    0x3A,                   // 58 % humidity
    0x01, 0x9A,             // 410 lux
    0x07, 0x1F,             // 1823 raw soil ADC
    0x00,                   // dry
    0x53                    // 83 % battery
  };

  String mockHex = bytesToHex(mockPayload, SENSOR_PAYLOAD_BYTES);
  Serial.print("Mock LoRa payload: ");
  Serial.println(mockHex);
  parseHexPayload(mockHex);
}

/*
    formatDeviceId(deviceId, out, outSize)
    Formats a uint32 device ID as 0xXXXXXXXX for Serial logs and JSON.
    The output buffer should be at least 11 chars: 0x + 8 hex digits + null terminator.
*/
void formatDeviceId(uint32_t deviceId, char* out, size_t outSize) {
  snprintf(out, outSize, "0x%08lX", (unsigned long)deviceId);
}

// =========================
// Helper functions
// =========================
void setLed(bool on) {
  ledState = on;
  digitalWrite(LED_PIN, on ? LED_ON_LEVEL : LED_OFF_LEVEL);

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