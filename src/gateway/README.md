# ESP32 Gateway - Comprehensive Technical Documentation

## Table of Contents
1. [System Overview](#system-overview)
2. [Hardware Architecture](#hardware-architecture)
3. [Integration with Other Nodes](#integration-with-other-nodes)
4. [Data Structures](#data-structures)
5. [Execution Flow](#execution-flow)
6. [Methods Reference & Execution Order](#methods-reference--execution-order)
7. [Communication Protocols](#communication-protocols)
8. [Security & Obfuscation](#security--obfuscation)

---

## System Overview

### Purpose
The **Gateway** is the central hub of the Greenhouse monitoring system. It sits on WiFi and MQTT, acting as a bridge between remote sensor nodes (communicating via LoRa P2P radio) and a cloud MQTT dashboard. It also manages a secondary board (gateway-slave) that handles LoRaWAN uplinks to cloud infrastructure.

### Key Responsibilities
- **WiFi/MQTT Connectivity**: Maintains persistent WiFi connection and MQTT broker link for dashboard communication
- **Sensor Pinging**: Periodically wakes sensor nodes via LoRa broadcast pings (every 2 minutes)
- **Data Reception**: Listens for sensor node replies containing environmental data (temperature, humidity, lux, soil moisture, water leak, battery)
- **MQTT Publishing**: Publishes received sensor data as JSON to the MQTT broker for dashboard visualization
- **Light Control**: Computes when plants need supplemental light based on ambient lux levels and sends control commands back to nodes
- **Slave Coordination**: Bridges commands and sensor data with a secondary gateway-slave board via UART2
- **Always-On Design**: Gateway never sleeps; it maintains 24/7 connectivity and is powered externally

---

## Hardware Architecture

### Main Components

#### **ESP32 Microcontroller (Main Gateway)**
- **Role**: Central control board with WiFi and MQTT stack
- **Power**: Externally powered (does not sleep)
- **UART Ports**:
  - **UART1** (pins 16/17): Connected to RN2483 LoRa module for P2P radio with sensor nodes
  - **UART2** (pins 18/19): Connected to gateway-slave ESP32 board
  - **UART0** (USB): USB Serial for debugging and manual testing

#### **RN2483 LoRa Module (Main Gateway)**
- **Communication**: Connected via UART1 at 57600 baud
- **Protocol**: LoRa P2P (not LoRaWAN)
- **Frequency**: 868.1 MHz (European ISM band)
- **Purpose**: Raw LoRa radio with sensor nodes using proprietary packet format
- **Interface**: AT command protocol
  - Commands: `radio tx`, `radio rx`, `radio set`, `mac pause`, `sys get ver`
  - Responses: `ok`, `radio_tx_ok`, `radio_rx <hex_payload>`, `radio_err`, etc.

#### **ESP32 Microcontroller (Gateway-Slave)**
- **Role**: Secondary board dedicated to LoRaWAN protocol
- **Connection**: UART2 bridge from main gateway (pins 18→16 RX, 19→17 TX)
- **Purpose**: Separate LoRaWAN stack for cloud uplinks; main gateway bridges frequency commands and receives parsed sensor data
- **Power**: Can sleep independently

#### **RN2483 LoRa Module (Gateway-Slave)**
- **Communication**: Connected via UART1 at 57600 baud
- **Protocol**: LoRaWAN (Class A)
- **Purpose**: Cloud bridge for deeper coverage using Loriot or similar LoRaWAN network

### Pin Mapping

```
ESP32 (Main Gateway) UART Connections:
├─ UART1 (LoRa P2P):
│  ├─ GPIO16 (RX) <- RN2483 TX
│  └─ GPIO17 (TX) -> RN2483 RX
├─ UART2 (Slave Bridge):
│  ├─ GPIO18 (RX) <- ESP32 Slave GPIO17 (TX)
│  └─ GPIO19 (TX) -> ESP32 Slave GPIO16 (RX)
├─ GPIO2: RN2483 Reset (active-low)
├─ GPIO4: LED (status indicator)
└─ UART0 (USB Serial @ 115200):
   └─ Used for Serial.print() debugging

Baud Rates:
├─ UART1: 57600 (RN2483 standard)
├─ UART2: 115200 (slave bridge, faster for reliability)
└─ USB Serial: 115200 (debugging)
```

---

## Integration with Other Nodes

### System Architecture Diagram

```
                          Internet / MQTT Broker (broker.emqx.io)
                                    ↑
                                    | WiFi + MQTT
                                    |
    ┌─────────────────────────────────────────────────────────┐
    │  ESP32 Gateway (Main Board)                             │
    │  ├─ WiFi (receives commands, publishes sensor data)     │
    │  ├─ MQTT (subscriptions: ping, control; topic: status) │
    │  ├─ UART1 ← RN2483 LoRa P2P (868.1 MHz)                │
    │  └─ UART2 ← Gateway-Slave (LoRaWAN bridge)             │
    └────┬────────────────────────────────┬──────────────────┘
         │ LoRa P2P @ 868.1 MHz           │ UART2 @ 115200
         │ (Proprietary protocol)         │
         │                                │
    ┌────▼──────────────────┐      ┌────▼──────────────────────┐
    │ Sensor Nodes          │      │ ESP32 Gateway-Slave       │
    │ (Multiple instances)  │      │ ├─ LoRaWAN Class A        │
    │ ├─ Node 0x01AABBCC   │      │ ├─ RN2483 LoRaWAN         │
    │ ├─ Node 0x02CCDDEE   │      │ ├─ Loriot Cloud Bridge    │
    │ └─ ...                │      │ └─ Receives sleep/freq cmds│
    └───────────────────────┘      └───────────────────────────┘
           ↑                               ↑
           │ 13-byte encrypted            │ LoRaWAN
           │ sensor payloads              │ Uplink
           │                              │
```

### Sensor Node Integration

#### **Wake-Up Flow**
1. **Scheduled Ping** (every 2 minutes):
   - Gateway sends 8-byte broadcast PING packet (`0x50 0x49 0x4E 0x47` + 4-byte SHARED_KEY)
   - Encrypted with XOR to prevent rogue nodes

2. **Node Reception**:
   - Sensor node's RN2483 receives the ping
   - Node wakes from sleep upon radio interrupt
   - Node checks packet validity (XOR obfuscation)

3. **Sensor Data TX**:
   - Node collects temperature, humidity, lux, soil moisture, water leak, battery voltage
   - Packages into 13-byte binary struct, XOR encrypts with SHARED_KEY
   - Transmits 26-char hex string via LoRa back to gateway within ~3 seconds (BOOT_OVERHEAD_MS)

#### **Data Reception Flow**
1. **Post-Ping Listen Window** (10 seconds):
   - Gateway enters RX mode after transmitting ping
   - Listens for `radio_rx <hex>` response from RN2483

2. **Payload Parsing**:
   - Convert 26-char hex string to 13 bytes
   - XOR decrypt with SHARED_KEY
   - Extract device ID (4 bytes), temperature (2 bytes, /10), humidity, lux, soil moisture, water leak, battery

3. **Validation**:
   - Verify device ID is in APPROVED_DEVICE_IDS whitelist
   - Reject if unknown node (security & noise filtering)

4. **MQTT Publication**:
   - JSON format: `{"type":"sensorData", "deviceId":"0x01AABBCC", "temperature":23.5, "humidity":65, "lux":150, ...}`
   - Published to `esp32/myroom123/status`

#### **Light Control Flow**
1. **Lux Threshold Check**:
   - If received lux < 200 (LIGHT_THRESHOLD_LUX), plant needs light
   - If lux >= 200, light is adequate

2. **Send Light Command**:
   - If light needed: send 8-byte command to sensor node with `desiredLux=500`, `nextSleepSeconds` (calculated)
   - If light adequate: send sync-only command (no supplemental light)

3. **Node Response**:
   - Node receives command, schedules next wake-up time
   - Sensor node stays awake longer if lux control active
   - Sensor node calculates new sleep interval

### Gateway-Slave Integration (LoRaWAN Bridge)

#### **Slave Communication Protocol (UART2)**
```
Main Gateway ← UART2 → Gateway-Slave
Baud: 115200, Format: 8N1

Command Format (Main → Slave):
  "FREQ:<uint16>\n"      - Set LoRaWAN TX frequency (e.g., "FREQ:120\n")
  "SLEEP:<seconds>\n"    - Command sleep duration

Response Format (Slave → Main):
  "<hex_payload>\n"      - Forward received sensor data as hex string
  "ACK\n"                - Command acknowledged
  "ERR\n"                - Error response
```

#### **Slave Data Flow**
1. **Main gateway sends frequency command**:
   - Example: `sendToSlave("FREQ:120")` instructs slave to transmit LoRaWAN uplink on frequency 120
   - Slave receives command, updates LoRaWAN parameters, transmits uplink to cloud

2. **Slave forwards sensor data**:
   - If slave's RN2483 receives a LoRa P2P packet (same frequency, compatible modulation)
   - Slave forwards raw hex payload to main gateway via UART2
   - Main gateway parses as SensorData, publishes to MQTT (same as direct reception)
   - This allows extended coverage: sensors can talk to either gateway or relay through slave

---

## Data Structures

### SensorData (13 bytes)

```cpp
struct SensorData {
  uint32_t deviceId;      // 4 bytes: Unique sensor node identifier
  float    temperature;   // 2 bytes: Signed int16, divided by 10 (e.g., 235 = 23.5°C)
  uint8_t  humidity;      // 1 byte: 0-100%
  uint16_t lux;           // 2 bytes: Ambient light level (0-65535 lux)
  uint16_t soilMoisture;  // 2 bytes: Capacitive soil moisture reading
  bool     waterLeak;     // 1 byte: Boolean flag (any non-zero = true)
  uint8_t  battery;       // 1 byte: Battery percentage or raw ADC value (0-255)
};
```

**Binary Layout**:
```
Byte 0-3:  Device ID (big-endian uint32_t)
Byte 4-5:  Temperature (big-endian int16_t, /10)
Byte 6:    Humidity
Byte 7-8:  Lux (big-endian uint16_t)
Byte 9-10: Soil Moisture (big-endian uint16_t)
Byte 11:   Water Leak
Byte 12:   Battery
```

### PING Packet (8 bytes)

```cpp
uint8_t pingBuf[8] = {
  0x50, 0x49, 0x4E, 0x47,  // ASCII "PING" (header)
  0xA3, 0x7F, 0x2C, 0x91   // SHARED_KEY (obfuscation)
};
```

Before TX: XOR encrypted with SHARED_KEY (cycles: key[0], key[1], key[2], key[3], key[0]...)

### Light Command Packet (8 bytes)

```cpp
uint8_t lightCmd[8] = {
  // Byte 0-3: Target device ID (big-endian uint32_t)
  // Byte 4-5: Desired lux level (big-endian uint16_t, 0 if not needed)
  // Byte 6-7: Next sleep interval in seconds (big-endian uint16_t)
};
```

Before TX: XOR encrypted with SHARED_KEY

### Light Control Node Packet (5 bytes)

```cpp
uint8_t lightNodeCmd[5] = {
  // Byte 0-3: Light node device ID (e.g., 0x00000005)
  // Byte 4: Command (0x01 = ON, 0x00 = OFF)
};
```

Response (ACK): Same format with status in byte 4 (0x01 = OK, 0x00 = FAIL)

---

## Execution Flow

### Boot Sequence

```
main() [Arduino startup]
  ↓
setup()
  ├─ Initialize Serial (115200)
  ├─ Initialize GPIO (LED pin)
  ├─ connectToWiFi()
  │  ├─ WiFi.mode(WIFI_STA)
  │  ├─ WiFi.begin(SSID, PASSWORD)
  │  └─ Wait for connection (20 sec timeout)
  ├─ Initialize MQTT client
  │  ├─ setServer(broker.emqx.io, 1883)
  │  └─ setCallback(mqttCallback)
  ├─ initLoRa()
  │  ├─ Reset RN2483 (GPIO2)
  │  ├─ Begin UART1 @ 57600
  │  ├─ Get RN2483 version ("sys get ver")
  │  ├─ Pause MAC ("mac pause") to enable raw P2P mode
  │  └─ Configure 13 radio parameters (freq, SF, BW, power, CRC, etc.)
  ├─ Initialize Slave UART2 @ 115200
  ├─ Initialize timing variables (lastPingTime, lastHeartbeatTime)
  └─ Print "Gateway ready"

[Loop begins]
```

### Main Loop (runs every ~1-10ms)

```
loop() [continuous, called thousands of times per second]
  ├─ ensureWiFiConnected()
  │  └─ If disconnected, attempt WiFi.begin() every 5 sec
  ├─ If WiFi connected:
  │  ├─ If MQTT disconnected:
  │  │  └─ Retry MQTT every 3 sec (connectToMQTT)
  │  │     ├─ Connect to broker
  │  │     └─ Subscribe to topics (control, ping)
  │  └─ If MQTT connected:
  │     ├─ mqttClient.loop() [process incoming messages]
  │     └─ If 60 sec elapsed (HEARTBEAT_INTERVAL_MS):
  │        └─ publishHeartbeat() [send "Hello" + uptime to MQTT]
  ├─ If 120 sec elapsed (PING_INTERVAL_MS):
  │  └─ sendPing("scheduled") [wake all sensor nodes]
  ├─ handleSerialLightControlInput()
  │  └─ If USB Serial: '1' = turn on light, '0' = turn off (test shortcut)
  └─ handleSlaveSerial()
     └─ Read any responses from slave, parse & publish to MQTT
```

---

## Methods Reference & Execution Order

### **Phase 1: Initialization Functions** (called in `setup()`)

#### 1. `connectToWiFi()`
- **Purpose**: Establish initial WiFi connection
- **Called**: Once during setup
- **Parameters**: None (reads WIFI_SSID, WIFI_PASSWORD defines)
- **Flow**:
  1. Set WiFi mode to STA (Station)
  2. Call WiFi.begin() with credentials
  3. Wait up to 20 seconds for connection
  4. Print IP address if successful, or retry message
- **Returns**: void (no return value; uses global WiFi state)

#### 2. `initLoRa()`
- **Purpose**: Initialize RN2483 LoRa module with all radio parameters
- **Called**: Once during setup (critical; halts if fails)
- **Parameters**: None
- **Flow**:
  1. Set GPIO2 (RN2483 reset pin) to HIGH
  2. Begin UART1 @ 57600 baud
  3. Pulse reset low → high (hardware reset)
  4. Flush any stale serial data
  5. Read banner message from RN2483
  6. Get module version ("sys get ver")
  7. Send "mac pause" to disable LoRaWAN MAC
  8. Send 13 radio configuration commands:
     - `radio set mod lora` (mode)
     - `radio set freq 868100000` (frequency)
     - `radio set pwr 14` (transmit power)
     - `radio set sf sf12` (spreading factor)
     - `radio set afcbw 41.7` (AFC bandwidth)
     - `radio set rxbw 125` (RX bandwidth)
     - `radio set prlen 8` (preamble length)
     - `radio set crc on` (CRC enable)
     - `radio set iqi off` (IQ inversion)
     - `radio set cr 4/5` (coding rate)
     - `radio set wdt 60000` (watchdog timeout)
     - `radio set sync 12` (sync word)
     - `radio set bw 125` (bandwidth again)
  9. Return true if all commands return "ok", else false
- **Returns**: bool (true = ready, false = failed)
- **Critical**: If false, gateway halts (safety measure; broken radio means system cannot wake nodes)

---

### **Phase 2: Main Loop Functions** (called repeatedly in `loop()`)

#### 3. `ensureWiFiConnected()`
- **Purpose**: Maintain WiFi connection; retry if dropped
- **Called**: Every loop iteration
- **Parameters**: None
- **Flow**:
  1. Check WiFi.status() == WL_CONNECTED
  2. If connected, return immediately (no action)
  3. If disconnected and 5 sec elapsed since last retry:
     - Print "WiFi dropped"
     - Call WiFi.disconnect() then WiFi.begin() again
- **Returns**: void (no return; modifies global WiFi state)
- **Note**: Non-blocking; doesn't wait for connection

#### 4. `mqttClient.loop()`
- **Purpose**: Process incoming MQTT messages
- **Called**: Every loop iteration (if WiFi + MQTT connected)
- **Parameters**: None (calls registered callback)
- **Triggers**: mqttCallback() for messages on subscribed topics
- **Returns**: void

#### 5. `publishHeartbeat()`
- **Purpose**: Periodically send gateway status to MQTT (keep-alive)
- **Called**: Every 60 seconds (HEARTBEAT_INTERVAL_MS)
- **Parameters**: None
- **Flow**:
  1. Check if MQTT connected (safety check)
  2. Build JSON: "Hello from ESP32 | uptime: X | LED: ON/OFF"
  3. Publish to TOPIC_STATUS ("esp32/myroom123/status")
  4. Print to Serial
- **Returns**: void

#### 6. `handleSerialLightControlInput()`
- **Purpose**: Read manual light control commands from USB Serial (test shortcut)
- **Called**: Every loop iteration
- **Parameters**: None
- **Flow**:
  1. If Serial.available():
     - Read one character
     - If '1': sendLightControlCmd(true)
     - If '0': sendLightControlCmd(false)
- **Returns**: void
- **Note**: For testing only; remove when automatic light control verified

#### 7. `handleSlaveSerial()`
- **Purpose**: Read responses/data from gateway-slave over UART2
- **Called**: Every loop iteration
- **Parameters**: None
- **Flow**:
  1. While slaveSerial.available():
     - Read line until '\n'
     - Trim whitespace
     - Parse as hex payload
     - Call parseSensorPayload()
     - If valid, call publishSensorData() to MQTT
- **Returns**: void

---

### **Phase 3: WiFi/MQTT Functions** (event-driven, called from loop or callbacks)

#### 8. `connectToMQTT()`
- **Purpose**: Connect to MQTT broker and subscribe to topics
- **Called**: When MQTT disconnected and reconnect interval elapsed
- **Parameters**: None
- **Flow**:
  1. Check WiFi connected (safety check)
  2. Build unique client ID from ESP32 MAC address
  3. Call mqttClient.connect(clientId)
  4. If connected:
     - Subscribe to TOPIC_CONTROL ("esp32/myroom123/control")
     - Subscribe to TOPIC_PING ("esp32/myroom123/ping")
     - Publish connection message to TOPIC_STATUS
     - Return true
  5. If failed, print error code and return false
- **Returns**: bool (true = connected, false = failed)

#### 9. `mqttCallback(char* topic, byte* payload, unsigned int length)`
- **Purpose**: Handle incoming MQTT messages
- **Called**: Automatically by mqttClient.loop() when message arrives
- **Parameters**:
  - topic: string (e.g., "esp32/myroom123/ping")
  - payload: byte array (raw message data)
  - length: size of payload
- **Flow**:
  1. Convert payload bytes to String
  2. Print topic and message to Serial
  3. Check topic:
     - If TOPIC_PING ("esp32/myroom123/ping"):
       - If msg == "PING": call manualPingWithRetry()
       - Publish result to MQTT
     - If TOPIC_CONTROL ("esp32/myroom123/control"):
       - If msg == "ON" or "LIGHT ON": setLed(true), sendLightControlCmd(true)
       - If msg == "OFF" or "LIGHT OFF": setLed(false), sendLightControlCmd(false)
- **Returns**: void

#### 10. `publishStatus(const String& msg)`
- **Purpose**: Log status to Serial and publish to MQTT
- **Called**: From various functions (error/info reporting)
- **Parameters**: msg (status message string)
- **Flow**:
  1. Print to Serial ("STATUS: " + msg)
  2. If MQTT connected, publish to TOPIC_STATUS
- **Returns**: void

#### 11. `publishLightControlState()`
- **Purpose**: Publish current LED state to MQTT as JSON
- **Called**: When light state changes (sendLightControlCmd)
- **Parameters**: None
- **Flow**:
  1. Build JSON: `{"type":"LED", "deviceId":"0x00000005", "state":"ON"/"OFF"}`
  2. Print to Serial
  3. Publish to TOPIC_STATUS
- **Returns**: void

#### 12. `setLed(bool on)`
- **Purpose**: Turn gateway LED on/off (physical GPIO4 and state variable)
- **Called**: From mqttCallback or manual input
- **Parameters**: on (true = LED on, false = LED off)
- **Flow**:
  1. Set ledState variable
  2. digitalWrite(LED_PIN, on ? HIGH : LOW)
  3. Print status to Serial
- **Returns**: void

---

### **Phase 4: Slave Communication Functions**

#### 13. `sendToSlave(const String& cmd)`
- **Purpose**: Send command to gateway-slave board over UART2
- **Called**: When main gateway needs to instruct slave (e.g., frequency updates)
- **Parameters**: cmd (command string, e.g., "FREQ:120")
- **Flow**:
  1. Print command to slaveSerial
  2. Print newline
  3. Echo to Serial for debugging
- **Returns**: void

#### 14. `handleSlaveSerial()` (duplicate listing for reference)
- **See Phase 2, Function 7**

---

### **Phase 5: LoRa Radio Communication Functions**

#### 15. `sendLoRaCommand(const String& cmd, int timeoutMs)`
- **Purpose**: Send AT command to RN2483 and read response
- **Called**: During initLoRa() and when TX/RX packets
- **Parameters**:
  - cmd: AT command (e.g., "radio tx A50149474F50")
  - timeoutMs: how long to wait for response
- **Flow**:
  1. Print command to loraSerial
  2. Send "\r\n" terminator
  3. Call readLoRaLine(timeoutMs) to read response
  4. Print response to Serial
  5. Return response string
- **Returns**: String (response from RN2483)

#### 16. `readLoRaLine(int timeoutMs)`
- **Purpose**: Read one line of text from RN2483 (blocking until '\n' or timeout)
- **Called**: By sendLoRaCommand() and waitForSensorReply()
- **Parameters**: timeoutMs (maximum time to wait)
- **Flow**:
  1. Initialize timer and empty String
  2. While millis() < timeout:
     - If loraSerial.available():
       - Read char
       - Skip '\r' (carriage return)
       - If '\n', trim and return line
       - Else append to line
     - Delay 1ms (yield)
  3. If timeout, trim and return whatever accumulated (possibly empty)
- **Returns**: String (one line of text, or empty if timeout)

#### 17. `flushLoRaInput()`
- **Purpose**: Clear any pending data in LoRa serial buffer
- **Called**: During initLoRa() and before RX mode
- **Parameters**: None
- **Flow**:
  1. While loraSerial.available(): read and discard
- **Returns**: void

#### 18. `stopRx()`
- **Purpose**: Stop active RX listening on LoRa radio
- **Called**: If RX times out or error detected
- **Parameters**: None
- **Flow**:
  1. Send "radio rxstop\r\n" to RN2483
  2. Read up to 2 lines of response (drain buffer)
  3. Print to Serial
- **Returns**: void

---

### **Phase 6: Sensor Ping & Communication Functions** (core data flow)

#### 19. `sendPing(const char* reason)` 🎯 **CRITICAL**
- **Purpose**: Broadcast wake-up PING to all sensor nodes
- **Called**: Every 120 seconds (PING_INTERVAL_MS) or manually from MQTT
- **Parameters**: reason (string for logging, e.g., "scheduled" or "manual MQTT retry")
- **Flow**:
  1. Check loraReady (safety check)
  2. Build 8-byte PING packet: 0x50494E47 (ASCII "PING") + SHARED_KEY (4 bytes)
  3. Call bytesToHex() to convert to 26-char hex string
  4. Send command: "radio tx [hex]"
  5. Read response (should be "ok")
  6. If not "ok", log and return false
  7. Wait for "radio_tx_ok" (confirms TX complete, ~1-5 sec)
  8. Call waitForSensorReply(POST_PING_WAIT_MS = 10 sec)
  9. Return true if reply received, false if timeout/error
- **Returns**: bool (true = at least one sensor replied)

#### 20. `manualPingWithRetry()` 🎯 **CRITICAL**
- **Purpose**: Retry PING sequence when manually triggered via MQTT
- **Called**: From mqttCallback when "PING" message received on TOPIC_PING
- **Parameters**: None
- **Flow**:
  1. Save original lastPingTime (to restore if all retries fail)
  2. Loop for up to MANUAL_PING_TIMEOUT_MS (150 sec total):
     - Call sendPing("manual MQTT retry")
     - If success, return true immediately
     - If failed, wait RETRY_INTERVAL_MS (15 sec) before next attempt
     - Repeat
  3. If timeout reached, restore original lastPingTime and return false
- **Returns**: bool (true = got reply within 150 sec, false = all retries expired)

#### 21. `waitForSensorReply(int timeoutMs)` 🎯 **CRITICAL**
- **Purpose**: Enter RX mode and listen for sensor node reply
- **Called**: By sendPing() after broadcasting PING
- **Parameters**: timeoutMs (listen window, typically 10 sec)
- **Flow**:
  1. Send "radio rx 0" command (0 = listen until packet or watchdog)
  2. If not "ok", return false
  3. Call readLoRaLine(timeoutMs) to wait for response
  4. Check response:
     - If "radio_rx [hex]":
       - Extract hex payload
       - Convert hex string to 13 bytes (hexToBytes)
       - XOR decrypt with SHARED_KEY (xorWithKey)
       - Parse into SensorData struct (parseSensorPayload)
       - Validate device ID in whitelist (isApprovedDevice)
       - If invalid, reject and return false
       - If valid:
         - Calculate if light needed (lux < 200?)
         - Calculate nextSleepSeconds
         - Call publishSensorData() to MQTT
         - Delay LIGHT_CMD_PRE_DELAY_MS (400ms) for RX turnaround
         - Call sendLightCommand() to transmit control packet
         - Call sendLightControlCmd() to turn light on/off if needed
         - Return true
     - If "radio_err" or timeout: return false
- **Returns**: bool (true = valid sensor reply processed, false = no valid reply)

#### 22. `parseSensorPayload(const String& hexStr, SensorData& out)`
- **Purpose**: Decode 26-char hex string to 13-byte SensorData struct
- **Called**: By waitForSensorReply() and handleSlaveSerial()
- **Parameters**:
  - hexStr: 26 hex characters (13 bytes)
  - out: reference to SensorData struct (output)
- **Flow**:
  1. Validate hex string length (must be 26 chars)
  2. Call hexToBytes() to convert hex to binary
  3. Extract fields (big-endian):
     - Bytes 0-3: deviceId (uint32_t)
     - Bytes 4-5: temperature as int16_t, divide by 10 → float
     - Byte 6: humidity
     - Bytes 7-8: lux
     - Bytes 9-10: soilMoisture
     - Byte 11: waterLeak (non-zero = true)
     - Byte 12: battery
  4. Return true if success, false if parse error
- **Returns**: bool (true = valid payload, false = malformed)

#### 23. `publishSensorData(const SensorData& d, bool needsLight)`
- **Purpose**: Format sensor data as JSON and publish to MQTT
- **Called**: By waitForSensorReply() and handleSlaveSerial()
- **Parameters**:
  - d: SensorData struct with all readings
  - needsLight: boolean indicating if lux below threshold
- **Flow**:
  1. Build JSON object:
     ```json
     {
       "type": "sensorData",
       "deviceId": "0x01AABBCC",
       "temperature": 23.5,
       "humidity": 65,
       "lux": 150,
       "soilMoisture": 512,
       "waterLeak": false,
       "battery": 95,
       "needsLight": true
     }
     ```
  2. Print to Serial
  3. If MQTT connected, publish to TOPIC_STATUS
- **Returns**: void

#### 24. `calculateNextSleepSeconds()` ⏰
- **Purpose**: Compute next sleep interval for sensor node
- **Called**: By waitForSensorReply() before sending light command
- **Parameters**: None (uses global lastPingTime and millis())
- **Flow**:
  1. Calculate time to next ping: (lastPingTime + PING_INTERVAL_MS) - millis()
  2. Subtract BOOT_OVERHEAD_MS (3 sec for node to boot)
  3. Convert to seconds
  4. Clamp to MIN_SLEEP_S (10) and MAX_SLEEP_S (65000)
  5. Log calculated value to Serial
  6. Return as uint16_t
- **Returns**: uint16_t (sleep seconds)

---

### **Phase 7: Light Control Functions**

#### 25. `sendLightCommand(uint32_t targetDeviceId, uint16_t desiredLux, uint16_t nextSleepS)`
- **Purpose**: Send 8-byte control packet to sensor node with lux target and sleep interval
- **Called**: By waitForSensorReply() after receiving sensor data
- **Parameters**:
  - targetDeviceId: device ID of sensor node (e.g., 0x01AABBCC)
  - desiredLux: target lux level (0 if no light needed, 500 if needed)
  - nextSleepS: next sleep interval in seconds
- **Flow**:
  1. Check loraReady
  2. Build 8-byte packet (big-endian):
     - Bytes 0-3: targetDeviceId
     - Bytes 4-5: desiredLux
     - Bytes 6-7: nextSleepS
  3. XOR encrypt with SHARED_KEY
  4. Convert to 16-char hex string
  5. Send "radio tx [hex]"
  6. Wait for "radio_tx_ok"
  7. Log packet details to Serial
  8. Return true if TX successful, false if failed
- **Returns**: bool

#### 26. `sendLightControlCmd(bool turnOn)` 💡
- **Purpose**: Send ON/OFF command to light node (device 0x00000005)
- **Called**: By mqttCallback (MQTT control) or handleSerialLightControlInput (manual test)
- **Parameters**: turnOn (true = ON, false = OFF)
- **Flow**:
  1. Check loraReady
  2. Retry up to maxRetries (currently 1)
  3. Build packet with light node device ID (0x00000005) and command (0x01 for ON, 0x00 for OFF)
  4. Send "radio tx [hex]"
  5. Call waitForLightAck(turnOn) to verify ACK received
  6. If ACK OK, call publishLightControlState()
  7. If failed, retry
  8. Return true if at least one retry succeeded
- **Returns**: bool

#### 27. `waitForLightAck(bool turnOn)` 🔄
- **Purpose**: Wait for light node to acknowledge command
- **Called**: By sendLightControlCmd()
- **Parameters**: turnOn (whether this was ON or OFF command)
- **Flow**:
  1. Enter RX mode ("radio rx 0")
  2. Wait ACK_TIMEOUT_MS (5 sec) for "radio_rx [hex]" response
  3. Decode response:
     - If device ID matches light node (0x00000005)
     - If status byte is 0x01: ACK OK (return 1)
     - If status byte is 0x00: ACK FAIL (return 0)
  4. If timeout or error: return -1
- **Returns**: int (1 = OK, 0 = FAIL, -1 = TIMEOUT)

---

### **Phase 8: Utility Functions**

#### 28. `makeDevicePacket(uint32_t deviceId, uint8_t value)`
- **Purpose**: Create 5-byte device ID + value packet, XOR encrypt, return as hex string
- **Called**: By sendLightControlCmd()
- **Parameters**:
  - deviceId: target device (e.g., 0x00000005 for light node)
  - value: command byte (0x00 = OFF, 0x01 = ON)
- **Flow**:
  1. Build 5-byte buffer: [ID high, ID mid-high, ID mid-low, ID low, value]
  2. XOR encrypt with SHARED_KEY
  3. Convert to 10-char hex string
  4. Return
- **Returns**: String (10-char hex)

#### 29. `hexToBytes(const String& hexStr, uint8_t* out, size_t outLen)`
- **Purpose**: Convert hex string to byte array
- **Called**: By parseSensorPayload(), waitForSensorReply()
- **Parameters**:
  - hexStr: hex string (e.g., "A50149474F50")
  - out: output byte array
  - outLen: size of output array
- **Flow**:
  1. Validate hexStr.length() == outLen * 2
  2. For each pair of hex chars:
     - Call hexNibble() to convert each char to 0-15
     - Combine: byte = (high nibble << 4) | low nibble
  3. Return true if success, false if format error
- **Returns**: bool

#### 30. `bytesToHex(const uint8_t* data, size_t len)`
- **Purpose**: Convert byte array to hex string
- **Called**: By sendPing(), sendLightCommand(), makeDevicePacket()
- **Parameters**:
  - data: input byte array
  - len: number of bytes
- **Flow**:
  1. For each byte:
     - Convert high nibble (0-15) to hex char ('0'-'9', 'A'-'F')
     - Convert low nibble to hex char
     - Append both to output String
  2. Return uppercase hex string
- **Returns**: String (hex representation)

#### 31. `hexNibble(char c)`
- **Purpose**: Convert single hex character ('0'-'9', 'a'-'f', 'A'-'F') to value (0-15)
- **Called**: By hexToBytes()
- **Parameters**: c (single hex character)
- **Flow**:
  1. If c >= '0' && c <= '9': return c - '0'
  2. If c >= 'a' && c <= 'f': return c - 'a' + 10
  3. If c >= 'A' && c <= 'F': return c - 'A' + 10
  4. Else: return -1 (error)
- **Returns**: int (0-15, or -1 for invalid)

#### 32. `xorWithKey(uint8_t* data, size_t len)`
- **Purpose**: XOR encrypt/decrypt data with SHARED_KEY (cycles through 4-byte key)
- **Called**: By sendPing(), waitForSensorReply(), sendLightCommand(), makeDevicePacket()
- **Parameters**:
  - data: byte array to encrypt/decrypt (modified in place)
  - len: length of data
- **Flow**:
  1. For each byte at index i:
     - data[i] ^= SHARED_KEY[i % 4]
  2. (XOR is reversible: apply twice to get original)
- **Returns**: void (modifies data in place)

#### 33. `isApprovedDevice(uint32_t id)` 🔐
- **Purpose**: Check if device ID is in whitelist of approved sensor nodes
- **Called**: By waitForSensorReply() to validate received device ID
- **Parameters**: id (device ID to check)
- **Flow**:
  1. Loop through APPROVED_DEVICE_IDS array
  2. If id matches any entry, return true
  3. If no match found, return false
- **Returns**: bool (true = approved, false = rogue/unknown node)

---

## Communication Protocols

### UART1: RN2483 LoRa Module (57600 baud)

#### AT Command Format
```
Request (Gateway → RN2483):
  <command>\r\n
  Example: "radio tx A50149474F50\r\n"

Response (RN2483 → Gateway):
  <response>\r\n
  Examples:
    "ok\r\n"
    "radio_tx_ok\r\n"
    "radio_rx A501494D4F50\r\n"
    "radio_err\r\n"
    "invalid_param\r\n"
```

#### Common Commands
```
Initialization:
  sys get ver              → Returns RN2483 firmware version
  mac pause                → Disable LoRaWAN MAC, enable raw LoRa P2P
  
Radio Configuration:
  radio set freq 868100000 → Set frequency
  radio set sf sf12        → Set spreading factor
  radio set pwr 14         → Set TX power
  radio set crc on         → Enable CRC
  
Transmission:
  radio tx <hex_payload>   → Transmit packet (hex string)
  Response: "ok" (accepted), then "radio_tx_ok" (completed)
  
Reception:
  radio rx 0               → Listen until packet or watchdog
  Response: "radio_rx <hex_payload>" (packet received)
           or "radio_err" (timeout/error)
  
  radio rxstop             → Stop RX mode
  Response: "ok"
```

### UART2: Gateway-Slave Bridge (115200 baud)

#### Command Protocol
```
Main Gateway → Slave:
  <command>\n
  Examples:
    "FREQ:120\n"     - Set LoRaWAN TX frequency
    "SLEEP:3600\n"   - Set sleep duration

Slave → Main Gateway:
  <hex_payload>\n   - Forward received sensor data
  "ACK\n"           - Command acknowledged
  "ERR\n"           - Error
```

### MQTT Protocol

#### Topics

**Subscriptions (gateway receives)**:
- `esp32/myroom123/ping`: Send "PING" to trigger manual wake-up sequence
- `esp32/myroom123/control`: Send "ON"/"OFF" or "LIGHT_ON"/"LIGHT_OFF" to control LED

**Publications (gateway sends)**:
- `esp32/myroom123/status`: All outgoing messages (heartbeat, sensor data, status)

#### Message Formats

**Heartbeat**:
```
Hello from ESP32 | uptime(s): 12345 | LED: ON
```

**Sensor Data**:
```json
{
  "type": "sensorData",
  "deviceId": "0x01AABBCC",
  "temperature": 23.5,
  "humidity": 65,
  "lux": 150,
  "soilMoisture": 512,
  "waterLeak": false,
  "battery": 95,
  "needsLight": true
}
```

**Light Control State**:
```json
{
  "type": "LED",
  "deviceId": "0x00000005",
  "state": "ON"
}
```

**Status Messages**:
```
Manual LoRa PING received a sensor reply
Manual LoRa PING retry window expired
RN2483 init failed
WiFi connected, IP: 192.168.1.100
```

---

## Security & Obfuscation

### XOR Encryption Mechanism

#### Purpose
- **Not cryptographically strong**, but effective for:
  - Filtering out random RF noise
  - Rejecting packets from rogue/unauthorized nodes
  - Simple obfuscation for casual eavesdropping

#### Implementation
```cpp
static const uint8_t SHARED_KEY[4] = { 0xA3, 0x7F, 0x2C, 0x91 };

void xorWithKey(uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    data[i] ^= SHARED_KEY[i % 4];  // Cycles: key[0], key[1], key[2], key[3], key[0]...
  }
}
```

#### Encryption/Decryption Process

**Transmit (Sensor Node)**:
1. Build 13-byte SensorData struct
2. Call xorWithKey(data, 13)
3. Convert 13 bytes to 26-char hex string
4. Transmit via LoRa radio

**Receive (Gateway)**:
1. Receive 26-char hex string from radio
2. Convert to 13 bytes
3. Call xorWithKey(data, 13) [XOR is reversible]
4. Parse decrypted bytes into SensorData struct

#### PING/ACK Encryption

**Gateway Sends PING**:
1. Build 8-byte packet: 0x50494E47 (ASCII "PING") + 0xA37F2C91 (SHARED_KEY)
2. Call xorWithKey(pingBuf, 8)
3. Transmit 16-char hex string

**Sensor Receives PING**:
1. Receive 16-char hex string
2. Convert to 8 bytes
3. Call xorWithKey(data, 8)
4. Check if bytes 0-3 == "PING" and bytes 4-7 == SHARED_KEY
5. If match, accept as valid wake-up signal

#### Reversibility Property
```
Original:        [0x50, 0x49, 0x4E, 0x47, ...]
After XOR once:  [0xF3, 0x76, 0x72, 0xD4, ...]
After XOR again: [0x50, 0x49, 0x4E, 0x47, ...]  ← Back to original
```

### Whitelist Validation

#### Approved Device Array
```cpp
static const uint32_t APPROVED_DEVICE_IDS[] = {
  0x01AABBCC   // sensor node 1
};
```

#### Validation Flow
1. Gateway receives encrypted SensorData packet
2. Decrypt with XOR
3. Extract deviceId (first 4 bytes)
4. Check if deviceId exists in APPROVED_DEVICE_IDS
5. If yes: process and publish to MQTT
6. If no: reject with "Rejected: unknown device 0xXXXXXXXX"

#### Security Implications
- **Prevents rogue nodes** from sending fake sensor data to dashboard
- **Prevents interference** from other LoRa devices in the area
- **Simple whitelist** is maintainable without centralized PKI
- **But not cryptographically secure**: anyone with SHARED_KEY can forge packets

---

## Summary of Data Flow

### Typical 2-Minute Cycle

```
T=0s:     [Scheduled PING Sent]
          ├─ sendPing("scheduled")
          ├─ Transmit 8-byte PING via LoRa
          └─ Wait for radio_tx_ok

T=0.5s:   [Node Wakes Up]
          ├─ Sensor node RN2483 receives PING broadcast
          ├─ Node MCU wakes from sleep
          └─ Node collects sensor data (DHT22, TSL2561, capacitive sensor, etc.)

T=0.5-1s: [Listening Window Opens]
          ├─ Gateway enters RX mode
          ├─ sendCommand("radio rx 0")
          └─ Listen timeout: 10 seconds

T=1-2s:   [Node Sends Reply]
          ├─ Sensor node builds 13-byte SensorData struct
          ├─ XOR encrypt with SHARED_KEY
          ├─ Convert to 26-char hex string
          └─ Transmit via LoRa radio

T=1.5s:   [Gateway Receives Data]
          ├─ "radio_rx 50494E4749FF1FA2B2..."
          ├─ Extract hex payload
          ├─ XOR decrypt
          ├─ parseSensorPayload() → SensorData struct
          ├─ Validate device ID (whitelist check)
          ├─ Calculate if light needed (lux < 200?)
          ├─ publishSensorData() to MQTT
          └─ JSON published to "esp32/myroom123/status"

T=1.9s:   [Light Control]
          ├─ If lux < 200:
          │  ├─ calculateNextSleepSeconds()
          │  ├─ sendLightCommand(deviceId, 500, nextSleepS)
          │  └─ sendLightControlCmd(true) → turn light ON
          └─ If lux >= 200:
             ├─ sendLightCommand(deviceId, 0, nextSleepS) → sync-only
             └─ Do NOT turn light on

T=2s:     [Node Returns to Sleep]
          ├─ Sensor node receives light/sync command (if still listening)
          ├─ Updates next sleep interval from command payload
          └─ Enters deep sleep

T=120s:   [Cycle Repeats]
          ├─ Heartbeat published at T=60s, 120s, 180s, etc.
          ├─ Next PING sent at T=120s
          └─ Loop continues
```

---

## Testing & Debugging

### Serial Monitor Output

```
=== Greenhouse Gateway starting ===
Connecting to WiFi: WIFI
WiFi connected, IP: 192.168.1.50

RN2483 banner: RN2483 1.0.5 Dec 19 2017
RN2483 version: RN2483 1.0.5 Dec 19 2017
RN2483 <= mac pause
RN2483 => 200,862,000
RN2483 <= radio set mod lora
RN2483 => ok
... [13 more radio config commands] ...
Gateway ready.

MQTT connect to broker.emqx.io:1883
MQTT connected

[Every 120 seconds]
Sending PING (scheduled)
RN2483 <= radio tx 50494E47A37F2C91
RN2483 => ok
RN2483 => radio_tx_ok
Listening for sensor reply (10000 ms)...
RN2483 <= radio rx 0
RN2483 => ok
RN2483 => radio_rx A5014947503030D2
RX payload hex: A5014947503030D2...
Sensor: {"type":"sensorData","deviceId":"0x01AABBCC","temperature":23.5,"humidity":65,"lux":150,...}

[Every 60 seconds]
Heartbeat: Hello from ESP32 | uptime(s): 3600 | LED: ON
```

### Manual Testing via MQTT

```bash
# Terminal 1: Subscribe to status updates
mosquitto_sub -h broker.emqx.io -t "esp32/myroom123/status"

# Terminal 2: Trigger manual PING
mosquitto_pub -h broker.emqx.io -t "esp32/myroom123/ping" -m "PING"

# Expected output (Terminal 1):
# {"type":"sensorData","deviceId":"0x01AABBCC",...}

# Terminal 3: Control light
mosquitto_pub -h broker.emqx.io -t "esp32/myroom123/control" -m "ON"
mosquitto_pub -h broker.emqx.io -t "esp32/myroom123/control" -m "OFF"
```

---

## Configuration & Tuning

### Key Timing Parameters
```cpp
PING_INTERVAL_MS         = 120,000  (2 min)  - Lower = more responsive, higher power
POST_PING_WAIT_MS        = 10,000   (10 sec) - RX listen window
HEARTBEAT_INTERVAL_MS    = 60,000   (1 min)  - MQTT keep-alive
MQTT_RECONNECT_INTERVAL  = 3,000    (3 sec)  - Retry connection if dropped
BOOT_OVERHEAD_MS         = 3,000    (3 sec)  - Node wake-up time buffer
```

### Adjusting for Your Environment
- **Increase PING_INTERVAL_MS** (e.g., 180,000): Less frequent pings save sensor battery but slower response time
- **Decrease PING_INTERVAL_MS** (e.g., 60,000): More responsive but uses more sensor power
- **Increase POST_PING_WAIT_MS** (e.g., 15,000): Better for weak RF conditions but uses more gateway power

---

## Conclusion

The **Gateway** implements a complete IoT system bridging LoRa sensor nodes, WiFi connectivity, and cloud MQTT infrastructure. Its hierarchical execution flow (initialization → main loop → event-driven callbacks) ensures reliable 24/7 operation while managing multiple communication protocols (UART, LoRa AT commands, MQTT, WiFi) simultaneously.

Key design principles:
- **Always-on architecture**: Gateway never sleeps; external power required
- **Distributed sleep**: Sensor nodes sleep between pings to conserve battery
- **Secure whitelist**: Only approved devices can publish sensor data
- **Automatic light control**: Lux-based decision-making for supplemental grow lights
- **Extensible slave bridge**: Secondary LoRaWAN board adds cloud uplink capability without main code changes
