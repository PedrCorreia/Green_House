# LoRaWAN Greenhouse Environmental Monitoring System

**Group 34346** — DTU IoT Project

Distributed IoT system for real-time greenhouse environmental monitoring using LoRa/LoRaWAN wireless communication over ESP32-based sensor nodes.

**Languages & Technologies:** C/C++ | Python | JavaScript | KiCad | Arduino

---

## System Overview

Multi-node greenhouse monitoring network with dual LoRa communication paths.

### Node Architecture
```
5 ESP32-based Nodes:
  • Sensor Node (LoRa P2P telemetry)
  • LoRaWAN Sensor Node (Cibicom OTAA uplink)
  • Gateway (WiFi/MQTT bridge + LoRa P2P RX)
  • Gateway Slave (LoRaWAN serial bridge)
  • Light Control Node (LoRa P2P receiver)
```

### Monitored Parameters
| Parameter | Sensor | Range |
|-----------|--------|-------|
| Temperature & Humidity | DHT22 | -40…+85°C, 0-100% |
| Soil Moisture | Capacitive | 0-100% |
| Light Intensity | KY-018 | 0-4095 lux |
| Battery Voltage | ADC | 0-5000 mV |


### Technology Stack
| Component | Technology |
|-----------|------------|
| Microcontroller | ESP32 DevKit V1 |
| Radio Module | RN2483 LoRa |
| Power Management | AP2112K-3.3 LDO, AO3401A MOSFET |
| PCB Design | KiCad 10.0.0 |
| Communications | LoRa P2P, LoRaWAN (OTAA), WiFi, MQTT |


---

## Firmware Targets

| Target | Language | File | Purpose |
|--------|----------|------|---------|
| Sensor Node | C++ | sensor-node.ino | DHT22/soil/light telemetry over LoRa P2P |
| LoRaWAN Sensor | C++ | lorawan-sensor-node.cpp | OTAA uplink to Cibicom/Loriot |
| Gateway | C++ | gateway.cpp | WiFi/MQTT bridge, LoRa P2P receiver |
| Gateway Slave | C++ | gateway-slave.cpp | LoRaWAN serial bridge |
| Light Node | C++ | light-node.ino | LoRa P2P command receiver for LED control |
| Backend Router | Python | loriot_router.py | 

---

## Communication Architecture

### Primary Path (LoRa P2P)
```
Sensor Node ──LoRa P2P──→ Gateway ──WiFi──→ MQTT Broker ──JSON──→ Dashboard
```

### Secondary Path (LoRaWAN)
```
LoRaWAN Sensor ──LoRaWAN──→ Cibicom/Loriot ──Webhook──→ Router ──MQTT──→ Dashboard
```
---

## Documentation

Individual README files in each directory contain hardware pinouts, configuration details, testing procedures, and troubleshooting.

**References:**
- [RN2483 Datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/40001811G.pdf)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)
- [Cibicom LoRaWAN](https://iotnet.teracom.dk)
- [MQTT Protocol](https://mqtt.org)
- [Communication Flowchart](COMMUNICATION_FLOWCHART.md)
- [Hardware Schematics](hardware/)
- [Test Results](test/README.md)

---

**Last Updated:** May 2026 | **Status:** Ready for Submission | **Contact:** Group 34346
