# Pylontech Battery Monitoring via WiFi (ESP32)

Forked from [irekzielinski/Pylontech-Battery-Monitoring](https://github.com/irekzielinski/Pylontech-Battery-Monitoring)
(based on the ESP8266/D1 mini version by [hidaba](https://github.com/hidaba) and [HeldvomForst] (https://github.com/HeldvomForst)), rewritten for the ESP32.

This project lets you monitor Pylontech **US2000B, US2000C, US3000C and US5000** batteries through their
RS-232 console port and exposes the data over WiFi: a full web interface, MQTT (with Home Assistant
autodiscovery), an on-device TFT touchscreen, SD card logging/export and OTA firmware updates.

**I ACCEPT NO RESPONSIBILITY FOR ANY DAMAGE CAUSED, PROCEED AT YOUR OWN RISK**

## Features

* Low cost (around 20–30 $ in total).
* Web interface (single-page app served from SPIFFS) with pages for:
  * Dashboard — stack/module overview: SOC, voltage, current, power, temperature, Max Delta, SD free space
  * Modules — per-module data (PWR/BAT/STAT)
  * Cell data — individual cell voltages plus today's min/max/delta per module
  * Health — module health and statistics, including today's cell stats
  * Statistics — counters and health history
  * Battery console — send raw console commands and read the response over WiFi (no PC needed)
  * Frame dump — inspect raw protocol frames
  * Data export / SD card — export PWR/BAT/History data to the SD card
  * File manager — upload/delete files on SPIFFS (and SD card)
  * Screen — TFT settings (brightness, touch, theme, schedule, auto power-off, refresh)
  * Connection — WiFi scan/connect (DHCP or static IP), NTP/timezone, MQTT
  * Update — OTA firmware upload + storage information
  * Log — device log viewer
* MQTT support:
  * Publishes SOC, voltage, current, temperature, state, cell data, statistics, …
  * Home Assistant autodiscovery (per-module and per-cell topics)
  * Active (publish on change) or passive mode
* TFT display (ILI9341 240x320) with touch (XPT2046) — MAIN, MODULES, CELLS and STATUS screens,
  dark/light theme, brightness, schedule/auto power-off and a calibration wizard
* SD card logging and CSV export of PWR, BAT and History data
* Flash new firmware over WiFi (OTA) — no need to disconnect from the battery
* English/Spanish interface (web UI + display), selected at runtime
* Easy to modify code using Arduino IDE
* Scheduled automatic reboot and double-buffered, 2-core FreeRTOS architecture

## Hardware needed

* **ESP32 board** — the tested hardware is a "Cheap Yellow Display" (**ESP32-2432S028R**, ESP32 + 2.8"
  ILI9341 TFT + XPT2046 touch). A plain ESP32 dev board also works (the display is then unused).
* **MAX3232 / SP3232 RS-232 level converter** — the Pylontech console is RS-232 levels, **not 3V3**.
  Power it from the board's **3V3 rail** (not from a GPIO) and keep the 5 × 0.1 µF charge-pump caps.
* **US2000B:** cable with an RJ11 connector (some RJ11 cables have only two wires — make sure you get
  one with all four wires present).
* **US2000C / US3000C / US5000:** cable with an RJ45 connector (see below).
* Optional: microSD card for logging/export, and a USB power supply.

![Schematics](Schemetics.png)

## Wiring

Battery console on the CYD board (**CN1** connector, defined by `BAT_RX_PIN` / `BAT_TX_PIN` in
`PylontechMonitoring/py_uart.h`):

* ESP **GPIO22** ← `R1OUT` (battery data)
* ESP **GPIO27** → `T1IN`
* Level converter powered from **3V3**, GND common with the battery

US2000C/US3000C/US5000 use an RJ45 plug (same schematic, different plug):

* RJ45 pin 3 (white-green) = `R1IN`
* RJ45 pin 6 (green)       = `T1OUT`
* RJ45 pin 8 (brown)       = `GND`

![image](https://user-images.githubusercontent.com/19826327/146428324-29e3f9bf-6cc3-415c-9d60-fa5ee3d65613.png)

> Do **not** use GPIO1/GPIO3 (shared with the onboard USB-UART), GPIO16/GPIO17 (RGB LED) or
> GPIO21 (TFT backlight).

## Getting started

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) plus the ESP32 board support.
2. Copy the contents of the [libraries](libraries) subdirectory into your
   [Arduino IDE libraries folder](https://forum.arduino.cc/index.php?topic=88380.0).
3. Open [PylontechMonitoring/PylontechMonitoring.ino](PylontechMonitoring/PylontechMonitoring.ino).
4. Select the partition scheme **`partitions/pylontech_ota_spiffs.csv`** (dual OTA + 512 KB SPIFFS).
5. Connect the ESP32 to the MAX3232, the MAX3232 to the RJ11/RJ45 console plug (all three lines),
   and the plug into the battery's serial/console port (the master battery if you have several).
6. Flash and power the ESP32 via USB.
7. Connect to the WiFi AP `pylontech-xxxx`, browse `192.168.4.1/filemanager` and upload **all** files
   from the [`PylontechMonitoring/data`](PylontechMonitoring/data) folder to SPIFFS.
8. Browse `http://192.168.4.1` → *Connection* → scan for WiFi and connect to your network.
   The AP switches off ~30 s after connecting (after a refresh you can see the ESP's IP address).
9. Optional: configure MQTT on the *Connection* page. `MQTT` = topic prefix, `Send` = Home Assistant
   autodiscovery. Use the *Battery console* page to talk to the battery directly.

Configuration pages and values are password protected. Default values are:
user-> admin
pass-> 12345

**Careful with OTA:** only flash the `.bin` produced by a fresh build (see below). Never flash an old
artifact left over from a previous build — it silently downgrades the device.

### Build from the command line (arduino-cli)

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 \
  --libraries PylontechMonitoring/libraries \
  --build-property build.partitions=PylontechMonitoring/partitions/pylontech_ota_spiffs.csv \
  PylontechMonitoring
```

The image lands in `~/.cache/arduino/sketches/<hash>/PylontechMonitoring.ino.bin`. Upload it through
the web UI (*Update* page) or via `POST /api/ota`.

## Boot button

| Action | Effect |
| --- | --- |
| 1× short press | Enable a temporary WiFi AP (5 min, AP+STA — your home WiFi stays connected) |
| ≥5 s hold | Reset WiFi credentials (drops to AP only) |
| 5× short press | Reset WiFi credentials (same action) |
| 15 s hold | Factory reset (clears all NVS + reboot) |

## Configuration

Everything is stored in **ESP32 NVS** (no config files) and edited from the web UI:

* `config` namespace — WiFi, MQTT, NTP, device settings
* `battery_pwr` / `battery_bat` / `battery_stat` — polling intervals, enabled flags, field configs
* `screen` — display settings; `touch` — touch calibration

The scheduler polls the battery (`pwr` / `bat` / `stat`) at configurable intervals (defaults: 60 s /
5 min / 30 min). BAT and STAT parsers can be enabled/disabled individually.

## Architecture

Two FreeRTOS tasks, `loop()` is intentionally empty:

* **Core 1 (priority 2)** — real-time UART pipeline: `Scheduler → UART → Parser → frameQueue`
* **Core 0 (priority 1)** — everything else: scheduler loop, MQTT publish, web server, WiFi manager,
  display

```
PylontechMonitoring/
├── PylontechMonitoring.ino   entry point, boot sequence, task creation
├── config.cpp/h              AppConfig, NVS persistence, data structures
├── py_uart.cpp/h             UART (RX=22, TX=27), baud auto-detection
├── py_scheduler.cpp/h        command queue, periodic PWR/BAT/STAT polling
├── py_parser_pwr|bat|stat.*  protocol parsers (double-buffered)
├── py_mqtt*.cpp/h            MQTT + Home Assistant autodiscovery
├── py_display.cpp/h          ILI9341 TFT + XPT2046 touch
├── py_sdcard.cpp/h           SD logging, rotation, export
├── py_wifimanager.cpp/h      WiFi AP/STA, scan, connection
├── py_systemmanager.cpp/h    boot button, scheduled reboot, factory reset, events
├── py_log.cpp/h              logging (serial + web)
├── data/                     SPIFFS web content (HTML/CSS/JS)
├── web/                      C++ webserver route handlers and API endpoints
└── partitions/               pylontech_ota_spiffs.csv (dual OTA + SPIFFS)
libraries/                    vendored Arduino libraries
tests/                        standalone Arduino test sketches (SD card, pin scan)
```

## Battery protocol

* **PWR** — power data: voltage, current, temperature, SOC per module
* **BAT** — cell data: individual cell voltages (Vlow, Vhigh, Vdiff)
* **STAT** — statistics: module health info
* Battery modes: **STACK** (daisy-chain) or **HUB** (with hub/Masterhub addressing)
* Cell diff warnings: 100 mV (warn) / 150 mV (error) — configurable

## i18n

Every new/changed web UI string ships in **both** English and Spanish
(`data/static_i18n.js`, identical key sets). Server responses and log messages stay English and are
translated client-side only for display.

## Credits

* Original project: [irekzielinski/Pylontech-Battery-Monitoring](https://github.com/irekzielinski/Pylontech-Battery-Monitoring)
* ESP8266/D1 mini port: [hidaba](https://github.com/hidaba)
* ESP32 port: [HeldvomForst] (https://github.com/HeldvomForst)

