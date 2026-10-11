# Pylontech Battery Monitoring via CYD (Cheap Yellow Display, ESP32)

Forked from [irekzielinski/Pylontech-Battery-Monitoring](https://github.com/irekzielinski/Pylontech-Battery-Monitoring)
(based on the ESP8266/D1 mini version by [hidaba](https://github.com/hidaba) and
[HeldvomForst](https://github.com/HeldvomForst)), rewritten for the CYD ESP32.

This project lets you monitor Pylontech **US2000B, US2000C, US3000C and US5000** batteries through their
RS-232 console port and exposes the data over WiFi: a full web interface, MQTT (with Home Assistant
autodiscovery), an on-device TFT touchscreen, SD card logging/export and OTA firmware updates.

**I ACCEPT NO RESPONSIBILITY FOR ANY DAMAGE CAUSED, PROCEED AT YOUR OWN RISK**

**----Español----**

Bifurcado (fork) de [irekzielinski/Pylontech-Battery-Monitoring](https://github.com/irekzielinski/Pylontech-Battery-Monitoring)
(basado en la versión para ESP8266/D1 mini de [hidaba](https://github.com/hidaba) y
[HeldvomForst](https://github.com/HeldvomForst)), reescrito para el CYD ESP32.

Este proyecto te permite monitorizar baterías Pylontech **US2000B, US2000C, US3000C y US5000** a través de
su puerto de consola RS-232 y expone los datos por WiFi: una interfaz web completa, MQTT (con
autodescubrimiento para Home Assistant), una pantalla táctil TFT en el propio dispositivo,
registro/exportación a tarjeta SD y actualizaciones de firmware por OTA.

**NO ACEPTO NINGUNA RESPONSABILIDAD POR CUALQUIER DAÑO CAUSADO. HAZLO BAJO TU PROPIO RIESGO**

## Purpose / Propósito

Some months ago, one of my US3000C modules began generating Overvoltage alarms to the inverter. I had no
way to see what was happening or which module was the problem. It was somewhat difficult to use the
BatteryView software to identify the module and the issue. So I loaded an ESP32 with the hidaba code and
used my Home Assistant instance to do precise monitoring of the Pylontech stack. This schematic helped a
lot, but I realized that:

- I had two spare CYD screens
- I didn't have much experience with Arduino
- I needed to gain experience in AI-assisted coding.

So adapting the code to the CYD would be a good exercise for me to understand the capabilities and
limitations of AI coding, and to get used to AI tools and methodologies. As a result, this project was
created using OpenCode + Gentle AI, DeepSeek V4.1 Flash and the Big Pickle models.

**----Español----**

Hace unos meses, uno de mis módulos US3000C empezó a generar alarmas de sobretensión (Overvoltage) hacia
el inversor. No tenía forma de ver qué estaba pasando ni cuál era el módulo problemático. Encontré que era algo
difícil usar el software BatteryView para identificar el módulo y el problema. Así que cargué un ESP32
con el código de hidaba y usé mi instancia de Home Assistant para hacer un monitoreo preciso de la batería
Pylontech. Este procedimiento ayudó mucho, pero me di cuenta de que:

- Tenía dos pantallas CYD de sobra
- No tenía nada de experiencia con Arduino
- Necesitaba ganar experiencia en programación asistida por IA.

Así que adaptar el código al CYD sería un buen ejercicio para entender las capacidades y limitaciones de
la programación con IA, así como para acostumbrarme a las herramientas y metodologías de IA. Como resultado,
este proyecto se creó usando OpenCode + Gentle AI y los modelos DeepSeek V4.1 Flash y Big Pickle.

## Features / Características

* Low cost (around 20–30 $ in total).
* Web interface (single-page app served from SPIFFS) with pages for:
  * **Dashboard** — stack/module overview: SOC, voltage, current, power, temperature, Max Delta, SD free space
  * **Modules** — per-module data (PWR/BAT/STAT)
  * **Health** — module health and statistics, including today's cell stats
  * **Battery data** — configure the PWR parser fields (display name, factor, unit), MQTT topics and the temperature unit
  * **Cells** — individual cell voltages plus today's min/max/delta per module
  * **Statistics** — STAT polling configuration (interval, MQTT topic) and battery statistics
  * **Settings** — WiFi scan/connect (DHCP or static IP), NTP/timezone, MQTT
  * **Screen** — TFT settings (brightness, touch, theme, schedule, auto power-off, refresh)
  * **Log** — device log viewer
  * **Data export** — export PWR/BAT/History data to the SD card
  * **SD card** — browse, upload and delete files on the SD card
  * **File manager** — upload/delete files on SPIFFS (and SD card)
  * **Console** — send raw console commands and read the response over WiFi (no PC needed)
  * **Frame Dump** — inspect raw protocol frames
  * **Update** — OTA firmware upload + storage information
* MQTT support:
  * Publishes SOC, voltage, current, temperature, state, cell data, statistics, …
  * Home Assistant autodiscovery (per-module and per-cell topics)
* TFT display (ILI9341 240x320) with touch (XPT2046) — MAIN, MODULES, CELLS and STATUS screens,
  dark/light theme, brightness, schedule/auto power-off
* SD card logging and CSV export of PWR, BAT and History data
* Flash new firmware over WiFi (OTA) — no need to disconnect from the battery
* English/Spanish interface (web UI + display), selected at runtime
* Easy to modify code using Arduino IDE
* Scheduled automatic reboot and double-buffered, 2-core FreeRTOS architecture

**----Español----**

* Bajo costo (unos 20–30 € en total).
* Interfaz web (servida desde SPIFFS) con páginas para:
  * **Dashboard** — resumen dela batería y el CYD: SOC, tensión, corriente, potencia, temperatura, Delta máximo, espacio libre en la SD
  * **Modules** — datos por módulo (PWR/BAT/STAT)
  * **Health** — salud y estadísticas de los módulos, incluidos los datos de hoy para las celdas
  * **Battery data** — configura MQTT para los campos del parser PWR (nombre mostrado, factor, unidad), los topics MQTT y la unidad de temperatura
  * **Cells** — configura MQTT para tensiones de cada celda más el mínimo/máximo/delta de hoy por módulo
  * **Statistics** — configura MQTT para el muestreo STAT (intervalo, topic MQTT) y estadísticas de la batería
  * **Settings** — escaneo/conexión WiFi (DHCP o IP estática), NTP/zona horaria, activación MQTT
  * **Screen** — ajustes de la pantalla TFT (brillo, táctil, tema, programación horaria, apagado automático, refresco)
  * **Log** — visor del registro del dispositivo
  * **Data export** — exporta datos PWR/BAT/History a la tarjeta SD
  * **SD card** — explora, sube y borra archivos en la tarjeta SD
  * **File manager** — sube/borra archivos en SPIFFS (y en la tarjeta SD)
  * **Console** — envía comandos crudos a la consola y lee la respuesta por WiFi (sin PC)
  * **Frame Dump** — inspecciona las tramas crudas del protocolo
  * **Update** — actualización de firmware por OTA + información de almacenamiento
* Soporte MQTT:
  * Publica SOC, tensión, corriente, temperatura, estado, datos de celdas, estadísticas, …
  * Autodescubrimiento para Home Assistant (topics por módulo y por celda)
* Información en pantalla TFT (ILI9341 240x320) con táctil (XPT2046) — pantallas MAIN, MODULES, CELLS y STATUS,
  tema claro/oscuro, brillo, programación horaria/apagado automático
* Registro en tarjeta SD y exportación CSV de datos PWR, BAT e History
* Actualiza el firmware por WiFi (OTA) — sin necesidad de desconectar de la batería
* Interfaz en inglés/español (web y pantalla), seleccionable en tiempo de ejecución
* Código fácil de modificar con el Arduino IDE
* Reinicio automático programado y arquitectura FreeRTOS de 2 núcleos con doble búfer

## Images / Imágenes

CYD screen / Pantalla del CYD

3D print / impresión 3D -> https://makerworld.com/es/models/2787810-cyd-desk-buddy-for-bambu-lab-home-assistant

<img width="320" height="180" alt="image" src="https://github.com/user-attachments/assets/de2246d2-b624-439d-a01d-d42c33a5e69c" /> <img width="320" height="180" alt="image" src="https://github.com/user-attachments/assets/d4eb628f-bd3e-4bb7-9e25-304aab9f2dc8" /> <img width="320" height="180" alt="image" src="https://github.com/user-attachments/assets/921083c8-5bef-46f9-b8a8-a9c7c0bf57c1" />

Main dashboard page / Página principal (Dashboard)
<img width="934" height="481" alt="image" src="https://github.com/user-attachments/assets/84e9134e-6345-477b-ae06-fbe87624d1e8" />

Modules details page / Página de detalles de módulos
<img width="1029" height="799" alt="image" src="https://github.com/user-attachments/assets/8ac34a2f-83a8-4624-9da5-6e910fdda3f8" />

Health details page / Página de detalles de Health
<img width="1037" height="534" alt="image" src="https://github.com/user-attachments/assets/393acb75-e5b5-4b34-8a6b-76abaf5b81e5" />

Screen configuration page / Página de configuración de la pantalla
<img width="1868" height="962" alt="image" src="https://github.com/user-attachments/assets/c030f42e-d3fc-4a88-96e7-111e2dc9b3b6" />

Data export page / Página de exportación de datos
<img width="2075" height="1068" alt="image" src="https://github.com/user-attachments/assets/36a02775-e705-431f-bb86-7e2e6917f254" />

Console page / Página de consola
<img width="1868" height="962" alt="image" src="https://github.com/user-attachments/assets/e957f79b-32ed-4833-b407-cfeabc74a801" />

## Hardware needed / Hardware necesario

* **ESP32 board** — the tested hardware is a "Cheap Yellow Display" (**ESP32-2432S028R**, ESP32 + 2.8"
  ILI9341 TFT + XPT2046 touch).
* **MAX3232 / SP3232 RS-232 level converter** — the Pylontech console is RS-232 levels, **not 3V3**.
  Power it from the board's **3V3 rail** (not from a GPIO) and keep the 5 × 0.1 µF charge-pump caps.
* **US2000B:** cable with an RJ11 connector (some RJ11 cables have only two wires — make sure you get
  one with all four wires present).
* **US2000C / US3000C / US5000:** cable with an RJ45 connector (see below).
* Optional: microSD card for logging/export, and a USB power supply.

**----Español----**

* **Placa ESP32** — el hardware utilizado es una "Cheap Yellow Display" (**ESP32-2432S028R**, ESP32 + TFT
  ILI9341 de 2.8" + táctil XPT2046). 
* **Conversor de niveles RS-232 MAX3232 / SP3232** — la consola Pylontech usa niveles RS-232, **no 3V3**.
  Aliméntalo desde el **riel de 3V3** de la placa (no desde un GPIO) y conserva los 5 capacitores de
  carga de 0.1 µF.
* **US2000B:** cable con conector RJ11 (algunos cables RJ11 traen solo dos hilos — asegúrate de conseguir
  uno con los cuatro hilos presentes).
* **US2000C / US3000C / US5000:** cable con conector RJ45 (ver abajo).
* Opcional: tarjeta microSD para registro/exportación, y una fuente de alimentación USB.

## Wiring / Cableado

Battery console on the CYD board (**CN1** connector, defined by `BAT_RX_PIN` / `BAT_TX_PIN` in
`PylontechMonitoring/py_uart.h`):

* ESP **GPIO22** ← `R1OUT`
* ESP **GPIO27** → `T1IN`
* Level converter powered from **3V3**, GND common with the battery

US2000C/US3000C/US5000 use an RJ45 plug (same schematic, different plug):

* RJ45 pin 3 (white-green) = `R1IN`
* RJ45 pin 6 (green)       = `T1OUT`
* RJ45 pin 8 (brown)       = `GND`

![image](https://user-images.githubusercontent.com/19826327/146428324-29e3f9bf-6cc3-415c-9d60-fa5ee3d65613.png)

> Do **not** use GPIO1/GPIO3 (shared with the onboard USB-UART), GPIO16/GPIO17 (RGB LED) or
> GPIO21 (TFT backlight).

**----Español----**

Consola de la batería en la placa CYD (conector **CN1**, definido por `BAT_RX_PIN` / `BAT_TX_PIN` en
`PylontechMonitoring/py_uart.h`):

* ESP **GPIO22** ← `R1OUT`
* ESP **GPIO27** → `T1IN`
* Conversor de niveles alimentado desde **3V3**, GND común con la batería

Los US2000C/US3000C/US5000 usan un conector RJ45 (mismo esquema, distinto conector):

* RJ45 pin 3 (blanco-verde) = `R1IN`
* RJ45 pin 6 (verde)        = `T1OUT`
* RJ45 pin 8 (marrón)       = `GND`

> **No** uses GPIO1/GPIO3 (están compartidos con el USB-UART de la placa), GPIO16/GPIO17 (LED RGB) ni
> GPIO21 (retroiluminación de la TFT).

## Getting started / Primeros pasos

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) plus the ESP32 board support.
2. Copy the contents of the
   [PylontechMonitoring/libraries](PylontechMonitoring/libraries) subdirectory into your
   [Arduino IDE libraries folder](https://forum.arduino.cc/index.php?topic=88380.0).
3. Open [PylontechMonitoring/PylontechMonitoring.ino](PylontechMonitoring/PylontechMonitoring.ino).
4. Select the partition scheme **`partitions/pylontech_ota_spiffs.csv`** (dual OTA + 512 KB SPIFFS),
   located in [`PylontechMonitoring/partitions`](PylontechMonitoring/partitions).
5. Flash and power the ESP32 via USB.
6. Connect to the WiFi AP `pylontech-xxxx`, browse `192.168.4.1/filemanager` and upload **all** files
   from the [`PylontechMonitoring/data`](PylontechMonitoring/data) folder to SPIFFS.
7. Connect the ESP32 to the MAX3232, the MAX3232 to the RJ11/RJ45 console plug (all three lines),
   and the plug into the battery's serial/console port (the master battery if you have several).
8. Browse `http://192.168.4.1` → *Settings* → scan for WiFi and connect to your network.
   The AP switches off ~30 s after connecting (after a refresh you can see the ESP's IP address).
9. Optional: configure MQTT on the *Settings* page. `MQTT` = topic prefix, `Send` = Home Assistant
   autodiscovery. Use the *Console* page to talk to the battery directly.

Configuration pages and values are password protected. Default values are:

* user → `admin`
* pass → `12345`

**Careful with OTA:** only flash the `.bin` produced by a fresh build (see below). Never flash an old
artifact left over from a previous build — it silently downgrades the device.

**----Español----**

1. Instala el [Arduino IDE](https://www.arduino.cc/en/software) más el soporte para placas ESP32.
2. Copia el contenido del subdirectorio
   [PylontechMonitoring/libraries](PylontechMonitoring/libraries) dentro de tu
   [carpeta de librerías del Arduino IDE](https://forum.arduino.cc/index.php?topic=88380.0).
3. Abre [PylontechMonitoring/PylontechMonitoring.ino](PylontechMonitoring/PylontechMonitoring.ino).
4. Selecciona el esquema de particiones **`partitions/pylontech_ota_spiffs.csv`** (OTA dual + 512 KB de
   SPIFFS), ubicado en [`PylontechMonitoring/partitions`](PylontechMonitoring/partitions).
5. Flashea y alimenta el ESP32 por USB.
6. Conéctate al AP WiFi `pylontech-xxxx`, entra a `192.168.4.1/filemanager` y sube **todos** los archivos
   de la carpeta [`PylontechMonitoring/data`](PylontechMonitoring/data) a SPIFFS.
7. Conecta el ESP32 al MAX3232, el MAX3232 al conector RJ11/RJ45 de consola (las tres líneas) y el
   conector al puerto serie/consola de la batería (la batería maestra si tienes varias).
8. Entra a `http://192.168.4.1` → *Settings* → escanea y conéctate a tu red.
   El AP se apaga ~30 s después de conectar (tras refrescar puedes ver la IP del ESP).
9. Opcional: configura MQTT en la página *Settings*. `MQTT` = prefijo del topic, `Send` =
   autodescubrimiento para Home Assistant. Usa la página *Console* para hablar directamente con la batería.

Las páginas y valores de configuración están protegidos por contraseña. Los valores por defecto son:

* usuario → `admin`
* contraseña → `12345`

**Cuidado con el OTA:** flashea solo el `.bin` producido por una compilación reciente (ver abajo). Nunca
flashees un artefacto viejo de una compilación anterior — degrada el dispositivo.

### Build from the command line (arduino-cli) / Compilar desde la línea de comandos (arduino-cli)

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 \
  --libraries PylontechMonitoring/libraries \
  --build-property build.partitions=PylontechMonitoring/partitions/pylontech_ota_spiffs.csv \
  PylontechMonitoring
```

The image lands in `~/.cache/arduino/sketches/<hash>/PylontechMonitoring.ino.bin`. Upload it through
the web UI (*Update* page) or via `POST /api/ota`.

**----Español----**

La imagen resultante queda en `~/.cache/arduino/sketches/<hash>/PylontechMonitoring.ino.bin`. Súbela a
través de la interfaz web (página *Update*) o vía `POST /api/ota`.

## Boot button / Botón de arranque

| Action | Effect |
| --- | --- |
| 1× short press | Enable a temporary WiFi AP (5 min, AP+STA — your home WiFi stays connected) |
| ≥5 s hold | Reset WiFi credentials (drops to AP only) |
| 5× short press | Reset WiFi credentials (same as above)|
| 15 s hold | Factory reset (clears all NVS + reboot) |

| Acción | Efecto |
| --- | --- |
| 1 pulsación corta | Activa un AP WiFi temporal (5 min, AP+STA — tu WiFi de casa sigue conectada) |
| Mantener ≥5 s | Resetea las credenciales WiFi (cae a modo AP únicamente) |
| 5 pulsaciones cortas | Resetea las credenciales WiFi (igual que el anterior)|
| Mantener 15 s | Restablecimiento de fábrica (borra toda la NVS + reinicio) |

## Configuration / Configuración

Everything is stored in **ESP32 NVS** (no config files) and edited from the web UI:

* `config` namespace — WiFi, MQTT, NTP, device settings
* `battery_pwr` / `battery_bat` / `battery_stat` — polling intervals, enabled flags, field configs
* `screen` — display settings; `touch` — touch calibration

The scheduler polls the battery (`pwr` / `bat` / `stat`) at configurable intervals (defaults: 60 s /
5 min / 30 min). BAT and STAT parsers can be enabled/disabled individually.

**----Español----**

Todo se guarda en la **NVS del ESP32** (sin archivos de configuración) y se edita desde la interfaz web:

* namespace `config` — WiFi, MQTT, NTP, ajustes del dispositivo
* `battery_pwr` / `battery_bat` / `battery_stat` — intervalos de sondeo, flags de habilitación, configuración de campos
* `screen` — ajustes de pantalla; `touch` — calibración táctil

El planificador interroga la batería (`pwr` / `bat` / `stat`) a intervalos configurables (por defecto:
60 s / 5 min / 30 min). Los parsers BAT y STAT se pueden habilitar/deshabilitar individualmente.

## Architecture / Arquitectura

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
├── partitions/               pylontech_ota_spiffs.csv (dual OTA + SPIFFS)
└── libraries/                vendored Arduino libraries
```

**----Español----**

Dos tareas FreeRTOS, `loop()` está vacío a propósito:

* **Núcleo 1 (prioridad 2)** — pipeline UART en tiempo real: `Scheduler → UART → Parser → frameQueue`
* **Núcleo 0 (prioridad 1)** — todo lo demás: bucle del planificador, publicación MQTT, servidor web,
  gestor WiFi, pantalla

```
PylontechMonitoring/
├── PylontechMonitoring.ino   punto de entrada, secuencia de arranque, creación de tareas
├── config.cpp/h              AppConfig, persistencia en NVS, estructuras de datos
├── py_uart.cpp/h             UART (RX=22, TX=27), autodetección de baudios
├── py_scheduler.cpp/h        cola de comandos, sondeo periódico PWR/BAT/STAT
├── py_parser_pwr|bat|stat.*  parsers del protocolo (doble búfer)
├── py_mqtt*.cpp/h            MQTT + autodescubrimiento para Home Assistant
├── py_display.cpp/h          TFT ILI9341 + táctil XPT2046
├── py_sdcard.cpp/h           registro en SD, rotación, exportación
├── py_wifimanager.cpp/h      WiFi AP/STA, escaneo, conexión
├── py_systemmanager.cpp/h    botón de arranque, reinicio programado, reset de fábrica, eventos
├── py_log.cpp/h              registro (serie + web)
├── data/                     contenido web de SPIFFS (HTML/CSS/JS)
├── web/                      handlers de rutas del servidor web y endpoints de la API en C++
├── partitions/               pylontech_ota_spiffs.csv (OTA dual + SPIFFS)
└── libraries/                librerías Arduino vendorizadas
```

## Battery protocol / Protocolo de la batería

* **PWR** — power data: voltage, current, temperature, SOC per module
* **BAT** — cell data: individual cell voltages (Vlow, Vhigh, Vdiff)
* **STAT** — statistics: module health info
* Battery modes: **STACK** (daisy-chain) or **HUB** (with hub/Masterhub addressing)
* Cell diff warnings: 100 mV (warn) / 150 mV (error) — configurable

**----Español----**

* **PWR** — datos de potencia: tensión, corriente, temperatura, SOC por módulo
* **BAT** — datos de celdas: tensiones individuales de cada celda (Vlow, Vhigh, Vdiff)
* **STAT** — estadísticas: información de salud de los módulos
* Modos de batería: **STACK** (en cadena/daisy-chain) o **HUB** (con direccionamiento por hub/Masterhub)
* Avisos de diferencia entre celdas: 100 mV (aviso) / 150 mV (error) — configurables

## i18n

Every new/changed web UI string ships in **both** English and Spanish
(`data/static_i18n.js`, identical key sets). Server responses and log messages stay English.

**----Español----**

Cada texto nuevo o modificado de la interfaz web se entrega **en inglés y en español**
(`data/static_i18n.js`, con conjuntos de claves idénticos). Las respuestas del servidor y los mensajes
de log permanecen en inglés.

## Credits / Créditos

* Original project: [irekzielinski/Pylontech-Battery-Monitoring](https://github.com/irekzielinski/Pylontech-Battery-Monitoring)
* ESP8266/D1 mini port: [hidaba](https://github.com/hidaba)
* ESP32 port: [HeldvomForst](https://github.com/HeldvomForst)

**----Español----**

* Proyecto original: [irekzielinski/Pylontech-Battery-Monitoring](https://github.com/irekzielinski/Pylontech-Battery-Monitoring)
* Puerto para ESP8266/D1 mini: [hidaba](https://github.com/hidaba)
* Puerto para ESP32: [HeldvomForst](https://github.com/HeldvomForst)
