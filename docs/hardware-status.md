# Hardware status

This repository contains a hardware prototype. This document separates source-code behavior from results on the actual assembled board.

## Current version

`GreenhouseSimple` uses NodeMCU ESP8266, GC9A01A, PCF8575, ADS1115 and HTU21. The pump is disabled in software, and the old DRV8833 inputs are held LOW. Fans use a separate active-HIGH relay on P15. `GreenhouseController` uses the previous wiring and requires separate adaptation.

## What was checked

- The October 20, 2026 history records successful compilation, upload, detection of three I²C devices, navigation, and calibration persistence after a restart: [VALIDATION.txt (Russian)](../GreenhouseSimple/VALIDATION.txt).
- During diagnostics on November 4, the warm-light command remained ON for 90 seconds on the light page and 120 seconds on the sensor page. The user confirmed that shutoffs recorded in other short checks were accidental button presses.
- A five-minute check was not completed: the controller first entered the UART bootloader, then the PCF was unavailable after application startup (`io=0`). These results do not establish that the original light-shutoff issue has been fixed.

## Open issues

**Lighting.** The current source has no shutoff timer. Commands are cleared on PCF communication loss or restart; selecting one lighting mode disables the other. The reported unexpected OFF state has not been fully explained. The interface displays a command; there is no sensor confirming actual light output.

**Fan relay.** Digital diagnostics confirmed P15 writes and reads. The user reported no physical relay response. An ON icon and a successful build do not verify relay actuation or fan rotation.

**Display and I²C.** Interaction between DC/GPIO0 and SDA/GPIO4 was found on the assembled device. The firmware releases DC between display operations and performs I²C transactions separately. This is a workaround for this assembly, not a substitute for checking its wiring. Diagnostics also showed `boot mode:(1,6)`; the GPIO0 and auto-reset circuits need inspection before drawing conclusions about the cause.

**Calibration.** The source retains the 9038/7382 reference points measured on October 20 for a specific sensor and soil. During recalibration on November 4, the dry EEPROM reference was updated to 13224; the wet reading failed the separation check, so its previous value was retained. The user stopped calibration. A board's EEPROM state is not transferred through Git: each new device needs complete calibration of both points.

## Before connecting loads

Check the [pinout](../GreenhouseSimple/PINOUT.md), supply voltages and module active levels. On I²C loss, the program can clear its own commands but cannot guarantee physical shutdown of a previously energized relay. Testing with loads requires a way to disconnect their power.

Publishing the repository and updating its documentation do not include a new firmware upload or another hardware test.
