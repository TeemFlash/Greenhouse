# GreenhouseSimple

Round interface, ON/OFF controls, soil moisture and air temperature. This English guide translates the firmware notes updated on October 20, 2026; see the [hardware status](../docs/hardware-status.md) for later findings.

## Build and upload

Open `GreenhouseSimple/GreenhouseSimple.ino` in Arduino IDE. Keep all `.h` files beside the sketch.

- Board: **NodeMCU 1.0 (ESP-12E Module)**, ESP8266 core **3.1.2**.
- CPU: **80 MHz**. Flash: **4 MB**. Upload speed: **115200 baud**.
- Libraries: **Adafruit GC9A01A**, **Adafruit GFX Library**, **Adafruit BusIO**.
- EEPROM and Wire are included in the ESP8266 board package.

## Screens

The smooth eye animation is followed by the warm-light page. Display rotation is `3`. Pages wrap in this order:

**Warm light → UV → soil / temperature → fans → warm light**

Load pages show icons and `ON` / `OFF`, without headings, hints, a UV label inside the bulb, or RAW/mV values. If the PCF is unavailable, a dash replaces the load status. Yellow means warm light; violet means UV.

The sensor page shows soil moisture percentage and **air temperature** in °C from the HTU21. There is no separate soil-temperature sensor. Missing calibration or measurements show `-- %`; missing fresh temperature shows `-- °C`. Navigation arrows and page dots remain visible.

## Buttons

| Button / PCF pin | Action |
|---|---|
| 1 / P10 | Previous page |
| 2 / P11 | Next page |
| 3 / P12 | Toggle the selected load; on the sensor page, request fresh soil and temperature readings |
| 4 / P13 | Turn all load commands OFF from any page |

Debounce is 40 ms. Holding a button does not repeat its action. A button held at startup must first be released. STOP takes priority.

Warm light and UV are mutually exclusive; fans are independent. Switching lighting modes includes at least 80 ms with both relays OFF. All load commands start OFF after a restart. Icon colors and ON/OFF represent commands, not physical feedback from the lamps or fans.

## Soil moisture calibration

The original dry and wet references each averaged eight fresh ADS1115 measurements:

| Reference | Raw ADC reading |
|---|---|
| Dry soil, 0% | 9038 |
| Wet soil, 100% | 7382 |

Readings use a relative scale between these points, clamped to 0-100%. They are not laboratory measurements of volumetric water content. Measure new references for a different sensor or soil.

Reference samples are in `build/soil_dry_reference.json` and `build/soil_wet_reference.json`. `CalibrationDefaults.h` stores these defaults for the original device. Saved user calibration takes priority. Settings are versioned and protected by CRC32 in EEPROM; they are not written on every sensor update.

To recalibrate using Serial Monitor at 115200 baud:

1. Open the soil page and turn all load commands OFF with button 4.
2. Place the sensing section in dry soil, wait for stable contact and readings, then send `d`.
3. Repeat in wet soil and send `w`.

Each command captures the current fresh reading. Points less than 500 ADC counts apart are rejected without replacing the previous valid settings. During initial calibration, percentage becomes available after both points. During recalibration, a new point temporarily pairs with the previous opposite point, so **update both**. See the [later calibration findings](../docs/hardware-status.md#open-issues) before relying on an existing board's EEPROM values.

## Sensors

**ADS1115, `0x48`:** input A0, roughly one sample every 500 ms, ±4.096 V range. Response, read length, configuration and raw range 0-26400 are checked. Values older than 2 seconds are discarded. Calibration supports either signal direction as moisture increases.

**HTU21, `0x40`:** air temperature only, no-hold command `0xF3`, 1500 ms interval. CRC8, status and the -40 to 125 °C range are checked. A transient error retains the last verified reading only until it is 5 seconds old. Errors never refresh its timestamp; stale data becomes `-- °C`. Air humidity is not substituted for soil moisture.

## Wiring and I²C

See the [full pinout](PINOUT.md).

- SDA: D2/GPIO4. SCL: D1/GPIO5. PCF8575 address: `0x20`.
- P0/K1: UV, active LOW. P1/K2: warm light, active LOW.
- P15: fan relay, active HIGH; bit 13, mask `0x2000`.
- Old DRV8833 P2-P5 inputs remain LOW; the pump is unused.
- Touch inputs 1-4: P10-P13, active HIGH.
- Display: SCK D5, MOSI D7, CS D8, DC D3, RST D4.
- SD CS: D0, held HIGH; the card is unused.

On this assembly, DC/GPIO0 interacts with SDA. DC is OUTPUT only during drawing, then INPUT_PULLUP while CS is HIGH. PCF/ADS/HTU transactions run between display operations.

If PCF communication is lost, load commands are cleared and blocked. They remain OFF after recovery. Without I²C, physical shutdown of a previously energized relay cannot be guaranteed.

## Rendering

The renderer uses eight-row strips and a 3,840-byte buffer instead of a full RGB framebuffer. Ordinary sensor updates transfer the 240 × 80 numeric region. Page changes, data-availability changes and the end of the eye animation trigger full frames. There is no intermediate black frame.

## Serial Monitor commands

Set the baud rate to **115200**.

| Command | Action |
|---|---|
| `1` / `2` | Navigate |
| `3` | Current page action |
| `0` / `4` | Turn all load commands OFF |
| `?` | Status, raw/mV/percentage/temperature readings, data age, render time; I²C scan when load commands are OFF |
| `e` | Replay the eye animation |
| `x` | Diagnose normal/swapped SDA/SCL with load commands OFF, then restore the normal configuration |
| `d` / `w` | Capture dry/wet reference, only on SOIL with load commands OFF |

Page indices WARM=0 / UV=1 / SOIL=2 / FANS=3 differ from output channels UV=0 / WARM=1 / FANS=2; the firmware maps them explicitly.

## Checks and history

- [VALIDATION.txt](VALIDATION.txt): original Russian record of checks and uploads.
- `preview/`: previews from the same renderer; example readings are illustrative.
- `tests/`: load controls, buttons, ADC, calibration and HTU21 tests.
- `tools/check_minimal_ui.py`: checks navigation, sensors and percentage calculation on a board without load-ON commands; requires Python and pyserial.
- `tools/check_calibration_persistence.py`: restarts the device and verifies saved references; leaves the sensor page selected and load commands OFF.

The original notes mention a local backup named `GreenhouseSimple_before_percent_temperature_2026-10-20.zip`; this archive is not distributed in the repository. The earlier full automation firmware is in `../GreenhouseController`.

## Fan relay limitation

Digital P15 switching was previously confirmed, but the user reported that the relay itself did not actuate. The interface and sensor updates do not establish physical operation of the new relay. See [hardware status](../docs/hardware-status.md).
