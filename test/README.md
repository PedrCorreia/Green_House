# Testing & Validation Conclusions

This directory previously contained isolated testing sketches used during the development phase. The codebase has matured past the need for keeping these test sketches in the repository. Below is a summary of the conclusions drawn from our testing phases, covering both hardware validation tests and pre-integration verification.

## 1. Hardware Validation
**DHT22 (Temperature/Humidity)**
- **Conclusion:** A 4.7kΩ pull-up resistor on the data line is mandatory. The sensor requires a 2-second stabilization delay before the first read.
- **Result:** Successfully provided reliable readings after startup delay logic was implemented.

**OLED Display (I2C)**
- **Conclusion:** The `128x32 SSD1306` configuration caused visual artifacts. We validated the screen as a `128x64 SH1106` driven OLED screen.
- **Result:** Fully functional after migrating to the `Adafruit_SH110X` library with proper screen dimensions.

## 2. Communication and LoRa P2P
**Continuous Transmission vs. Deep Sleep**
- **Conclusion:** Point-to-point transmission loops smoothly when nodes are constantly awake. However, using deep sleep creates synchronization drift. Wait and listen windows between nodes can easily become out of phase due to RTC timer independence.
- **Result:** The system requires precise clock drift calibration or extended receiver awake windows at the gateway. The initial transmission after sleep correctly aligns, but repeated wake-cycles can cause packet loss. 

## 3. Gateway Integration
**Reception & Status Output**
- **Conclusion:** The gateway successfully loops the RN2483 radio module, ingests payloads (XOR-obfuscated), parses out sensor metrics, and updates the OLED display per second. 
- **Result:** The radio link functioned correctly during field/range tests out to **257.1 m**, but packet timing became unstable and marginal past this point.
- **Limitations:** The RSSI value heavily dictates signal quality visualization, but isn't always returned by the LoRa module. We fall back to standard states when signal metrics drop out natively.

## 4. Light Control Node (MQTT -> LoRa CMD)
**Bidirectional Node Communication**
- **Conclusion:** A downlink solution was successfully established where MQTT "ON" / "OFF" commands triggered the Gateway to send a 5-byte target payload to the specific Light Node device ID. 
- **Result:** The node correctly ignores commands not directed to it, acknowledges valid commands, translates it directly to physical pin toggles, and relays success back up to the MQTT `status` topic.
- **Limitations:** ACK indicates receipt. It cannot prove the light bulb is physically active.

All tests outlined in the initial validation and `pre-integration` phases have been fully incorporated into the production firmware located in the `src/` directory.
