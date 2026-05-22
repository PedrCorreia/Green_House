[Repository Root](../../README.md) > PCB Overview

# PCB Documentation — IoT Greenhouse Monitor
**Project:** IoT LoRaWAN Greenhouse Monitoring System  
**Group:** Group 5  
**Last Modified:** 22/05/2026  
**KiCad Version:** 10.0.0  
**Board Revision:** v1.1  

---

## Overview

Custom 2-layer PCB designed to host the ESP32 DevKit V1, RN2483 LoRa breakout module, and all sensor connectors for a greenhouse environmental monitoring node. Communicates via LoRaWAN to a central gateway.

---

## Board Specifications

| Parameter | Value |
|---|---|
| Layers | 2 |
| Top layer | Signal traces + components |
| Bottom layer | GND copper pour |
| Board dimensions |  |
| Minimum trace width | 0.25mm (signal), 1.0mm (power) |
| Minimum clearance | 0.2mm |
| Minimum drill size | 0.8mm |
| Surface finish | HASL |
| PCB thickness | 1.6mm |
| Copper weight | 1oz |

---

## Layer Stack

| Layer | Function |
|---|---|
| F.Cu (Top) | All components, signal + power traces |
| B.Cu (Bottom) | Solid GND copper pour |
| F.Silkscreen | Component labels, orientation markers |
| Edge.Cuts | Board outline |

---

## Power Architecture

```
LiPo Battery (3.7-4.2V)
    ↓
J1 (2-pin connector)
    ↓
Q1 AO3401A (P-ch MOSFET reverse polarity protection)
    ↓
U1 AP2112K-3.3 (LDO 3.3V 600mA)
    ↓
+3V3 rail → ESP32, LoRa, OLED, all sensors
```

---

## Net Summary

| Net | Source | Destinations |
|---|---|---|
| +3V3 | U1 VOUT | ESP32 3V3, J2 pin5, U3 VCC, J_SOIL, J_WATER, U2 VDD |
| GND | Common | All components |
| TH | ESP32 GPIO4 | U2 DHT22 DATA |
| LORA_RX | ESP32 GPIO16 | J2 pin1 |
| LORA_TX | ESP32 GPIO17 | J2 pin2 |
| LORA_RST | ESP32 GPIO14 | J2 pin4 |
| I2C_SDA | ESP32 GPIO21 | U3 OLED SDA |
| I2C_SCL | ESP32 GPIO22 | U3 OLED SCL |
| SOIL_MOIST | ESP32 GPIO32 | J_SOIL pin1 |
| WATER_LEAK | ESP32 GPIO33 | J_WATER pin3 |
| LIGHT | ESP32 GPIO35 | R3 LDR midpoint (no connector) |
| LED_CTRL | ESP32 GPIO25 | LED connector |
| BATT_SENSE | ESP32 GPIO34 | R5/R6 voltage divider midpoint |

---

## Component Index

| Ref | Value | Footprint | Description |
|---|---|---|---|
| U1 | AP2112K-3.3 | SOT-23-5 HandSoldering | LDO 3.3V regulator |
| U2 | DHT22 | TO-92-3 | Temp/humidity sensor |
| U3 | SH1106 OLED | ER_OLEDOM0.96 | 0.96" I2C OLED display |
| U6 | ESP32 DevKit V1 | DOIT_ESP32_DEVKIT_V1 | Main MCU |
| Q1 | AO3401A | SOT-23-5 HandSoldering | P-ch MOSFET |
| R1 | 10kΩ | R_Axial THT | Gate pull-down resistor |
| R3 | GL5528 LDR | LDR_Disc_D5.0mm THT | Embedded light sensor |
| R4 | 10kΩ | R_Axial THT | LDR pull-down resistor |
| R5 | 100kΩ | R_Axial THT | Battery divider top |
| R6 | 100kΩ | R_Axial THT | Battery divider bottom |
| C1 | 10µF | CP_Radial THT | LDO output bulk cap |
| C2 | 10µF | CP_Radial THT | LDO input ceramic cap |
| C3 | 100µF | CP_Radial THT | LDO input bulk cap |
| C4 | 100µF | CP_Radial THT | LDO output bulk cap |
| C5 | 100nF | C_Disc THT | DHT22 decoupling cap |
| C6 | 100nF | C_Disc THT | ESP32 decoupling cap |
| C7 | 100nF | C_Disc THT | LDR ADC noise filter |
| J1 | Conn_01x02 | PinHeader 2.54mm | Battery input |
| J2 | Conn_01x07_Socket | PinSocket 2.54mm | LoRa serial/power |
| J3 | Conn_01x14_Socket | PinSocket 2.54mm | LoRa GPIO (mechanical) |
| J_SOIL | Conn_01x03_Socket | JST PH 2.0mm | Soil moisture sensor |
| J_WATER | Conn_01x03_Socket | PinSocket 2.54mm | Water leakage sensor |
| J_LIGHT | Conn_01x03_Socket | PinSocket 2.54mm | KY-018 photoresistor |

---

## Subfolders

| Folder | Contents |
|---|---|
| [MCU/](MCU/) | ESP32 DevKit V1 pinout, GPIO assignment, decoupling |
| [COMS/](COMS/) | RN2483 LoRa module, OLED display |
| [SENSORS/](SENSORS/) | DHT22, soil moisture, water leakage, light sensor |
| [POWER/](POWER/) | LDO, MOSFET protection, capacitor strategy |

---

## Manufacturer Notes

- Target manufacturer: JLCPCB or Aisler (Europe)
- Gerber export: `File → Fabrication Outputs → Gerbers`
- Drill file: `File → Fabrication Outputs → Drill Files`
- BOM: `File → Fabrication Outputs → BOM`

---

## Change Log

| Date | Version | Author | Change |
|---|---|---|---|
| 22/05/2026 | v1.1 | Group 5 | Added battery voltage monitor (R5/R6), embedded LDR (R3/R4/C7), removed J_LIGHT, reassigned GPIO34→BATT_SENSE, GPIO35→LIGHT, GPIO33→WATER_LEAK |
| 03/04/2026 | v0.1 | Group 5 | Initial schematic — power management |
| 06/04/2026 | v0.2 | Group 5 | Added LoRa, OLED, sensor schematics |
| 07/04/2026 | v0.3 | Group 5 | PCB layout started |
| 09/04/2026 | v1.0 | Group 5 | Layout in progress, routing ongoing |


---

## Related Documents

- [Communications](COMS/COMS.md)
- [Microcontroller Unit](MCU/MCU.md)
- [Power Management](POWER/POWER.md)
- [Sensors](SENSORS/SENSORS.md)
