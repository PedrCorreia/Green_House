[Repository Root](../../../README.md) > [PCB Overview](../../PCB_OVERVIEW.md) > Power Management

# Power Management — PCB Documentation
**Last Modified:** 03/04/2026  
**Subsystem:** Power Management  
**Schematic Sheet:** Power Management  

---

## Schematic

![Power Management Schematic](../../images/pcb/POWER/Power%20Management.png)

---

## Design Overview

LiPo battery input with P-channel MOSFET reverse polarity protection feeding an LDO regulator producing a clean 3.3V rail for all system components.

```
LiPo (3.7–4.2V) → J1 → Q1 (protection) → C3/C2 → U1 (LDO) → C1/C4 → +3V3
                              ↓
                         R1 (gate pull-down to GND)
```

---

## Components

### J1 — Battery Connector
| Parameter | Value |
|---|---|
| Part | Conn_01x02_Socket |
| Footprint | PinHeader_2.54mm Vertical THT |
| Pin 1 | GND |
| Pin 2 | 4.7VIN (battery +) |
| Placement | Board edge for cable exit |

---

### Q1 — Reverse Polarity Protection
| Parameter | Value |
|---|---|
| Part | AO3401A |
| Type | P-channel MOSFET |
| Package | SOT-23-5 HandSoldering |
| Vds | -30V |
| Id | -4A continuous |
| Rds(on) | 45mΩ typical |
| Last Modified | 03/04/2026 |

**Datasheet:** (https://octopart.com/datasheet/alpha-omega-semiconductor/AO3401A)

**Pin connections:**
| Pin | Net | Notes |
|---|---|---|
| Source (pin 2) | 4.7VIN | Faces battery+ always |
| Drain (pin 3) | Net-(Q1-D) → LDO VIN | Faces LDO |
| Gate (pin 6) | Net-(Q1-G) → R1 → GND | Pulled low via 10kΩ |

**Operating principle:**  
Gate pulled to GND via R1 → Vgs = 0 - Vbat = negative → MOSFET ON → current flows S→D to LDO. Reversed polarity → Vgs = 0 → MOSFET OFF → circuit protected. Body diode also blocks reverse current.

---

### R1 — Gate Resistor
| Parameter | Value |
|---|---|
| Part | Resistor 10kΩ |
| Package | R_Axial_DIN0207 THT |
| Footprint | Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal |
| Power rating | 0.25W |
| Tolerance | 5% |
| Last Modified | 03/04/2026 |

**Purpose:** ESD protection on MOSFET gate. Limits gate current during transients.

---

### U1 — LDO Voltage Regulator
| Parameter | Value |
|---|---|
| Part | AP2112K-3.3 |
| Package | SOT-23-5 HandSoldering |
| Input voltage | 2.5V – 6V |
| Output voltage | 3.3V fixed |
| Max output current | 600mA |
| Dropout voltage | ~300mV @ 600mA |
| Quiescent current | 55µA typical |
| EN pin | Tied to VIN (always on) |
| Last Modified | 03/04/2026 |

**Datasheet:** (https://www.bing.com/search?qs=LT&pq=AP2112K-3.3+datashee&sk=CSYN1&sc=2-20&q=ap2112k-3.3+datasheet&cvid=18bfe72665df491fb46917099448cfc6&gs_lcrp=EgRlZGdlKgYIABAAGEAyBggAEAAYQNIBCDQ0NTJqMGo0qAIAsAIA&FORM=ANAB01&PC=U531)

**Current budget:**
| Consumer | Typical | Peak |
|---|---|---|
| ESP32 active | 80–160mA | 240mA |
| RN2483 TX | 120mA | 120mA |
| Sensors total | 20–50mA | 50mA |
| OLED | 10mA | 10mA |
| **Total worst case** | **~350mA** | **~420mA** |

⚠️ 600mA limit gives comfortable headroom. Monitor thermal dissipation — at 4.2V input, LDO drops ~0.9V @ 350mA = ~315mW. SOT-23-5 thermal limit ~300–400mW — ensure good GND copper pour for heat dissipation.

**Pin connections:**
| Pin | Net |
|---|---|
| VIN (1) | Net-(Q1-D) |
| GND (2) | GND |
| EN (3) | VIN (always on) |
| NC (4) | NC |
| VOUT (5) | +3V3 |

---

### Capacitors

| Ref | Value | Type | Location | Purpose |
|---|---|---|---|---|
| C3 | 100µF | Electrolytic polarized THT | LDO input | Bulk charge storage |
| C2 | 10µF | Ceramic unpolarized THT | LDO input | High-freq noise filter |
| C1 | 10µF | Ceramic unpolarized THT | LDO output | High-freq noise filter |
| C4 | 100µF | Electrolytic polarized THT | LDO output | Bulk + transient support |

**Footprints:**
```
Electrolytic (100µF): Capacitor_THT:CP_Radial_D5.0mm_P2.50mm
Ceramic (10µF):       Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm
```

⚠️ Verify polarity before soldering — + terminal faces +3V3/VIN, - terminal faces GND.

---

## PCB Layout Notes

- All power traces: **1.0mm width minimum**
- Signal traces: **0.25mm minimum**
- Decoupling caps (C1, C2) placed as close as possible to LDO pins
- J1 placed at board edge — cable exits toward battery
- Q1 placed directly after J1 in signal flow
- Bottom layer: solid GND copper pour

---

## Change Log

| Date | Change |
|---|---|
| 03/04/2026 | Initial design — LDO + caps |
| 03/04/2026 | Added AO3401A reverse polarity protection + R1 |
| 09/04/2026 | PCB layout placed and routing started |


---

## Related Documents

- [PCB Overview](../../PCB_OVERVIEW.md)
- [Communications](../COMS/COMS.md)
- [Microcontroller Unit](../MCU/MCU.md)
- [Sensors](../SENSORS/SENSORS.md)
