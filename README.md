# IoT Greenhouse Monitor

The IoT Greenhouse Monitor is a professional embedded systems solution designed to accurately process and transmit environmental data. This repository contains the hardware documentation, source code, and user interface components for the IoT LoRaWAN Greenhouse Monitoring System. The node uses an ESP32 microcontroller, RN2483 LoRaWAN module, and various environmental sensors to monitor greenhouse conditions and transmit data telemetry to a central gateway.


## Technology Stack

| Component | Technology |
|---|---|
| Microcontroller | ESP32 DevKit V1 |
| Communications | RN2483 LoRaWAN Module |
| Sensors | DHT22, Capacitive Soil Moisture, Water Leakage, KY-018 Photoresistor |
| Power Management| AP2112K-3.3 LDO, AO3401A MOSFET |
| PCB Design | KiCad 10.0.0 |
| Firmware | C/C++ |
| User Interface | Web/Mobile |

## Repository Structure

```text
iot-greenhouse/
├── README.md
├── .gitignore
├── docs/
│   └── pcb/
│       ├── PCB_OVERVIEW.md
│       ├── POWER/
│       │   └── POWER.md
│       ├── MCU/
│       │   └── MCU.md
│       ├── COMS/
│       │   └── COMS.md
│       └── SENSORS/
│           └── SENSORS.md
├── pre-integration/
│   ├── Coms/
│   ├── Others/
│   └── Sensors/
├── src/
│   └── README.md
└── ui/
    └── README.md
```

## Navigation

- [PCB Overview](docs/pcb/PCB_OVERVIEW.md)
- [Power Management](docs/pcb/POWER/POWER.md)
- [Microcontroller Unit](docs/pcb/MCU/MCU.md)
- [Communications](docs/pcb/COMS/COMS.md)
- [Sensors](docs/pcb/SENSORS/SENSORS.md)
