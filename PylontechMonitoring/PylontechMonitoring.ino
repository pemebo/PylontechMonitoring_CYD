// =========================
// PylontechMonitoring (ESP32-S)
// Clean architecture with 2 tasks:
//   - Task 1 (Core 0): Real‑time pipeline (UART + Parser)
//   - Task 2 (Core 1): Non‑critical pipeline (Scheduler + MQTT + Webserver + WiFi)
// =========================



// ---- System Includes ----
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <TimeLib.h>
#include <SPIFFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <new>
#include <exception>
#include <esp_heap_caps.h>

// ---- Project Includes ----
#include "esp_log.h"
#include "config.h"
#include "py_wifimanager.h"
#include "wp_webserver.h"
#include "py_systemmanager.h"
#include "py_uart.h"
#include "py_scheduler.h"
#include "py_parser_pwr.h"
#include "py_parser_bat.h"
#include "py_parser_stat.h"
#include "py_cellstats.h"
#include "py_cellstore.h"
#include "py_log.h"
#include "py_mqtt.h"
#include "py_display.h"
#include "py_sdcard.h"
#include "esp_debug_helpers.h"

// =========================
//  Failed-allocation diagnostic hook
// =========================
// heap_caps_register_failed_alloc_callback() fires for EVERY allocation that
// fails, on any task, at the moment it fails -- before the resulting
// std::bad_alloc unwinds. The existing try/catch handlers can only report the
// heap state after the fact; this reports the size that was actually requested
// plus a backtrace, which is what identifies the allocation site.
static bool s_allocHookBusy = false;
static uint32_t s_allocFailCount = 0;

static void onFailedAlloc(size_t size, uint32_t caps, const char* fn) {
    // The backtrace walk must not recurse into another failed allocation.
    if (s_allocHookBusy) return;
    s_allocHookBusy = true;

    uint32_t n = ++s_allocFailCount;

    // Rate-limited on purpose. Walking and formatting a 14-frame backtrace on
    // every failure is itself expensive and floods the 115200 baud log (826
    // lines in 4 min during testing), which adds load exactly when the heap is
    // already under pressure. The first few failures carry the diagnostic value;
    // the rest only need the count.
    if (n <= 5) {
        Serial.printf("[ALLOCFAIL #%u] want=%u caps=0x%08x fn=%s\n",
                      (unsigned)n, (unsigned)size, (unsigned)caps, fn ? fn : "?");

        // Largest 8-bit block is the metric that matters: Arduino String and
        // std::vector allocate through the 8-bit-capable heap, and the observed
        // signature was plenty of free heap chopped into blocks too small to use.
        multi_heap_info_t d, b8;
        heap_caps_get_info(&d,  MALLOC_CAP_DEFAULT);
        heap_caps_get_info(&b8, MALLOC_CAP_8BIT);
        Serial.printf("[ALLOCFAIL] D:free=%u largest=%u | 8B:free=%u largest=%u | I:free=%u maxalloc=%u\n",
                      (unsigned)d.total_free_bytes, (unsigned)d.largest_free_block,
                      (unsigned)b8.total_free_bytes, (unsigned)b8.largest_free_block,
                      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

        esp_backtrace_print(14);
    } else if (n == 6) {
        Serial.printf("[ALLOCFAIL] further failures suppressed; total=%u\n",
                      (unsigned)n);
    }

    s_allocHookBusy = false;
}

// =========================
//  Global Data Structures
// =========================

// Magic Header – MUSS in der .bin stehen
const char OTA_MAGIC_HEADER[] = "PYLONTECH_FW_V1";



// Global queue handle
QueueHandle_t mqttQueue;
QueueHandle_t rtWakeQueue;

// frameQueue kommt aus config.cpp (extern in config.h deklariert)
extern QueueHandle_t frameQueue;

// Global objects
PyUart py_uart;
extern PyScheduler py_scheduler;
extern PyMqtt py_mqtt;
extern PySdCard sdCard;

// =========================
 //  Task 1: Real‑Time Pipeline (Core 0)
 //  UART → Parser → Queue
 // =========================
void realtimeTask(void* parameter) {

    // Hoisted out of the command block so the catch handlers below can report it.
    String cmd;

    for (;;) {

        // 1) UART must be ready
        if (!py_uart.isReady()) {
py_uart.begin(BAT_RX_PIN, BAT_TX_PIN);
            vTaskDelay(100);
            continue;
        }

        // 2) If UART is busy, wait
        if (py_uart.isBusy()) {
            vTaskDelay(1);
            continue;
        }

        // 3) Check if scheduler has a command
        if (!py_scheduler.hasQueuedCommand()) {
            vTaskDelay(1);
            continue;
        }

        // Exception boundary for the whole command path. A std::bad_alloc from any
        // String allocation below would otherwise unwind past this task into the
        // default Arduino task wrapper and abort() the whole process.
        //
        // The boundary opens BEFORE the pop on purpose: popNextCommand() erases the
        // queue entry and sets commandInFlight BEFORE its own trailing Log() call, so
        // an allocation failure in there would otherwise strand the scheduler with
        // commandInFlight stuck true and popNextCommand() returning "" forever.
        try {
            // 4) Pop next command
            cmd = py_scheduler.popNextCommand();
            if (cmd.length() == 0) {
                vTaskDelay(1);
                continue;
            }

            Log(LOG_INFO, "Task1: executing command: " + cmd);

            // 5) Execute UART command (blocking allowed). The frame is validated,
            //    parsed and consumed inside sendCommand(), so only the return code matters.
            bool ok = py_uart.sendCommand(cmd.c_str());

            if (!ok) {
                Log(LOG_WARN, "Task1: UART failed for command: " + cmd);
                py_scheduler.markCommandFinished();
                vTaskDelay(1);
                continue;
            }

            // 6) Mark command finished
            py_scheduler.markCommandFinished();
        } catch (const std::bad_alloc &) {
            // Serial.printf, not Log(): Log() concatenates Strings and would
            // throw again inside the handler.
            Serial.printf("[DIAG] Task1 bad_alloc: cmd='%s' free=%u max=%u largest8=%u\n",
                          cmd.length() ? cmd.c_str() : "",
                          (unsigned)ESP.getFreeHeap(),
                          (unsigned)ESP.getMaxAllocHeap(),
                          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            // markCommandFinished() is idempotent: its commandInFlight guard makes
            // it a no-op when nothing was in flight and prevents a double
            // batCommandsPending decrement. Safe to call unconditionally, whether
            // the throw came before the pop, between pop and send, or after the
            // normal-path markCommandFinished() already ran.
            py_uart.abortFrame();
            py_scheduler.markCommandFinished();
            vTaskDelay(100 / portTICK_PERIOD_MS);
        } catch (const std::exception &e) {
            Serial.printf("[DIAG] Task1 exception: cmd='%s' what=%s free=%u max=%u\n",
                          cmd.length() ? cmd.c_str() : "", e.what(),
                          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
            py_uart.abortFrame();
            py_scheduler.markCommandFinished();
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        // 8) Small delay
        vTaskDelay(1);
    }
}

// =========================
//  Task 2: Non‑Critical Pipeline (Core 1)
//  Scheduler + MQTT + Webserver + WiFi
// =========================
// The whole non-critical body, kept as its own function so the exception
// boundary in noncriticalTask() can enclose it without re-indenting ~190 lines.
// It never returns normally; an escaping exception unwinds to that boundary.
static void noncriticalTaskLoop() {
    MqttMessage msg;

    unsigned long lastSched = 0;
    unsigned long lastMqtt  = 0;
    unsigned long lastWeb   = 0;
    unsigned long lastSys   = 0;
    unsigned long lastRam   = 0;

    for (;;) {
        unsigned long now = millis();

        // 1) Scheduler
        if (now - lastSched >= 20) {
            lastSched = now;
            py_scheduler.loop();
        }

        // 2) MQTT raw queue
        while (xQueueReceive(mqttQueue, &msg, 0) == pdTRUE) {
            py_mqtt.publishRaw(msg.topic, msg.payload);
        }

        // 2.5) Feed BAT cell data to the display.
        // Consume by sequence, not by the one-shot flag: the flag is shared with
        // MQTT (cleared in PyMqtt::loop) and stays latched while MQTT is disabled,
        // which used to rebuild the cell store on every ~1 ms loop iteration.
        static uint32_t lastDisplayBatSeq = 0;
        if (batFrameSeq != lastDisplayBatSeq) {
            lastDisplayBatSeq = batFrameSeq;
            BatBuffer* bat = batUseA ? &batA : &batB;
            // File the frame under its OWN module index (published with the cells),
            // never a separate shared index that can lag and store this frame under
            // the previous module. Skip empty frames so a module is never blanked.
            if (!bat->cells.empty()) {
                updateCellStore(bat->moduleIndex, bat->cells, bat->header);
                display.updateCells(bat->moduleIndex);
            }
        }

        // 3) MQTT internal
        if (now - lastMqtt >= 20) {
            lastMqtt = now;
            py_mqtt.loop();
        }

        // 4) Webserver
        if (now - lastWeb >= 20) {
            lastWeb = now;
            WebServerModule_handle();
        }

        // 5) WiFi + System
        if (now - lastSys >= 20) {
            lastSys = now;
            WiFiManagerModule::loop();
            SystemManager::loop();
        }

        // 6) RAM Debug
        if (config.logDebug) {
            if (now - lastRam >= 5000) {
                lastRam = now;

                // Serial.printf, not Log(): Log() builds a String and this runs in
                // Task 2, which has no exception boundary -- an allocation failure
                // here would abort the device. Reports the 8-bit pool that String
                // and std::vector actually allocate from; ESP.getFreeHeap() and
                // getMaxAllocHeap() use MALLOC_CAP_INTERNAL and include IRAM, which
                // is 32-bit only and can never satisfy a byte allocation.
                multi_heap_info_t d, b8;
                heap_caps_get_info(&d,  MALLOC_CAP_DEFAULT);
                heap_caps_get_info(&b8, MALLOC_CAP_8BIT);
                Serial.printf("RAM: D:free=%u largest=%u min=%u alloc=%u | 8B:free=%u largest=%u\n",
                              (unsigned)d.total_free_bytes, (unsigned)d.largest_free_block,
                              (unsigned)d.minimum_free_bytes, (unsigned)d.total_allocated_bytes,
                              (unsigned)b8.total_free_bytes, (unsigned)b8.largest_free_block);
            }
        }

        // Display-Update (NICHT im Debug-Block!)
        // Consume by sequence: the one-shot flag is also owned by MQTT and stays
        // latched while MQTT is disabled, which used to deep-copy the whole PWR
        // buffer on every ~1 ms loop iteration.
        static uint32_t lastDisplayPwrSeq = 0;
        if (pwrFrameSeq != lastDisplayPwrSeq) {
            lastDisplayPwrSeq = pwrFrameSeq;

            auto pwr = pwrUseA ? pwrA : pwrB;

            if (!pwr.modules.empty()) {
                auto &s = pwr.stack;

                display.updatePwr(
                    s.avgVoltage_mV,
                    s.totalCurrent_mA,
                    s.temperature,
                    s.soc,
                    s.status
                );
                // Snapshot the health event count at the PWR frame boundary (the
                // parser has already updated it and bumped pwrFrameSeq), so the
                // main-screen EVENTS row is read in Task 2, not raced with Task 1.
                display.setEventCount((int)health.events.size());

                // Snapshot per-module data for the modules screen.
                // Function-static and reused: capacity persists after the first
                // frame, so the steady state does not allocate.
                static std::vector<DisplayModule> mods;
                mods.clear();
                mods.reserve(pwr.modules.size());
                for (const auto &m : pwr.modules) {
                    if (!m.present) continue;

                    DisplayModule dm;
                    dm.index = m.index;
                    dm.soc = m.soc;
                    dm.volt_mV = m.voltage_mV;
                    dm.curr_mA = m.current_mA;
                    dm.temp_mC = m.temperature;
                    dm.cellDiff_mV = -1;
                    dm.status = 0;

                    for (const auto &h : health.modules) {
                        if (h.index != m.index) continue;
                        dm.cellDiff_mV = (int)(h.cellDiff * 1000.0f + 0.5f);
                        dm.status = (h.status == "Error")   ? 2 :
                                    (h.status == "Warning") ? 1 : 0;
                        break;
                    }
                    mods.push_back(dm);
                }
                display.updateModules(mods);
            }

            // Cell "today" extremes from PWR when BAT polling is disabled.
            // PWR only reports Vlow/Vhigh per module (no cell numbers), so the
            // values are captured without a cell index or a capture time.
            if (!config.battery.enableBat) {
                for (auto &m : pwr.modules) {
                    if (!m.present) continue;
                    String vlo = m.fields.asString("Vlow");
                    String vhi = m.fields.asString("Vhigh");
                    if (vlo == "-" || vhi == "-") continue;
                    int low = vlo.toInt();
                    int high = vhi.toInt();
                    if (low > 1000 && low < 5000 && high > 1000 && high < 5000) {
                        updateCellStats(m.index, low, high, 0, 0, false);
                    }
                }
            }
        }

        bool apMode = (WiFi.getMode() == WIFI_MODE_AP || WiFi.getMode() == WIFI_MODE_APSTA);
        String apIp = apMode ? WiFi.softAPIP().toString() : String();

        display.updateWifi(
            WiFi.status() == WL_CONNECTED,
            WiFi.localIP().toString(),
            WiFi.RSSI(),
            apMode,
            apIp
        );

        display.pollTouch();

        // Update display only every 500 ms
        static unsigned long lastDisplay = 0;
        if (now - lastDisplay >= 500) {
            lastDisplay = now;
            display.loop();
        }

        // SD card export processing
        sdCard.loop();


        vTaskDelay(1);
    }
}

// Exception boundary for the whole non-critical path. Task 1 has had one since
// 34d42b0; without this, a std::bad_alloc anywhere below -- a webserver String
// response, MQTT payload building, the display vectors, an SD export row --
// unwinds past this task into the Arduino wrapper and calls abort(), rebooting
// the device. An allocation failure has to degrade one poll cycle, not the device.
void noncriticalTask(void* parameter) {
    // Rate-limited the same way as the failed-allocation hook: the first few
    // failures carry the diagnostic value and the rest only need the count,
    // because formatting every one floods the log exactly when the heap is failing.
    static uint32_t errCount = 0;

    for (;;) {
        try {
            noncriticalTaskLoop();   // does not return normally
        } catch (const std::bad_alloc &) {
            // Serial.printf, not Log(): Log() builds Strings and would throw
            // again inside the handler.
            uint32_t n = ++errCount;
            if (n <= 5 || (n % 50) == 0) {
                Serial.printf(
                    "[DIAG] Task2 bad_alloc (n=%u) 8B:free=%u largest=%u\n",
                    (unsigned)n,
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            }
            vTaskDelay(100 / portTICK_PERIOD_MS);
        } catch (const std::exception &e) {
            uint32_t n = ++errCount;
            if (n <= 5 || (n % 50) == 0) {
                Serial.printf("[DIAG] Task2 exception (n=%u) what=%s\n",
                              (unsigned)n, e.what());
            }
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
}


// =========================
//  Setup
// =========================
void setup() {
    Serial.begin(115200);
    delay(100);

    WebLogInit();

    // Register before anything else so early allocation failures are visible.
    heap_caps_register_failed_alloc_callback(onFailedAlloc);

    Log(LOG_INFO, "System booting...");

    // Load configuration (the existing load() takes care of everything)
    config.load();

    // Create MQTT queue. Depth reduced 50 -> 12: 12 * 192 B = 2,304 B instead of
    // 9,732 B, returned to the 8-bit pool even when MQTT is disabled. Task 2
    // drains the queue every 20 ms, so 12 pending messages is ample.
    mqttQueue = xQueueCreate(
        12,                      // number of buffered messages
        sizeof(MqttMessage)      // size of each element
    );

    if (mqttQueue == NULL) {
        Log(LOG_ERROR, "MQTT Queue could not be created!");
    }

    // UART + Scheduler
    py_uart.begin(BAT_RX_PIN, BAT_TX_PIN);      // RX=22, TX=27 (CN1)
    py_scheduler.begin(&py_uart);

    // Initial command
    py_scheduler.enqueue("pwr");
    Log(LOG_INFO, "Scheduler: initial CMD_PWR enqueued");

    // System Manager
    SystemManager::begin();

    // WiFi + OTA + NTP
    WiFiManagerModule::begin();

    // Boot-button events: SystemManager stays decoupled from WiFiManagerModule,
    // so the two events are wired here.
    //   1x short press -> temporary AP (TEMP_AP_DURATION_MS)
    //   5x short press -> clear WiFi credentials and fall back to AP
    static const unsigned long TEMP_AP_DURATION_MS = 5UL * 60UL * 1000UL;
    SystemManager::onAPTemporary = []() {
        WiFiManagerModule::enable();
        WiFiManagerModule::startTemporaryAP(TEMP_AP_DURATION_MS);
    };
    SystemManager::onWiFiReset = []() {
        WiFiManagerModule::enable();
        WiFiManagerModule::resetWiFi();
    };

    // MQTT
    py_mqtt.begin();

    // SPIFFS
    if (!SPIFFS.begin(false)) {
        Log(LOG_WARN, "SPIFFS mount failed, formatting...");
        SPIFFS.begin(true);
    } else {
        Log(LOG_INFO, "SPIFFS mounted");
    }

    // Webserver
    WebServerModule_begin();

    display.begin();
    // The screen respects the saved master switch at boot: with enabled=false it
    // starts OFF and stays fully inert (no draw, no touch). applyScreenConfig
    // sets screenEnabled from config.screen.enabled.
    display.applyScreenConfig(config.screen);
    display.setScreenOn(config.screen.enabled);

    // Initialize SD card on VSPI (separate from display HSPI)
    sdCard.begin();

    // Start Task 1 (Real‑Time) on Core 1
    // Stack trimmed 8192 -> 5120: measured high-water mark is ~2.6 KB used,
    // ~5.5 KB still free. Task 2 is left at 8192 (only ~1.7 KB free measured).
    xTaskCreatePinnedToCore(
        realtimeTask,
        "RealTime Task",
        5120,
        NULL,
        2,          // higher priority
        NULL,
        1           // Core 1
    );

    // Start Task 2 (Non‑Critical + OTA + Webserver) auf Core 0
    xTaskCreatePinnedToCore(
        noncriticalTask,
        "NonCritical Task",
        8192,
        NULL,
        1,          // normal priority
        NULL,
        0           // Core 0
    );

    Log(LOG_INFO, "Setup complete");
}

// =========================
//  Arduino Loop (unused)
// =========================
void loop() {
    // Empty – all logic moved to FreeRTOS tasks
    vTaskDelay(1000);
}
