# Assembly pinout

Translated from the wiring notes updated on October 20, 2026. Fans use a separate relay with IN connected to PCF8575 P15. The user specified HIGH for ON and LOW for OFF; physical actuation remains unconfirmed. Other wiring is retained from the original notes. See [hardware status](../docs/hardware-status.md) for observed limitations.

## 1. Power

All grounds share a common connection: the 12 V supply negative, all three DC-DC outputs and NodeMCU GND. Set each DC-DC output to 5.0 V before connecting loads.

| Source | Destination |
|---|---|
| 12 V input | IN+ / IN− of all three DC-DC converters |
| DC-DC 1, 5 V | NodeMCU VIN, SD module VCC |
| DC-DC 2, 5 V | DRV8833 VM |
| DC-DC 3, 5 V | Relay VCC and JD-VCC with jumper fitted, K2 COM for the strip |
| NodeMCU 3V3 | PCF8575, ADS1115, HTU21, four touch modules, display, soil sensor, water-level sensor, driver STBY as labeled in the original notes, K1 COM for the 3.3 V lamp |

## 2. NodeMCU pins

| Pin | GPIO | Purpose |
|---|---|---|
| D1 | 5 | I²C SCL |
| D2 | 4 | I²C SDA |
| D5 | 14 | SPI SCK, display and SD |
| D6 | 12 | SPI MISO, SD only |
| D7 | 13 | SPI MOSI, display and SD |
| D8 | 15 | Display CS |
| D3 | 0 | Display DC |
| D4 | 2 | Display RST |
| D0 | 16 | SD CS |
| RX / TX | 3 / 1 | Serial debugging |
| A0 | ADC | Unused |
| SD2 / SD3 | 9 / 10 | Do not use |

## 3. I²C bus

SDA D2 and SCL D1 connect to PCF8575, ADS1115 and HTU21.

| Device | Address |
|---|---|
| PCF8575 | `0x20` |
| ADS1115 | `0x48` |
| HTU21 | `0x40` |

## 4. PCF8575

| Pin | Connection |
|---|---|
| VCC / GND | 3V3 / common GND |
| SDA / SCL | D2 / D1 |
| A0, A1, A2 | GND, selecting `0x20` |
| INT | Unconnected |
| P0 | Relay IN1, UV lamp |
| P1 | Relay IN2, warm-light strip |
| P2 / P3 | DRV8833 AIN1 / AIN2 |
| P4 / P5 | DRV8833 BIN1 / BIN2, previous fan channel; always LOW in GreenhouseSimple |
| P6 | Unused; driver STBY is tied to 3V3 in the original notes |
| P7 | Spare |
| P10 / P11 / P12 / P13 | Touch 1 / 2 / 3 / 4 |
| P14 | Spare, for example a float switch |
| P15 | Separate fan relay IN, HIGH=ON, LOW=OFF |
| P16 / P17 | Spare |

P15 is **bit 13** of the 16-bit word, mask `0x2000`. It is not NodeMCU GPIO15.

## 5. ADS1115

| Pin | Connection |
|---|---|
| VDD / GND | 3V3 / common GND |
| SDA / SCL | D2 / D1 |
| ADDR | GND |
| A0 | Soil sensor AOUT |
| A1 | Water-level sensor S |
| A2, A3, ALRT | Unconnected |

## 6. Sensors

| Module | Connections |
|---|---|
| HTU21 / GY-21 | VIN 3V3, GND common, SDA D2, SCL D1 |
| Soil Moisture V2.0 | VCC 3V3, GND common, AOUT to ADS1115 A0 |
| Water-level sensor | + to 3V3, − to GND, S to ADS1115 A1 |
| Four touch modules | VCC 3V3, GND common, I/O to P10-P13 respectively |

The water-level sensor belongs to the original full wiring; the current minimal firmware does not use it.

## 7. HL-52S lighting relays

The JD-VCC/VCC jumper is fitted.

| Terminal | Connection |
|---|---|
| VCC and JD-VCC | DC-DC 3, 5 V |
| GND | Common GND |
| IN1 / IN2 | PCF8575 P0 / P1, active LOW |
| K1 COM | 3V3 |
| K1 NO | Positive terminal of the 3.3 V lamp |
| K2 COM | DC-DC 3, 5 V |
| K2 NO | Positive terminal of the 5 V LED strip |
| Lamp and strip negative terminals | GND |

### Separate fan relay

IN connects to PCF8575 P15, HIGH=ON and LOW=OFF. It switches the fans from the fan page. The original update did not establish this relay's supply or the fans' power wiring, so COM/NO/NC assignments are not specified here.

## 8. Previous DRV8833 circuit

| Pin | Connection in original wiring notes |
|---|---|
| VM | DC-DC 2, 5 V |
| GND | Common GND |
| STBY | 3V3, permanently enabled as described in the notes |
| AIN1 / AIN2 | P2 / P3 |
| BIN1 / BIN2 | P4 / P5 |
| AO1 / AO2 | Pump + / − |
| BO1 / BO2 | No longer used for fan control; fans now use the separate P15 relay |
| NC | Unconnected |

The original notes describe weak PCF8575 HIGH outputs, with driver input voltage dropping to about 2.6 V. They recommend 4.7 kΩ pull-ups from AIN1, AIN2, BIN1 and BIN2 to 3V3, plus a 100-470 µF capacitor between VM and GND. These are historical wiring recommendations, not a new validation of this module. GreenhouseSimple holds P2-P5 LOW and does not use the pump.

## 9. GC9A01 SPI display

| Display pin | NodeMCU connection |
|---|---|
| VCC / GND | 3V3 / GND |
| SCL, meaning SCK | D5 |
| SDA, meaning MOSI | D7 |
| DC | D3 |
| CS | D8 |
| RST | D4 |
| BLK, if present | 3V3 |

## 10. MicroSD SPI module

| Module pin | Connection |
|---|---|
| VCC | DC-DC 1, 5 V only if the module has a regulator; otherwise 3V3 |
| GND | Common GND |
| SCK / MISO / MOSI | D5 / D6 / D7 |
| CS | D0 |

## 11. Assembly checks and original notes

- Confirm common GND across all modules and converters.
- Do not confuse the display's SPI SCL/SDA labels (D5/D7) with I²C (D1/D2).
- ESP8266 boot requires D8 LOW and D3/D4 HIGH. The original notes suggest temporarily disconnecting display CS, DC or RST if the board cannot start with the display attached; inspect the actual boot wiring before doing so.
- ADS1115 A0/A1 input voltage must not exceed its 3.3 V VDD.
- The lamp, display backlight and sensors share the 3V3 supply. The original notes recommend a separate 3.3 V regulator if the lamp draws more than about 200 mA.
- The original SD setup specifies FAT32, up to 32 GB. Earlier card detection failed; check module power and MISO/MOSI wiring. SD is unused by GreenhouseSimple.
- The original upload procedure recommends disconnecting 12 V to avoid powering NodeMCU from two sources during upload.
- All load commands OFF in GreenhouseSimple corresponds to `0xDFC3`; fans ON with other channels OFF corresponds to `0xFFC3`. Touch P10-P13 remain HIGH in the PCF output latch so they can be read.
- GreenhouseController retains the previous wiring. The P15 migration applies to GreenhouseSimple.

This translation preserves the documented wiring; it does not certify the assembly. Consult the [hardware status](../docs/hardware-status.md) before powering loads.
