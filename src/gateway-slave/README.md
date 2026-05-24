# ESP32 Gateway-Slave - LoRaWAN Cloud Bridge

## Table of Contents
1. [System Overview](#system-overview)
2. [Hardware Architecture](#hardware-architecture)
3. [Integration with Main Gateway](#integration-with-main-gateway)
4. [Data Structures](#data-structures)
5. [Execution Flow](#execution-flow)
6. [Methods Reference & Execution Order](#methods-reference--execution-order)
7. [Communication Protocols](#communication-protocols)
8. [Setup & Deployment](#setup--deployment)

---

## System Overview

### Purpose
The **Gateway-Slave** is an optional secondary ESP32 board that bridges LoRaWAN (cloud uplink) with the main gateway's P2P LoRa network. It acts as both:
1. **LoRaWAN relay**: Sends sensor data to Loriot/cloud backend
2. **P2P relay**: Forwards P2P LoRa packets back to main gateway via UART2

### Key Responsibilities
- **LoRaWAN Uplink**: Maintains persistent connection to Loriot cloud via OTAA join
- **Frequency Management**: Receives sleep/frequency commands from main gateway over UART2
- **P2P Passthrough**: Listens on same 868.1 MHz frequency as main network, forwards received packets to main gateway
- **Always-On Design**: Powered externally (USB), never sleeps; runs alongside main gateway

### When to Use
- **Extended range**: LoRaWAN coverage reaches areas P2P doesn't
- **Redundancy**: Sensor nodes can reach either gateway or relay through this one
- **Cloud uplinks**: Send data to Loriot, TTN, or other LoRaWAN network servers
- **Frequency monitoring**: Main gateway can command slave to change TX intervals from cloud

---

## Hardware Architecture

### Main Components

#### **ESP32 Microcontroller (Slave)**
- **Role**: Secondary control board dedicated to LoRaWAN
- **Power**: Externally powered (USB or AC adapter, does not sleep)
- **UART Ports**:
  - **UART1** (pins 16/17): Connected to RN2483 LoRaWAN module at 57600 baud
  - **UART2** (pins 18/19): Connected to main gateway for command/data bridge at 115200 baud
  - **UART0** (USB): USB Serial for debugging at 115200 baud

#### **RN2483 LoRa Module (Slave - LoRaWAN)**
- **Communication**: Connected via UART1 at 57600 baud
- **Protocol**: LoRaWAN Class A (OTAA join)
- **Network**: Loriot backend (cibicom.net) EU868 cluster
- **Purpose**: Cloud uplink for sensor data and downlink reception
- **Interface**: AT command protocol (via rn2xx3 library)
  - Commands: `sys get hweui`, `mac join otaa`, `mac tx ucnf`, etc.
  - Responses: `ok`, `accepted`, `mac_tx_ok`, `mac_rx <port> <hex>`, etc.

### Pin Mapping

```
ESP32 (Gateway-Slave) UART Connections:
├─ UART1 (LoRaWAN):
│  ├─ GPIO16 (RX) <- RN2483 TX
│  └─ GPIO17 (TX) -> RN2483 RX
├─ UART2 (Main Gateway Bridge):
│  ├─ GPIO18 (RX) <- Main Gateway GPIO19 (TX)
│  └─ GPIO19 (TX) -> Main Gateway GPIO18 (RX)
├─ GPIO23: RN2483 Reset (active-low)
├─ GPIO2: LED (status indicator)
└─ UART0 (USB Serial @ 115200):
   └─ Used for Serial.print() debugging

Baud Rates:
├─ UART1: 57600 (RN2483 standard for LoRaWAN)
├─ UART2: 115200 (slave bridge, higher speed for reliability)
└─ USB Serial: 115200 (debugging)
```

### Integration Point: Main Gateway ↔ Slave

```
Main Gateway GPIO19 (TX @ 115200)  →  Slave GPIO18 (RX)
Main Gateway GPIO18 (RX @ 115200)  ←  Slave GPIO17 (TX)

(Plus GND shared)
```

---

## Integration with Main Gateway

### Dual-Mode Operation

The slave simultaneously operates in two modes:

#### **Mode 1: LoRaWAN Class A Uplink (Primary)**
```
Slave RN2483 ← LoRaWAN network @ 868.1 MHz
    ↓
Joined to Loriot backend (OTAA credentials)
    ↓
Transmit 13-byte sensor payload on port 2
    ↓
Receive Class A downlink (1 sec after TX)
    ↓
Parse frequency/command, update sleep interval
    ↓
Acknowledge via UART2 to main gateway
```

#### **Mode 2: P2P Relay Passthrough (Secondary)**
```
Slave RN2483 also listens on 868.1 MHz in P2P mode
    ↓
Receives encrypted sensor packets from nodes
    ↓
Forward raw hex payload to main gateway via UART2
    ↓
Main gateway parses, validates, publishes to MQTT
```

### UART2 Command Protocol

**Main Gateway → Slave** (115200 baud, one command per line):
```
FREQ:<uint16>\n
  Example: "FREQ:120\n"
  Meaning: Set LoRaWAN TX interval to 120 seconds
  Response: "OK\n" or "ERR\n"

POLL\n
  Meaning: Send a poll uplink to receive queued downlinks
  Response: "OK\n" or "ERR\n"
```

**Slave → Main Gateway** (115200 baud):
```
<hex_payload>\n
  Example: "A5014947503030D2FFAA55...\n"
  Meaning: Forward P2P LoRa packet (13 bytes, hex encoded)
  Action: Main gateway parses as SensorData, publishes to MQTT

OK\n
  Meaning: Command (FREQ or POLL) was accepted

ERR\n
  Meaning: Join not established or TX failed
```

### Information Flow

```
MINUTE 0:00 ─ Main Gateway PING ────────────────────────
    ├─ Gateway: sendPing("scheduled")
    └─ Broadcast 8-byte PING on 868.1 MHz

MINUTE 0:00 ─ Slave Receives PING ──────────────────────
    ├─ Slave RN2483 in P2P RX mode
    ├─ Detects broadcast (P2P, not addressed)
    └─ (Ignores or logs for debugging)

MINUTE 0:04 ─ Slave Sends LoRaWAN Uplink ──────────────
    ├─ Slave: mac tx ucnf 2 <hex_payload>
    ├─ RN2483 transmits LoRaWAN packet to Loriot
    └─ Main gateway: sendToSlave("FREQ:118")
       ├─ Slave receives command via UART2
       ├─ Parses "FREQ:118"
       └─ Responds: "OK\n"

MINUTE 0:05 ─ Slave Receives Class A Downlink ─────────
    ├─ 1 second after LoRaWAN TX
    ├─ Loriot sends queued command (frequency update)
    ├─ Slave parses downlink port + payload
    └─ Slave responds to main gateway via UART2
       └─ Forwards parsed frequency if needed

MINUTE 2:00 ─ Cycle Repeats ────────────────────────────
    └─ Next PING / LoRaWAN TX
```

---

## Data Structures

### Sensor Data (13 bytes, from P2P relay)

**Received from gateway as hex string via UART2**:
```
<hex_payload>\n
Format: 26 hex characters = 13 bytes (same as main sensor-node format)

Structure:
Byte 0-3:   Device ID (0x01AABBCC for sensor nodes)
Byte 4-5:   Temperature (int16_t / 10)
Byte 6:     Humidity
Byte 7-8:   Lux
Byte 9-10:  Soil Moisture
Byte 11:    Water Leak
Byte 12:    Battery

Encryption: XOR with SHARED_KEY (same key as main gateway)
```

### LoRaWAN Uplink Packet

**13 bytes, port 2**:
```
[Device ID: 4B] [Temperature: 2B] [Humidity: 1B] [Lux: 2B] 
[Soil: 2B] [Leak: 1B] [Battery: 1B]

Encryption: LoRaWAN AppSKey + NwkSKey (hardware AES)
```

### LoRaWAN Downlink Packet (Class A)

**Received from Loriot, port varies**:
```
Port 2:  Frequency/sleep interval (2 bytes, big-endian)
  Example: [0x00, 0x78] = 120 seconds next TX interval
  
Custom Port: Other cloud commands as needed

Decryption: Automatic (RN2483 library handles LoRaWAN crypto)
```

### UART2 Command Packet

```
Command: "FREQ:<uint16>\n"
  Example: "FREQ:120\n" (11 bytes + newline)

Response: "OK\n" (3 bytes) or "ERR\n" (4 bytes)

Data Packet: "<hex_string>\n"
  Example: "A5014947503030D2FFAA55...\n" (27 bytes + newline)
```

---

## Execution Flow

### Boot Sequence

```
main() [Arduino startup]
  ↓
setup()
  ├─ Initialize Serial (115200 USB debug)
  ├─ Initialize GPIO (LED, reset pin)
  ├─ connectToLoRa()
  │  ├─ Reset RN2483 (GPIO23 pulse)
  │  ├─ Begin UART1 @ 57600 baud
  │  ├─ Get hardware EUI ("sys get hweui")
  │  └─ Print EUI to Serial (for Loriot registration)
  ├─ joinLoRaWAN()
  │  ├─ Set APPEUI and APPKEY (from code defines)
  │  ├─ Attempt "mac join otaa"
  │  ├─ Wait for acceptance (up to 10 seconds)
  │  └─ If success: set loraReady = true
  ├─ Initialize UART2 @ 115200 for main gateway bridge
  ├─ Initialize timing variables
  └─ Print "Slave ready"

[Loop begins]
```

### Main Loop (continuous)

```
loop() [runs thousands of times per second]
  ├─ readUart2FromGateway()
  │  └─ If line received:
  │     ├─ Check if "FREQ:<uint16>"
  │     │  ├─ Parse frequency/interval
  │     │  ├─ Update txIntervalMs
  │     │  └─ Respond: "OK\n"
  │     └─ Check if hex payload (P2P relay)
  │        ├─ Forward to main gateway later
  │        └─ Skip (main handles parsing)
  │
  ├─ readUart2PayloadsFromGateway()
  │  └─ Buffer P2P sensor packets for cloud uplink
  │
  ├─ manageLoRaWANTx()
  │  └─ If txIntervalMs elapsed:
  │     ├─ Build 13-byte payload
  │     ├─ Call sendLoRaData()
  │     │  ├─ mac tx ucnf 2 <hex>
  │     │  └─ Wait for "mac_tx_ok"
  │     └─ Listen for Class A downlink (1 sec)
  │        └─ readLoRaLine() to capture downlink
  │
  ├─ sendToGateway()
  │  └─ Forward P2P payloads to main gateway via UART2
  │
  └─ handleLed()
     └─ Blink LED to indicate activity
```

---

## Methods Reference & Execution Order

### **Phase 1: Initialization Functions** (called in `setup()`)

#### 1. `connectToLoRa()`
- **Purpose**: Initialize RN2483 LoRaWAN module with UART and reset
- **Called**: Once during setup
- **Parameters**: None
- **Flow**:
  1. Set GPIO23 (RN2483 reset pin) to HIGH
  2. Begin UART1 @ 57600 baud
  3. Pulse reset LOW → HIGH (hardware reset)
  4. Flush serial buffer
  5. Read banner from RN2483
  6. Get hardware EUI via "sys get hweui"
  7. Print EUI to Serial (user must register this on Loriot)
- **Returns**: void

#### 2. `joinLoRaWAN()`
- **Purpose**: Perform OTAA join to Loriot LoRaWAN network
- **Called**: Once during setup (critical for cloud uplink)
- **Parameters**: None
- **Flow**:
  1. Call rn2xx3 library to set APPEUI and APPKEY (from defines)
  2. Send "mac join otaa" command
  3. Wait for "accepted" response (up to 10 seconds)
  4. If joined: set loraReady = true, print success
  5. If timeout: print error, attempt retry in loop
- **Returns**: bool (true = joined, false = failed)
- **Critical**: Device must be registered on Loriot before join succeeds

---

### **Phase 2: Main Loop Functions** (called repeatedly in `loop()`)

#### 3. `readUart2FromGateway()`
- **Purpose**: Listen for commands from main gateway over UART2
- **Called**: Every loop iteration
- **Parameters**: None
- **Flow**:
  1. If slaveSerial.available():
     - Read line until '\n'
     - Check if "FREQ:<uint16>"
       - Parse interval, update txIntervalMs
       - Respond: "OK\n"
     - Else if hex string (P2P relay)
       - Buffer for later forwarding
- **Returns**: void

#### 4. `manageLoRaWANTx()`
- **Purpose**: Periodically send LoRaWAN uplinks at configured interval
- **Called**: Every loop iteration
- **Parameters**: None
- **Flow**:
  1. Check if txIntervalMs elapsed since lastTxMs
  2. If yes:
     - Build 13-byte sensor payload (or mock data)
     - Call sendLoRaData()
       - mac tx ucnf 2 <hex_payload>
       - Wait for "mac_tx_ok"
     - Update lastTxMs = millis()
     - Listen for Class A downlink (1 second window)
- **Returns**: void

#### 5. `sendLoRaData()`
- **Purpose**: Transmit LoRaWAN uplink packet
- **Called**: By manageLoRaWANTx()
- **Parameters**: None (uses global payload buffer)
- **Flow**:
  1. Convert payload to hex string
  2. Send command: "mac tx ucnf 2 <hex>"
  3. Wait for response (should be "ok" then "mac_tx_ok")
  4. Log to Serial
- **Returns**: bool (true = TX accepted, false = failed)

#### 6. `sendToGateway(const String& msg)`
- **Purpose**: Send data or acknowledgment back to main gateway over UART2
- **Called**: After processing commands or forwarding P2P packets
- **Parameters**: msg (response string)
- **Flow**:
  1. Print to slaveSerial
  2. Print newline
  3. Echo to Serial for debugging
- **Returns**: void

---

### **Phase 3: UART2 Bridge Functions**

#### 7. `handleSerialInput()`
- **Purpose**: Read commands from main gateway and process them
- **Called**: Every loop iteration
- **Parameters**: None
- **Flow**:
  1. While slaveSerial.available():
     - Read line until '\n'
     - Trim whitespace
     - Check if "FREQ:120" format
       - Extract uint16_t value
       - Update txIntervalMs
       - Respond "OK\n"
     - Else if hex string
       - Forward to gateway (or ignore if P2P relay)
- **Returns**: void

#### 8. `relayP2PPacket(const String& hexPayload)`
- **Purpose**: Forward P2P LoRa packet from slave's RN2483 back to main gateway
- **Called**: If slave RN2483 receives P2P packet while in RX mode
- **Parameters**: hexPayload (26-char hex string)
- **Flow**:
  1. Send to gateway via sendToGateway()
  2. Main gateway receives, parses as SensorData
  3. Main gateway publishes to MQTT
- **Returns**: void

---

### **Phase 4: LoRaWAN Management Functions**

#### 9. `readLoRaLine(int timeoutMs)`
- **Purpose**: Read one line of text from RN2483 (blocking until '\n' or timeout)
- **Called**: By sendLoRaData() and downlink listeners
- **Parameters**: timeoutMs (maximum time to wait)
- **Flow**:
  1. Initialize timer and empty String
  2. While millis() < timeout:
     - If loraSerial.available():
       - Read char
       - Skip '\r'
       - If '\n', trim and return
       - Else append to line
     - Delay 1ms
  3. If timeout, return whatever accumulated (possibly empty)
- **Returns**: String (one line of text, or empty if timeout)

#### 10. `flushLoRaInput()`
- **Purpose**: Clear any pending data in LoRa serial buffer
- **Called**: Before RX mode or after TX
- **Parameters**: None
- **Flow**:
  1. While loraSerial.available(): read and discard
- **Returns**: void

#### 11. `parseFrequencyCommand(const String& cmd)`
- **Purpose**: Extract uint16_t sleep interval from "FREQ:120" format
- **Called**: By handleSerialInput()
- **Parameters**: cmd (e.g., "FREQ:120")
- **Flow**:
  1. Find ':' delimiter
  2. Extract substring after ':'
  3. Convert to uint16_t
  4. Validate range (MIN_TX_INTERVAL_MS to MAX_TX_INTERVAL_MS)
  5. Return parsed value
- **Returns**: uint16_t (interval in seconds, or 0 if invalid)

---

### **Phase 5: Utility Functions**

#### 12. `bytesToHex(const uint8_t* data, size_t len)`
- **Purpose**: Convert byte array to hex string
- **Called**: By sendLoRaData()
- **Parameters**:
  - data: input byte array
  - len: number of bytes
- **Flow**:
  1. For each byte:
     - Convert high nibble to hex char
     - Convert low nibble to hex char
     - Append both to output String
  2. Return uppercase hex string
- **Returns**: String (hex representation)

#### 13. `hexToBytes(const String& hexStr, uint8_t* out, size_t outLen)`
- **Purpose**: Convert hex string to byte array
- **Called**: By payload parsing functions
- **Parameters**:
  - hexStr: hex string (e.g., "A5014947503030D2")
  - out: output byte array
  - outLen: size of output array
- **Flow**:
  1. Validate hexStr.length() == outLen * 2
  2. For each pair of hex chars:
     - Convert to nibbles (0-15)
     - Combine: byte = (high << 4) | low
  3. Return true if success
- **Returns**: bool

#### 14. `setLed(bool on)`
- **Purpose**: Turn slave LED on/off (GPIO2)
- **Called**: To indicate activity or status
- **Parameters**: on (true = LED on, false = LED off)
- **Flow**:
  1. digitalWrite(LED_PIN, on ? HIGH : LOW)
  2. Log to Serial
- **Returns**: void

---

## Communication Protocols

### UART1: RN2483 LoRaWAN Module (57600 baud)

#### LoRaWAN AT Command Format
```
Request (Slave → RN2483):
  <command>\r\n
  Example: "mac tx ucnf 2 A50149474F50\r\n"

Response (RN2483 → Slave):
  <response>\r\n
  Examples:
    "ok\r\n"           (command accepted)
    "accepted\r\n"     (join successful)
    "mac_tx_ok\r\n"    (TX complete)
    "mac_rx 2 A50149...\r\n"  (downlink received on port 2)
    "invalid_param\r\n" (error)
```

#### Common Commands
```
Initialization:
  sys get hweui           → Returns hardware EUI (register on Loriot)
  mac set deveui <id>     → Set device EUI (usually not needed for OTAA)
  
Credentials:
  mac set appeui <hex>    → Application EUI (16 hex chars)
  mac set appkey <hex>    → Application Key (32 hex chars)
  
OTAA Join:
  mac join otaa           → Initiate join request
  Response: "ok" (sent), later "accepted" or timeout
  
Transmission:
  mac tx ucnf 2 <hex>     → Send unconfirmed uplink on port 2
  mac tx cnf 2 <hex>      → Send confirmed uplink (waits for ACK)
  
  Response: "ok" (accepted), then:
    "mac_tx_ok"  (TX complete, uplink sent)
    or
    "mac_err"    (TX failed)
  
  1 second after TX: Class A receive window opens
    "mac_rx 2 <hex>"  (if downlink received)
    or (no downlink)
```

### UART2: Main Gateway Bridge (115200 baud)

#### Command Protocol
```
Main Gateway → Slave:
  <command>\n

  FREQ:<uint16>\n
    Example: "FREQ:120\n"
    Meaning: Set LoRaWAN TX interval to 120 seconds
    Response: "OK\n" or "ERR\n"

  POLL\n
    Meaning: Send a poll uplink (receive-only, no data)
    Response: "OK\n" or "ERR\n"

Slave → Main Gateway:
  <response>\n

  OK\n               Command acknowledged
  ERR\n              Error (join failed, TX failed, etc.)
  <hex_payload>\n    P2P relay packet (13 bytes, hex encoded)
                    Example: "A5014947503030D2FFAA55...\n"
```

#### Typical Exchange
```
Main → Slave: "FREQ:120\n"
Slave → Main: "OK\n"
[Later, LoRaWAN TX happens]
Slave → Main: "<hex_payload>\n" (if P2P packet received)
Main → Slave: "POLL\n"
Slave → Main: "OK\n"
```

---

## Setup & Deployment

### Prerequisites

#### Credentials from Loriot
1. **Register device on Loriot (iotnet.teracom.dk)**:
   - Select "OTAA" mode
   - Click "Generate all except DevEUI"
   - Copy APPEUI (16 hex chars)
   - Copy APPKEY (32 hex chars)

2. **Flash slave, retrieve hardware EUI from Serial Monitor**:
   ```
   Slave starting...
   RN2483 connected.
   Hardware EUI: 70B3D5xxxxxxxx
   [Copy this value]
   ```

3. **Enter Hardware EUI on Loriot**:
   - On device page, fill in Hardware EUI field
   - Device is now registered and ready to join

#### Libraries Required
```
Arduino IDE → Sketch → Include Library → Manage Libraries

Install:
  - rn2xx3 (by Ideetron) - LoRaWAN library
  - HardwareSerial (built-in)
  - Arduino core libraries
```

### Configuration

**File**: [gateway-slave/gateway-slave.cpp](gateway-slave.cpp) (lines 33-35)

```cpp
static const char* APPEUI = "0123456789ABCDEF";  // 16 hex chars from Loriot
static const char* APPKEY = "0123456789ABCDEF0123456789ABCDEF";  // 32 hex chars

#define DEFAULT_TX_INTERVAL_MS 30000UL  // 30 seconds between uplinks
```

### Flashing Procedure

```
Board: ESP32 Dev Module
Upload Speed: 921600
Port: COM[your device]
File: gateway-slave/gateway-slave.cpp
```

### Verification

**Serial Monitor Output** (115200 baud):
```
Slave starting...
RN2483 connected.
Hardware EUI: 70B3D5XXXXXXXX
Attempting LoRaWAN join...
[Waiting for "accepted"...]
LoRaWAN joined successfully!
Slave ready.
[Every 30 seconds]
Sending LoRaWAN uplink on port 2...
TX complete.
Listening for Class A downlink...
```

**Main Gateway Integration**:
```
Gateway Serial:
  <- Slave: OK
  <- Slave: A5014947503030D2FFAA55... (P2P relay)
  
Main gateway publishes to MQTT:
  {"type":"sensorData","deviceId":"0x01AABBCC",...}
```

---

## Summary

The **Gateway-Slave** extends the greenhouse monitoring system to cloud platforms by:

| Aspect | Detail |
|--------|--------|
| **Primary Role** | LoRaWAN uplink bridge to Loriot |
| **Secondary Role** | P2P relay for extended coverage |
| **Power** | Externally powered (always-on) |
| **Communication** | UART1 to RN2483, UART2 to main gateway |
| **Timing** | LoRaWAN TX every 30-3600 seconds (configurable) |
| **Security** | LoRaWAN AES encryption (automatic) |

**Key Functions**:
1. OTAA join to Loriot LoRaWAN network
2. Periodic uplink with 13-byte sensor data
3. Receive Class A downlinks (frequency/sleep updates)
4. Forward P2P packets from main network to cloud
5. UART2 bridge for gateway coordination

**Integration Points**:
- Main Gateway sends commands via UART2 ("FREQ:120\n")
- Slave sends acknowledgments ("OK\n")
- Slave forwards P2P packets for MQTT publication
- Slave's LoRaWAN uplinks provide redundant cloud connectivity

See [Main Gateway Documentation](../gateway/README.md) for system-wide architecture.
