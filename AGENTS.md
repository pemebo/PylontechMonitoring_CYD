# AGENTS.md — Pylontech Monitoring ESP32

## Project Overview
ESP32 Arduino firmware that monitors Pylontech US2000B/C, US3000C, US5000 batteries via RS-232 console port and exposes data over WiFi (web UI + MQTT). **No Makefile, no PlatformIO, no CI, no tests** — pure Arduino IDE project.

## Code Conventions
- **All code comments must be in English** — never Spanish. Spanish is only allowed in user-facing i18n content (`dict.es` in `data/static_i18n.js`, the ES half of `tr("en","es")` pairs). Applies to firmware, web handlers, JS, HTML and CSS.

## Build & Flash
- **Arduino IDE only** — open `PylontechMonitoring.ino`
- **Vendored libraries** in `libraries/`: ArduinoJson, PubSubClient, Time, NTPClient, ESP8266TimerInterrupt, Misc, TFT_eSPI
  - These must also be placed in the Arduino IDE's `libraries/` folder for compilation
- **Partition scheme**: `partitions/pylontech_ota_spiffs.csv` (dual OTA + 512KB SPIFFS)
- **Flash over WiFi** (OTA) is supported — no USB cable needed after initial flash

### Arduino CLI compile + OTA (verified 2026-09-23)
- **compile** (writes to the CLI cache, NOT to the sketch's `build/` folder):
  ```
  arduino-cli compile --fqbn esp32:esp32:esp32 \
    --libraries PylontechMonitoring/libraries \
    --build-property build.partitions=PylontechMonitoring/partitions/pylontech_ota_spiffs.csv \
    PylontechMonitoring
  ```
  The fresh app image lands in `~/.cache/arduino/sketches/<hash>/PylontechMonitoring.ino.bin` (`<hash>` = CID of the sketch sources; `ls -dt ~/.cache/arduino/sketches/*/` shows the newest). Verify it's the right build by **mtime**, not by content: `stat -c '%y %n' ~/.cache/arduino/sketches/*/PylontechMonitoring.ino.bin` must be newer than the moment you started the compile. Do NOT use `strings <bin> | grep -c <token>` to prove a build unless `<token>` is a real string literal in the firmware: short tokens that exist only as numeric constants or in comments are folded/absent, so the check returns 0 for a perfectly correct build. To prove a specific code change is inside the image, grep for a string literal you just added or changed, e.g. `strings <bin> | grep -c 'Module data, Cells data'`.
- **OTA upload** (Basic Auth `admin:<configPassword>`, filename must contain `Pylontech` and end in `.bin`):
  ```
  curl -u admin:<pass> -F "firmware=@<fresh .ino.bin>;filename=PylontechMonitoring.ino.bin" \
    http://<esp-ip>/api/ota
  ```
  Device reboots after `OK`. Verify battery recovery after ~40 s with `curl -u admin:<pass> http://<esp-ip>/api/dashboard` (`battery.overview.soc != null`).

## Architecture (2 FreeRTOS Tasks)
- **Task 1 (Core 1, priority 2)** — Real-time UART pipeline: `Scheduler → UART → Parser → frameQueue`
  - Battery pins (CYD): **RX=22, TX=27** on the **CN1** connector (defined via `BAT_RX_PIN`/`BAT_TX_PIN` in `py_uart.h`) — **confirmed working with the real battery 2026-09-20** (loopback-validated first); previously TX=22/RX=35 on P3
  - Baud rate auto-detection supported
- **Task 2 (Core 0, priority 1)** — Everything else: Scheduler loop, MQTT publish, Webserver, WiFi manager, Display
- **`loop()` is empty** — all logic runs in tasks. Do not add code to `loop()`.

## Entry Point & Flow
- `PylontechMonitoring.ino:setup()` — boot sequence: config load → queues → UART → scheduler → WiFi → MQTT → SPIFFS → webserver → create tasks
- `PylontechMonitoring.ino:240` — initial scheduler command: `"pwr"`
- Scheduler cycles through PWR/BAT/STAT commands at configurable intervals (defaults: 60s / 5min / 30min)

## Configuration (NVS via Preferences)
- All config stored in ESP32 NVS, **not in files**:
  - `config` namespace: WiFi, MQTT, NTP, device settings
  - `battery_pwr` / `battery_bat` / `battery_stat` namespaces: intervals, enabled flags, field configs (chunked JSON)
- Field configs use pipe-delimited packed format: `name|display|factor|unit|mqtt|send`
- **`config.load()`** (called in setup) reads all namespaces and applies factory defaults if empty
- No static config file — everything is web UI or code

## Web UI & OTA
- **Initial setup**: ESP creates AP `pylontech-XXXX` → browse `192.168.4.1/filemanager` → upload all files from `data/` to SPIFFS
- After WiFi connection (30s), AP shuts off → browse ESP's IP
- **`data/`** — HTML/CSS/JS pages served from SPIFFS (index.html, layout.html, pages_*.html, static_*.js, static_style.css, timezone.json)
- **`web/`** — C++ header files with route handlers and API endpoints for the webserver
- OTA firmware update available via web UI

### i18n — MANDATORY for every new/changed field or button
- **Rule**: any new field, label, button, dropdown option, placeholder, table header, tooltip, alert/confirm or message added or modified in the web UI **must ship its text in BOTH English and Spanish**.
- **Where**: add the key to `data/static_i18n.js` under **both** `dict.en` and `dict.es` (identical key sets — never one without the other). Key convention: namespaced lowercase dot-separated (`nav.*`, `layout.*`, `conn.*`, `dash.*`, `mod.*`, `health.*`, `screen.*`, `log.*`, `bv.*`, `cell.*`, `stat.*`, `sd.*`, `fm.*`, `upd.*`, `exp.*`, `con.*`, `fd.*`, `status.*`, `event.*`).
- **HTML**: static text via `data-i18n` (or `data-i18n-html` / `data-i18n-ph` / `data-i18n-title` / `data-i18n-aria`). Wrap `<label>` text in a `<span data-i18n="key">` so the input is preserved.
- **JS**: dynamic text via `t("key")`.
- **Server response strings stay English**: never translate the C++ strings; map known responses to a `*.msg_*` key client-side for display only, keeping the raw string for any logic/comparison.
- **Battery status / health events from the backend stay English protocol tokens**: translate client-side with `statusLabel()` / `eventLabel()` (handle exact and composite comma-joined strings).
- **Log messages stay English** (not translated).
- **Verify before deploy**: EN and ES must have identical key sets and no referenced key may be missing; then md5-verify the uploaded files against the device (see SPIFFS upload pitfall below).
- Reference implementation: `data/pages_connection.html` + `data/static_connection.js`.

## Battery Protocol
- **PWR** — Power data: voltage, current, temperature, SOC per module
- **BAT** — Cell data: individual cell voltages (Vlow, Vhigh, Vdiff)
- **STAT** — Statistics: module health info
- Battery modes: **STACK** (daisy-chain) or **HUB** (with hub/Masterhub addressing)
- Cell diff warnings: 100mV (warn), 150mV (error) — configurable
- **Stack SOC policy (`computeStackSoc` in `py_parser_pwr.cpp`)**: average of per-module SOCs while the stack status is `Charging` (`all modules "Charge"`), otherwise the **minimum** module SOC (safe/conservative).

## MQTT
- Home Assistant autodiscovery supported
- Topics: `{prefix}/{topicStack}/{pwr|bat|stat}` (prefix default: "Pylontech")
- Cell topics: `{prefix}/{cellPrefix}{cellIndex}/...`
- Modes: `active` (publish on change) or `passive`
- MQTT messages flow via FreeRTOS queue from Task 1 → Task 2

## Boot Button (GPIO)
- **1x short** → enable WiFi AP mode (temporary, 5 min; AP+STA, home WiFi stays connected)
- **≥5s hold** → reset WiFi credentials (clears the `wifi` namespace, drops to AP only)
- **5x short** → reset WiFi credentials (same action, kept as an alternative trigger)
- **15s long** → factory reset (clears NVS + reboot)

## Key Globals (config.h)
- `config` — AppConfig singleton
- `pwrA/pwrB`, `batA/batB`, `statA/statB` — double-buffered parsed data
- `pwrUseA/batUseA/statUseA` — buffer swap flags
- `g_batteryMode` — BatteryMode enum (UNKNOWN/STACK/HUB)
- `health` — HealthStatus struct for module health tracking
- `mqttQueue`, `frameQueue`, `rtWakeQueue` — FreeRTOS queues

## Common Pitfalls
- **Do not add to `loop()`** — it's intentionally empty; all logic is in FreeRTOS tasks
- **`long` is 32-bit on ESP32 (ILP32)** — any `mV × mA` product overflows `INT32_MAX` above ~42.3 A at 50.7 V and wraps to a negative number. Compute power in `long long`: `(long)((long long)voltage_mV * current_mA / 1000000LL)`. This bit the stack `powerW` on the dashboard (`web/api_combined.h`) and the TFT (`py_display.cpp`): charging at +42665 mA at 50740 mV showed **−2130 W instead of +2164 W** (fixed in `382442a`). MQTT is immune (it publishes V/A as float, no wattage).
- **Battery UART pins on the CYD (confirmed 2026-09-20)** — **RX=22, TX=27** on the **CN1** connector (`BAT_RX_PIN`/`BAT_TX_PIN` in `py_uart.h`; passed to `py_uart.begin()` in `PylontechMonitoring.ino`), **working with the real battery** (loopback-validated first). Replaces the old P3 wiring **TX=22, RX=35** (which was confirmed 2026-09-19 and still works — **GPIO35 is input-only**, no internal pullup, cannot TX). GPIO27 is a better RX than 35 (bidirectional, has internal pulls). Do NOT regress to other CYD pins:
  - **GPIO1/GPIO3** (silkscreen "TX"/"RX" on P1) = UART0 shared with the onboard CH340 — the CP2102 drives GPIO3, so wiring a converter there fights the USB chip and kills USB logging.
  - **GPIO16/GPIO17** = RGB LED (green/blue), not on any header. A pin-scan will falsely "detect" 16↔17: it is an on-board asymmetric coupling through the LED circuit (17 drives 16, but 16 does NOT drive 17) — NOT a wire; UART loopback there fails. Ignore that pair.
  - **GPIO21** = TFT backlight (LEDC PWM) — never use it to power anything (screen OFF → 0V → converter dies; dim → PWM ripple).
  - Power the RS-232 level converter (**SP3232EEN/MAX3232**, the US3000C console is RS-232 levels, NOT 3V3) from the **3V3/CN1** rail, not from a GPIO. Keep the 5 × 0.1µF charge-pump caps. Battery console wiring (CN1): ESP GPIO22 ← `R1OUT`, ESP GPIO27 → `T1IN`, RJ45 **pin 3 (white-green) → R1IN**, **pin 6 (green) ← T1OUT**, **pin 8 (brown) = GND**.
  - Verifying pins on hardware: a bridge between two P3 header pins turned out to be **GPIO22 ↔ GPIO35**; the CN1 signal pins are **GPIO22 ↔ GPIO27** (manufacturer documents CN1 as the temp/humidity sensor port). A wrong jumper onto the GND pin of CN1 drove **GPIO27 to LOW permanently** — catch it as "GPIO27 stuck LOW even when driven HIGH". Use a diagnostic pin-scan firmware before assuming physical pin = GPIO number; then confirm with a UART loopback echoing a known pattern both directions. A bare loopback echo is not a valid Pylontech frame, so `invalid frame` warnings during a loopback test are expected (watchdog `commReady=false` resets on the first valid battery frame). `tests/pin_scan/pin_scan.ino` is the working diagnostic (full-chip scan + pull/stuck probe + UART echo, preserves the custom partition table via `tests/pin_scan/partitions.csv`).
- **SPIFFS must be populated** before web UI works — upload `data/` via filemanager AP mode
- **SPIFFS upload pitfall (2026-10-04)** — `POST /fm/upload` (Basic Auth, field `file`) can silently fail: an upload launched while the device is still rebooting after an OTA returns empty and the file is lost, so the device keeps serving a STALE web asset (e.g. an old `static_i18n.js` missing keys → the whole UI renders untranslated). **After every upload, verify `md5sum` local == device.** Note `pages_*.html` are password-protected, so the verification `curl` needs `-u admin:<pass>` or it reports a false DIFF (401/redirect body).
- **Library vendoring** — `libraries/` folder is local; Arduino IDE needs them in its own libraries path too
- **NVS chunking** — field config JSON is split into 1500-byte chunks (max 50) due to NVS string limits
- **Display module** (`py_display.cpp/h`) — ILI9341 240x320 via TFT_eSPI, pins: **CS=15, DC=2, RST=-1, MOSI=13, SCLK=14, MISO=12, BL=21**. `display.begin()` called in `PylontechMonitoring.ino:273` before FreeRTOS tasks. **Working config confirmed 2026-09-12**
- **Touch XPT2046 (CYD ESP32-2432S028R)** — the touch controller has its OWN SPI bus, NOT the display SPI: **CLK=25, MOSI=32, MISO=39, CS=33, IRQ=36**. Read via bit-bang in `py_display.cpp` (manufacturer style: 8-bit cmd → 6µs settle → clear BUSY → 16-bit read >>4; CMD 0x90/0xD0/0xB0/0xC0). Calibration stored in NVS namespace `touch` (`ver` key invalidates stale state). **Pitfall**: GPIO36 is an input-only RTC pin — `pinMode(36, INPUT)` MUST run on every boot (before the "already calibrated" early return) or `digitalRead(36)` returns a spurious LOW that latches the touch as permanently pressed. Working config confirmed 2026-09-16
- **Touch reliability — root cause & fix (confirmed 2026-09-17)**: erratic touch ("must tap several times before the screen changes") was caused by (1) dispatching the tap with a single raw sample taken on the exact IRQ falling edge — the ADC is still settling there, so the coordinate lands off-target; and (2) using `IRQ || Z>350` for press detection — spurious Z values created phantom edges that consumed real taps. **Fix implemented in `py_display.cpp`:** `xptReadMedian()` reads **16 raw samples**, drops the 2 lowest and 2 highest, averages the 12 central (+ `readTouchMedian()` wrapper); `pollTouch()` detects press **by IRQ only**, does a 5ms settle, then dispatches the median-filtered coordinate. The calibration wizard's `rawValid` also uses `readTouchMedian`. Single-press navigation now works reliably on small buttons (e.g. 68px BACK). Grading/from earlier builds: strict two-sample `diff<=200` reject dropped quick taps; plain 4-sample average still captured transition samples. Do NOT reintroduce Z-fallback as a press source.

## Display Configuration (ILI9341 240x320)
- **Driver**: TFT_eSPI with ILI9341_2_DRIVER
- **Pins**: CS=15, DC=2, RST=-1, MOSI=13, SCLK=14, MISO=12, BL=21
- **File**: `PylontechMonitoring/User_Setup.h`
- **Config file**: `PylontechMonitoring/py_display.cpp:h`
- **Working config confirmed**: 2026-09-12
- **Common issue**: Display illuminates but shows no text if pins are incorrect
- **Solution**: Verify pins match User_Setup.h exactly; check TFT_eSPI library is in Arduino libraries path
- **Battery icon (main screen)**: drawn with TFT primitives (`drawBatteryIcon` in `py_display.cpp`) replicating the web dashboard — level-scaled fill, slate outline, amber bolt while charging, "!" for disconnected/alarm. **Web palette** (`COLOR_OK` green, `BAT_OUTLINE` 0x7C93, `BAT_AMBER` 0xF460; red for alarm or SOC<10). Replaces the old smooth MDI font (`mdi_battery_160.h` no longer included — freed ~457 KB flash, build 71%): the MDI charging glyph overflowed the left panel. Fixed fill: 100% fills the whole case (web `static_dashboard.js` uses `fillH = 18.0*s/100; fillY = 22-fillH`)

## Screen Settings (Page "Screen" + config)
- Web page `pages_screen.html` / `static_screen.js` (menu Config → Screen), API `web/screen_api.h` (`GET/POST /api/screen`)
- Persisted in NVS namespace `screen` via `ScreenConfig` (`config.screen`) — keys `enabled`, `touch`, `bright`, `sched`, `off_h`, `off_m`, `on_h`, `on_m`, `refresh_sec`, `dark`, `autoOff`, `autoOffSec`
  - Legacy `timed_off`/`timed_on` keys are **migrated** into the single `sched` flag on load and removed on save (there is no per-gate toggle anymore)
- **Boot rule**: the screen **respects the saved master switch** `enabled` at boot (`setScreenOn(config.screen.enabled)` in `PylontechMonitoring.ino`). With `enabled=false` it boots OFF and stays fully inert; with `enabled=true` it boots ON. `applyScreenConfig()` sets the runtime `screenEnabled` master flag but never turns the backlight off/on itself
- **Screen off guard**: `enabled=false` is rejected (HTTP 400) unless WiFi is connected (`WL_CONNECTED`) or in AP/APSTA mode — checked in `screen_api.h:screenWifiOk()`
- **Web master switch**: `scr_enabled` reflects the **stored** `cfg.enabled`, not the runtime backlight — an auto power-off (`screenOn=false`) must not uncheck it. When unchecked, `applyEnabledState()` greys out (`scr-disabled` class) and disables every other control on the page except the master checkbox and the Display box Save button (so the disabled state can still be saved)
- **Brightness**: manual `0-100%`, applied to backlight via LEDC PWM on `TFT_BL` (pin 21). API is core-version-aware: core 2.x uses `ledcSetup/ledcAttachPin(ledcWrite(ch))`, core 3.x uses `ledcAttach/ledcWrite(pin)` (`backlightInit`/`backlightWrite` in `py_display.cpp`)
  - **Single write path**: `PyDisplay::writeBacklight()` computes duty for the whole on/off + polarity (`BACKLIGHT_INVERT` const in `py_display.cpp`). **Polarity calibrated on device (2026-09-18): NORMAL (active-high), `BACKLIGHT_INVERT=false`** — 0% = minimum light, 100% = maximum. When `screenOn`, the duty has a floor of 1 (0% still glows; never fully dark while "on"); screen OFF writes 0 (or 255 if inverted)
  - **Apply order in POST**: values saved → `setBrightness` (live, respects a runtime-off screen) → `applyScreenConfig` → `setScreenOn` **only if `enabled` actually changed** (a plain save must not wake a screen a power-timer turned off)
  - Brightness slider applies **live (debounced 350 ms)** in the browser via `onBrightnessInput()`
- **Touch**: gated first by the master switch — `pollTouch()` returns immediately when `screenEnabled` is false (touch is completely dead, not even wake). Otherwise it bails out **only while the screen is ON and `touchEnabled` is false**. While the screen is OFF the touch is ALWAYS live (regardless of the Touch setting) so a tap can wake it
- **Auto power-off**: "Auto power-off" checkbox + seconds field (max 4 digits, default `30`) in the **Power Control** card (web card renamed from "Timed power control"). Persisted as `autoOff`/`autoOffSec`. When ON, `loop()` (1 s block) turns the screen off after `autoOffSec` without any touch (`lastTouchTime` reset on any press and on any `setScreenOn(true)`). A tap while OFF only wakes the screen (`setScreenOn(true)`, no `handleTap` — the tap is not dispatched because the user cannot know what the pressed while dark) and the current view (MAIN/MODULES/CELLS) is kept, since only the backlight toggles. Auto-off only flips `screenOn`; it never changes `screenEnabled`. **Mutually exclusive with Schedule**: enabling one clears the other (JS `setScheduleUI`/`autoOffChange`; server `web/screen_api.h`, on conflict Schedule wins)
- **Timed power**: **"Schedule" checkbox** + two `<input type="time">` fields (Screen off / Screen on) in the Power Control card (form state only; saved via Save). When ON, BOTH `offHour:offMin` and `onHour:onMin` fire together: off → screen off, on → screen on. `loop()` checks the local minute every second (minute-rollover detection); only runs when the system time is valid (`config.isSystemTimeValid()`); no NTP → no timing. **Mutually exclusive with Auto power-off** (enabling one clears the other)
- **Min refresh period** (key `refresh_sec`, default `10`, 0–255 s water UI as seconds): `refreshMs` in `PyDisplay` (applied via `applyScreenConfig()`) throttles **data-driven** redraws of ALL screens (MAIN `needsRedraw`, MODULES `modulesDirty`, CELLS `cellsDirty`) in `loop()` through a shared `lastDataRedraw` timestamp. `refreshSec=0` = redraw on every data update (legacy). User interactions (tap/back/scroll in `handleTap()`) still redraw immediately. `updateModules()` has change detection, so identical frames do not queue repaints
- **No light sensor**: auto-brightness option intentionally omitted (no LDR/BH1750 in hardware)
- **Theme (dark/light)**: "Dark mode" checkbox in the web Display card, persisted as `dark` in the `screen` NVS namespace (`ScreenConfig.darkMode`, default `true`). `py_display.cpp` uses file-static `themeBg()`/`themeFg()` helpers (driven by `g_darkMode`, applied via `applyTheme(cfg.darkMode)`). Dark = black background + white primary text; Light inverts both. Accent colors stay identical in both themes (green/yellow/red/blue cell highlights, amber bolt, slate battery outline, navy/skyblue buttons, grey placeholders, darkgrey labels/borders, green WiFi). The touch-calibration wizard stays dark always. `begin()` calls `applyTheme(config.screen.darkMode)` before drawing (no boot flash); `applyScreenConfig()` repaints the visible screen immediately when the theme changes

## File Structure
```
PylontechMonitoring.ino    — entry point, task definitions
config.cpp/h               — AppConfig, NVS persistence, data structures
py_uart.cpp/h              — UART communication (RX=35, TX=22)
py_scheduler.cpp/h         — command queue, periodic polling
py_parser_pwr.cpp/h        — PWR frame parser
py_parser_bat.cpp/h        — BAT cell data parser
py_parser_stat.cpp/h       — STAT frame parser
py_mqtt.cpp/h              — MQTT + Home Assistant discovery
py_wifimanager.cpp/h       — WiFi AP/STA, scan, connection
wp_webserver.cpp/h         — Web server (ESP32WebServer)
wp_routes.cpp/h            — Route registration
py_systemmanager.cpp/h     — Boot button, factory reset, events
py_display.cpp/h           — ILI9341 240x320 display via TFT_eSPI
py_log.cpp/h               — Logging (serial + web)
data/                      — SPIFFS web content (HTML/CSS/JS)
web/                       — Webserver route handlers (.h files)
libraries/                 — Vendored Arduino libraries
partitions/                — OTA + SPIFFS partition table
```

## Cell "today" stats (V min/max/delta today — shared by TFT CELLS screen + web Health table)
- Per-module day stats live in the **global** `cellDayStats` array (`config.h`, `MAX_MODULES+1` entries, defined in `config.cpp`); fed by `updateCellStats()` (`py_cellstats.h/.cpp`), which updated from **PWR** (`Vlow/Vhigh`, no cell/time) when `config.battery.enableBat=false` or from **BAT** frames (per-cell, with cell number + capture time) when `enableBat=true`. All writes/reads stay in Task 2 (no cross-task sharing); `resetAllCell`. Reset on calendar-day change via `AppConfig::getCurrentDayKey()` (DST-aware, `-1` while time invalid → never wipe against 1970); also force-reset once per minute in `PyDisplay::loop()` so all modules clear at 00:00 even without a fresh frame.
- Rendered on the TFT CELLS screen (`drawCellScreen`) reading the same store: BAT mode shows `3.345V (c7)` / `3.345V (c7 14:32)`; PWR mode shows values without cell/time. Web `GET /api/health` emits `todayMin/todayMinCell/todayMinTime`, `todayMax/…`, `deltaToday/deltaTodayTime` per module (delta is two-cell → time only).
- **`enableBat=false` → cell/time never shown (2026-09-24)**: today's cell number + capture time are only meaningful when derived from BAT frames, and a manual `bat N` (module tap / console) could stamp the store with a cell + time even while BAT polling was disabled — `(c? 13:47)` in the web Health table. Two guards now enforce the rule: (1) `PyDisplay::updateCells()` only feeds the shared day-store from BAT frames while `config.battery.enableBat` (the CELLS screen still shows the instantaneous BAT payload; PWR frames still feed the store without cell/time via `PylontechMonitoring.ino`); (2) `web/wp_health_api.h` emits `today*Cell`/`today*Time`/`deltaTodayTime` only while `enableBat` — so even stale BAT-sourced times of the same day stay hidden. The TFT CELLS "… today" lines apply the same gate (`showTodayCells`). `(c?)` in `static_health.js` renders `cell 0` with a non-empty time, which the gating makes unreachable.
- Cell payloads are stored **per module** in `PyDisplay::cellMap`; `cellsModuleIndex` is the view selector and is set **only** on user tap. `updateCells()` never rewrites it (each module shows only its own cells; modules without BAT data render `--` placeholders everywhere).
- Requires BAT polling for every present module (scheduler sends `bat N` per present module every `intervalBat` **only if `config.battery.enableBat`**). `enableBat=false` defaults meant only the initial `bat 1` ever ran → only module 1 had cell/day data. A one-time migration (`config.load()` -> NVS marker `mig_cellstats`) forces `enableBat=true` once; afterwards the web checkbox is authoritative again.

