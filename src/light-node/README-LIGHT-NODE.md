# ESP32 Light Node - Smart Grow Light Controller

## Table of Contents
1. [System Overview](#system-overview)
2. [Hardware Architecture](#hardware-architecture)
3. [Integration with Gateway](#integration-with-gateway)
4. [Data Structures](#data-structures)
5. [Execution Flow](#execution-flow)
6. [Methods Reference](#methods-reference)
7. [Communication Protocol](#communication-protocol)
8. [Setup & Deployment](#setup--deployment)

---

## System Overview

### Purpose
The **Light Node** is a specialized ESP32 device that receives ON/OFF commands from the main gateway and controls supplemental grow lights via GPIO relay or direct LED output. It listens continuously on the same LoRa frequency as the sensor network and executes light commands in real-time.

### Key Responsibilities
- **Command Reception**: Continuously listen for gateway light ON/OFF commands on 868.1 MHz
- **LED Control**: Switch GPIO pin HIGH/LOW to drive relay or LED
- **Acknowledgment**: Transmit ACK packet back to gateway confirming command success/failure
- **Always-On**: No sleep; powered externally via USB or AC adapter
- **Fixed Device ID**: Hardcoded as 0x00000005 (light node identifier)

### When to Use
- **Automatic light control**: Gateway sends ON command when lux drops below threshold
- **Real-time response**: Grow lights activate immediately based on ambient conditions
- **Feedback loop**: ACK packet confirms light state change to dashboard
- **Scalable**: Can expand to multiple light nodes (different device IDs) if needed

---

## Hardware Architecture

### Main Components

#### **ESP32 Microcontroller (Light Node)**
- **Role**: Light control relay driver
- **Power**: USB or AC adapter (always-on, no battery)
- **UART Ports**:
  - **UART1** (pins 18/19): Connected to RN2483 LoRa module at 57600 baud
  - **UART0** (USB): USB Serial for debugging at 115200 baud
- **GPIO Output**:
  - **GPIO2**: LED control or relay driver (active-HIGH)
- **Device ID**: 0x00000005 (hard-coded, all light nodes share this or use variants like 0x00000005, 0x00000006, etc.)

#### **RN2483 LoRa Module (P2P)**
- **Communication**: Connected via UART1 at 57600 baud
- **Protocol**: LoRa P2P (same as sensor nodes, proprietary mode)
- **Frequency**: 868.1 MHz (European ISM band)
- **Purpose**: Receive ON/OFF commands from gateway, send ACK back
- **Interface**: AT command protocol

### Pin Mapping

```
ESP32 (Light Node) Pin Configuration:
├─ UART1 (LoRa P2P):
│  ├─ GPIO18 (RX) <- RN2483 TX
│  └─ GPIO19 (TX) -> RN2483 RX
├─ GPIO23: RN2483 Reset (active-low)
├─ GPIO2: LED/Relay Output (active-HIGH)
│  └─ Drive relay coil or direct LED control
│      HIGH = Light ON
│      LOW = Light OFF
└─ UART0 (USB Serial @ 115200):
   └─ Used for Serial.print() debugging

Baud Rates:
├─ UART1: 57600 (RN2483 standard)
└─ USB Serial: 115200 (debugging)
```

### Relay Wiring (if controlling external load)

```
GPIO2 (ESP32)
    ↓
    [220Ω resistor]
    ↓
    [NPN transistor base]
    |
    [Relay coil]  ← GND
    
NC (normally closed) contact
    ↓
    [Grow light power supply 120V AC]
    ↓
    [Grow light fixture]
```

---

## Integration with Gateway

### Command Reception Flow

```
T=0s    Gateway detects low lux (< 200 lux)
        └─ calculateNextSleepSeconds()
           └─ sendLightControlCmd(true)

T=0-100ms  Gateway RN2483 TX:
           ├─ Build 5-byte packet: [0x00,0x00,0x00,0x05][0x01 = ON]
           ├─ XOR encrypt with SHARED_KEY
           ├─ Convert to hex: "0000000501ABCDEF" (10 chars + check)
           └─ Transmit: "radio tx 0000000501ABCDEF..."

T=0.5s  Light Node RN2483 receives broadcast
        └─ RN2483 radio interrupt (in RX mode)

T=0.5s-1s  Light Node MCU wakes on RX event:
           ├─ "radio_rx 0000000501XXXXXX"
           ├─ Decrypt (XOR): [0x00,0x00,0x00,0x05][0x01]
           ├─ Check device ID: matches 0x00000005 ✓
           ├─ Check command: 0x01 = ON ✓
           ├─ digitalWrite(GPIO2, HIGH) → Light ON
           ├─ Print "Light ON" to Serial
           └─ Store cmd_state = true

T=1-2s  Light Node builds ACK packet:
        ├─ 5-byte ACK: [0x00,0x00,0x00,0x05][0x01 = success]
        ├─ XOR encrypt
        ├─ Convert to hex
        └─ Transmit: "radio tx ..."

T=2-3s  Gateway (still in RX mode) receives ACK:
        └─ publishLightControlState() → MQTT
           └─ {"type":"LED","state":"ON"}
```

### Graceful Degradation

```
Scenario 1: ACK Received Successfully
  Gateway receives ACK with status=0x01
  └─ publishLightControlState() with state="ON"
  └─ Dashboard shows light successfully turned ON

Scenario 2: ACK Lost (timeout after ACK_TIMEOUT_MS = 5s)
  Gateway does NOT receive ACK
  └─ Retries once (maxRetries = 1)
  └─ If still no ACK: publishStatus("Light command may have failed")
  └─ Dashboard shows unknown state
  └─ Light may be ON (command succeeded but ACK lost)
  └─ Light may be OFF (command failed)
  
  Note: This is acceptable; user can manually verify LED status

Scenario 3: Command Never Received
  Light Node not powered or out of range
  └─ Gateway: ACK timeout → retry
  └─ After retries: "Light command failed"
```

---

## Data Structures

### Light Control Command (5 bytes, from gateway)

**Sent by gateway, received by light node**:
```
uint8_t lightCmd[5] = {
  0x00,           // Byte 0: Device ID high byte
  0x00,           // Byte 1: Device ID mid-high byte
  0x00,           // Byte 2: Device ID mid-low byte
  0x05,           // Byte 3: Device ID low byte (0x00000005)
  0x01            // Byte 4: Command (0x01 = ON, 0x00 = OFF)
};

Total: 4-byte device ID + 1-byte command = 5 bytes
Transmitted as 10-character hex string: "0000000501"

Encryption: XOR with SHARED_KEY (cycles through 4 bytes)
Before XOR: [0x00, 0x00, 0x00, 0x05, 0x01]
After XOR:  [0x00^0xA3, 0x00^0x7F, 0x00^0x2C, 0x05^0x91, 0x01^0xA3]
           = [0xA3, 0x7F, 0x2C, 0x94, 0xA2]
Hex: "A37F2C94A2"
```

### Light ACK Packet (5 bytes, from light node)

**Response sent by light node to gateway**:
```
uint8_t ack[5] = {
  0x00,           // Byte 0: Device ID high byte
  0x00,           // Byte 1: Device ID mid-high byte
  0x00,           // Byte 2: Device ID mid-low byte
  0x05,           // Byte 3: Device ID low byte (0x00000005)
  0x01            // Byte 4: Status (0x01 = success, 0x00 = failure)
};

Encryption: Same XOR process as command
```

### Command Values

```cpp
#define CMD_LIGHT_ON   0x01
#define CMD_LIGHT_OFF  0x00
#define ACK_OK         0x01
#define ACK_FAIL       0x00

Examples:
  "0000000501" → Turn light ON, expect ACK with 0x01
  "0000000500" → Turn light OFF, expect ACK with 0x00
  "0000000501" with status=0x00 → Command failed (light not responding)
```

---

## Execution Flow

### Boot Sequence

```
main() [Arduino startup]
  ↓
setup()
  ├─ Initialize Serial (115200 USB debug)
  ├─ Initialize GPIO (LED/relay pin, reset pin)
  │  └─ Set GPIO2 to OUTPUT, initial state LOW (light OFF)
  │
  ├─ connectToLoRa()
  │  ├─ Reset RN2483 (GPIO23 pulse)
  │  ├─ Begin UART1 @ 57600 baud
  │  ├─ Get version, pause MAC, configure radio (13 commands)
  │  └─ Set loraReady = true
  │
  └─ Print "Light node ready"
     └─ Initial state: LED OFF, listening for commands

[Loop begins]
```

### Main Loop (continuous listening)

```
loop() [runs continuously]
  ├─ Ensure LoRa RX active:
  │  └─ If not in RX mode: "radio rx 0"
  │
  ├─ Check for incoming command:
  │  └─ readLoRaLine(timeout = 100ms)
  │     ├─ If "radio_rx <hex>" received:
  │     │  ├─ Extract 5-byte payload
  │     │  ├─ Decrypt (XOR)
  │     │  ├─ Check device ID (must be 0x00000005)
  │     │  ├─ Extract command (0x01 = ON, 0x00 = OFF)
  │     │  │
  │     │  ├─ If device ID matches:
  │     │  │  ├─ If command == 0x01:
  │     │  │  │  ├─ digitalWrite(GPIO2, HIGH) → Light ON
  │     │  │  │  ├─ ledState = true
  │     │  │  │  └─ ack_status = 0x01 (success)
  │     │  │  ├─ Else if command == 0x00:
  │     │  │  │  ├─ digitalWrite(GPIO2, LOW) → Light OFF
  │     │  │  │  ├─ ledState = false
  │     │  │  │  └─ ack_status = 0x01 (success)
  │     │  │  └─ Call transmitAck(ack_status)
  │     │  │
  │     │  └─ If device ID does NOT match:
  │     │     └─ Ignore (not for this light node)
  │     │
  │     └─ If "radio_err" or no packet:
  │        └─ Continue listening (non-blocking)
  │
  └─ Repeat loop
```

---

## Methods Reference

### **Phase 1: Initialization**

#### 1. `connectToLoRa()`
- **Purpose**: Initialize RN2483 LoRa P2P module
- **Called**: Once during setup
- **Parameters**: None
- **Flow**:
  1. Reset RN2483 (GPIO23 pulse)
  2. Begin UART1 @ 57600
  3. Flush serial
  4. Get version
  5. Send "mac pause" (P2P mode)
  6. Configure 13 radio parameters (frequency, SF, BW, CRC, etc.)
  7. Enter continuous RX: "radio rx 0"
- **Returns**: bool (true if all commands succeed)

---

### **Phase 2: Main Loop**

#### 2. `checkForCommand()` 🎯 **CRITICAL**
- **Purpose**: Check if light command received and process it
- **Called**: Every loop iteration
- **Parameters**: None
- **Flow**:
  1. Call readLoRaLine(100ms timeout)
  2. If "radio_rx <hex>" received:
     - Extract 10-char hex string (5 bytes)
     - hexToBytes() → convert to 5-byte buffer
     - xorWithKey() → decrypt
     - Parse: deviceId (bytes 0-3), command (byte 4)
     - If deviceId == 0x00000005:
       - If command == 0x01:
         - digitalWrite(GPIO2, HIGH)
         - setLed(true)
         - transmitAck(0x01)
       - Else if command == 0x00:
         - digitalWrite(GPIO2, LOW)
         - setLed(false)
         - transmitAck(0x01)
     - Else: ignore (not for this node)
  3. Continue listening
- **Returns**: void

#### 3. `transmitAck(uint8_t status)` 📤
- **Purpose**: Send ACK packet back to gateway
- **Called**: After processing valid command
- **Parameters**: status (0x01 = success, 0x00 = failure)
- **Flow**:
  1. Build 5-byte ACK buffer:
     - Bytes 0-3: 0x00000005 (device ID, big-endian)
     - Byte 4: status (0x01 or 0x00)
  2. xorWithKey() → encrypt
  3. bytesToHex() → convert to 10-char hex
  4. sendLoRaCommand("radio tx <hex>")
  5. Wait for "radio_tx_ok"
  6. Log to Serial
- **Returns**: bool (true = ACK sent, false = TX failed)

---

### **Phase 3: LoRa Communication**

#### 4. `readLoRaLine(int timeoutMs)`
- **Purpose**: Read one line from RN2483 with timeout
- **Called**: By checkForCommand()
- **Parameters**: timeoutMs (timeout in milliseconds)
- **Flow**:
  1. Initialize timer, empty String
  2. While elapsed < timeout:
     - If available: read char
     - Skip '\r'
     - If '\n': return trimmed line
     - Else: append char
  3. Return accumulated (may be empty)
- **Returns**: String

#### 5. `sendLoRaCommand(const String& cmd, int timeoutMs)`
- **Purpose**: Send AT command to RN2483
- **Called**: During initialization and ACK TX
- **Parameters**:
  - cmd: AT command (e.g., "radio rx 0")
  - timeoutMs: response timeout
- **Flow**:
  1. Print to loraSerial with "\r\n"
  2. readLoRaLine(timeoutMs)
  3. Return response
- **Returns**: String

#### 6. `flushLoRaInput()`
- **Purpose**: Clear serial buffer
- **Called**: Before RX mode
- **Parameters**: None
- **Flow**:
  1. While available: read and discard
- **Returns**: void

---

### **Phase 4: Utility Functions**

#### 7. `setLed(bool on)`
- **Purpose**: Turn physical LED on/off (GPIO2)
- **Called**: After command execution
- **Parameters**: on (true = ON, false = OFF)
- **Flow**:
  1. digitalWrite(GPIO2, on ? HIGH : LOW)
  2. Log to Serial
- **Returns**: void

#### 8. `bytesToHex(const uint8_t* data, size_t len)`
- **Purpose**: Convert byte array to hex string
- **Called**: By transmitAck()
- **Parameters**:
  - data: input bytes
  - len: length
- **Flow**:
  1. For each byte: extract high/low nibble, convert to hex char
  2. Return uppercase hex string
- **Returns**: String

#### 9. `hexToBytes(const String& hexStr, uint8_t* out, size_t outLen)`
- **Purpose**: Convert hex string to bytes
- **Called**: By checkForCommand()
- **Parameters**:
  - hexStr: hex input
  - out: output buffer
  - outLen: output length
- **Flow**:
  1. Validate length (must be 2*outLen)
  2. For each pair: convert hex chars to nibbles, combine
  3. Return true if success
- **Returns**: bool

#### 10. `xorWithKey(uint8_t* data, size_t len)`
- **Purpose**: XOR encrypt/decrypt with SHARED_KEY
- **Called**: By checkForCommand() and transmitAck()
- **Parameters**:
  - data: buffer to encrypt/decrypt (modified in place)
  - len: length
- **Flow**:
  1. For each byte i: data[i] ^= SHARED_KEY[i % 4]
- **Returns**: void

---

## Communication Protocol

### UART1: RN2483 LoRa P2P (57600 baud)

```
Request: <command>\r\n
Response: <response>\r\n

Examples:
  IN:  "radio rx 0\r\n"
  OUT: "ok\r\n"
  
  OUT: "radio_rx 0000000501XXXXXX\r\n"  [command received]
  
  IN:  "radio tx 0000000501YYYYYY\r\n"
  OUT: "ok\r\n"
  OUT: "radio_tx_ok\r\n"  [ACK sent]
```

---

## Setup & Deployment

### Configuration

**File**: [light-node/light-node.ino](light-node.ino) (lines 30-40)

```cpp
const uint32_t lightNodeId = 5UL;  // 0x00000005 (or 6, 7, etc. for multiple nodes)
const uint8_t cmdOff = 0x00;
const uint8_t cmdOn  = 0x01;
const uint8_t ackFail = 0x00;
const uint8_t ackOk   = 0x01;

#define LORA_FREQ        868100000UL  // Must match gateway
#define LORA_SF          "sf12"       // Must match gateway
#define LORA_PWR         14           // Must match gateway

#define LED_PIN 2  // GPIO2 - relay/light control
```

**Important**: Light node device ID (0x00000005) must be recognized by gateway (or set `sendLightControlCmd()` to use this ID).

### Flashing Procedure

```
Board: ESP32 Dev Module
Upload Speed: 921600
Port: COM[your device]
File: light-node/light-node.ino
```

### Verification

**Serial Monitor Output** (115200 baud):
```
Light node starting...
RN2483 connected.
Radio configured (SF12, 868.1 MHz, P2P mode).
Entering RX mode...
Light node ready.

[Waiting for commands...]

[If command received]
radio_rx 0000000501XXXXXX
Decrypted: [0x00, 0x00, 0x00, 0x05, 0x01]
Device ID: 0x00000005 ✓
Command: ON (0x01)
Light ON
Sending ACK...
radio_tx 0000000501YYYYYY
radio_tx_ok
ACK sent.

[Back to listening...]
```

---

## Multi-Light Setup

To control multiple lights:

1. **Create variants with different device IDs**:
   ```cpp
   // light-node-1.ino
   const uint32_t lightNodeId = 5UL;  // 0x00000005
   
   // light-node-2.ino
   const uint32_t lightNodeId = 6UL;  // 0x00000006
   
   // light-node-3.ino
   const uint32_t lightNodeId = 7UL;  // 0x00000007
   ```

2. **Update gateway to send individual commands**:
   ```cpp
   // In gateway.cpp mqttCallback()
   if (msg.equalsIgnoreCase("LIGHT1 ON"))
     sendLightControlCmd(true, 0x00000005);
   else if (msg.equalsIgnoreCase("LIGHT2 ON"))
     sendLightControlCmd(true, 0x00000006);
   ```

3. **Flash each light node with its variant**
4. **Each light node responds only to its device ID**

---

## Summary

The **Light Node** provides real-time grow light control by:

| Aspect | Detail |
|--------|--------|
| **Device ID** | 0x00000005 (or configurable variants) |
| **Power** | External (USB/AC), always-on |
| **Control Mode** | LoRa P2P, continuous RX listening |
| **Response Time** | <1 second from command TX to light activation |
| **Output** | GPIO2 (can drive relay or direct LED) |
| **Feedback** | ACK packet to confirm state change |
| **Range** | ~1-2 km P2P with SF12 |

**Key Functions**:
1. Initialize RN2483 in RX mode
2. Wait for valid 5-byte light command
3. Decrypt and validate device ID
4. Activate/deactivate GPIO2 (light ON/OFF)
5. Send ACK packet to confirm

See [Main Gateway Documentation](../gateway/README.md) for system-wide architecture.
