#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <vector>
#include <esp_heap_caps.h>

#include "nvs_flash.h"
#include "py_systemmanager.h"
#include "py_log.h"          // Log()
#include "py_wifimanager.h"  // WiFi reset
#include "config.h"          // zentrale Konfig

// ----------------------------------------------------
// FreeRTOS CPU Runtime Stats
// ----------------------------------------------------
extern "C" uint32_t ulHighFrequencyTimerTicks = 0;

void IRAM_ATTR highFreqTimer(void* arg) {
    ulHighFrequencyTimerTicks++;
}

// Parses the vTaskGetRunTimeStats output into tasks + core load. Single source
// of truth shared by buildCpuJson(), printCpuStats() and getCpuLoads().
// `tasks` may be null when only the two core loads are needed (getCpuLoads),
// which avoids building the per-task vector on every STATUS redraw.
static void parseRunTimeStats(int &load0, int &load1,
                              std::vector<SystemManager::CpuTaskStat> *tasks) {
    load0 = -1;
    load1 = -1;
    if (tasks) tasks->clear();

    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    vTaskGetRunTimeStats(buffer);

    char* saveLine = nullptr;
    char* line = strtok_r(buffer, "\n", &saveLine);
    while (line != nullptr) {
        char* saveTab = nullptr;
        char* nameTok = strtok_r(line, "\t", &saveTab);
        char* timeTok = nameTok ? strtok_r(nullptr, "\t", &saveTab) : nullptr;
        char* pctTok  = timeTok ? strtok_r(nullptr, "\t", &saveTab) : nullptr;

        if (nameTok && timeTok && pctTok) {
            SystemManager::CpuTaskStat st;
            st.name = String(nameTok);
            st.name.trim();
            st.runtime = strtoul(timeTok, nullptr, 10);
            String pct = String(pctTok);
            pct.replace("%", "");
            st.percent = (uint8_t)pct.toInt();

            // Skip header line (?Task / Abs Time / %)
            if (!(st.name == "Task" && st.runtime == 0 && st.percent == 0)) {
                if (tasks) tasks->push_back(st);

                if (st.name == "IDLE0") load0 = 100 - (int)st.percent;
                if (st.name == "IDLE1") load1 = 100 - (int)st.percent;
            }
        }
        line = strtok_r(nullptr, "\n", &saveLine);
    }
}

// ----------------------------------------------------
// Configuration
// ----------------------------------------------------
static const uint8_t BUTTON_PIN = 0;   // BOOT-Button
static const unsigned long LONG_PRESS_TIME = 15000;   // >15s → factory reset
static const unsigned long MEDIUM_PRESS_TIME = 5000;  // >=5s → WiFi reset
static const unsigned long SHORT_PRESS_MAX = 1000;    // <1s → short press
static const unsigned long MULTI_PRESS_TIMEOUT = 1000; // 1s pause between short presses

// ----------------------------------------------------
// Interne Variablen
// ----------------------------------------------------
static unsigned long buttonPressStart = 0;
static unsigned long lastButtonRelease = 0;
static int shortPressCount = 0;
static bool buttonWasPressed = false;

// Boot-Counter
static Preferences prefs;
static int bootCounter = 0;

// ----------------------------------------------------
// Initialisierung
// ----------------------------------------------------
void SystemManager::begin() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    prefs.begin("system", false);
    bootCounter = prefs.getInt("bootcount", 0);
    bootCounter++;
    prefs.putInt("bootcount", bootCounter);
    prefs.end();

    Log(LOG_INFO, "SystemManager: BootCounter = " + String(bootCounter));
    // ----------------------------------------------------
    // High-Frequency Timer for FreeRTOS CPU statistics
    // ----------------------------------------------------
    const esp_timer_create_args_t timerArgs = {
        .callback = &highFreqTimer,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "cpu_timer"
    };

    esp_timer_handle_t timerHandle;
    esp_timer_create(&timerArgs, &timerHandle);

    // 1 µs Takt (1 MHz)
    esp_timer_start_periodic(timerHandle, 100);

    Log(LOG_INFO, "SystemManager: CPU Runtime Stats aktiviert");

}

// ----------------------------------------------------
// Button-Handling
// ----------------------------------------------------
void SystemManager::loop() {
    handleButton();
    checkScheduledReboot();

    static unsigned long lastCpuPrint = 0;
    if (millis() - lastCpuPrint > 5000) {
        lastCpuPrint = millis();
        printCpuStats();
    }
}

// ----------------------------------------------------
// Automatic scheduled reboot
// Reboot at rbt_hour:rbt_min every rbt_days days. The anchor rbt_last (the
// last scheduled reboot timestamp) lives in NVS so the schedule survives
// manual reboots and crashes: a reboot fires once the time-of-day matches AND
// either no anchor exists yet (first run) or the whole days elapsed since the
// anchor reach the configured interval.
// ----------------------------------------------------
void SystemManager::checkScheduledReboot() {
    static unsigned long lastCheck = 0;
    unsigned long now = millis();

    if (now - lastCheck < 1000) return;
    lastCheck = now;

    if (!config.autoReboot || !config.isSystemTimeValid()) return;

    int cur = config.getCurrentMinutesOfDay();
    int target = config.rebootHour * 60 + config.rebootMin;
    if (cur != target) return;

    Preferences p;
    p.begin("config", false);

    uint32_t last = p.getULong("rbt_last", 0);
    time_t nowT  = time(nullptr);

    // No anchor yet (first run after enabling): the first matching window must
    // reboot, not merely anchor, otherwise the first scheduled restart is
    // deferred by one whole interval. Treat a missing anchor as eligible.
    uint32_t elapsedDays = (last == 0)
        ? config.rebootEveryDays
        : (uint32_t)(nowT - (time_t)last) / 86400UL;

    if (elapsedDays >= config.rebootEveryDays) {
        // Write the anchor BEFORE restarting so the next tick sees a fresh
        // anchor and cannot trigger a reboot loop.
        p.putULong("rbt_last", (uint32_t)nowT);
        p.end();

        Log(LOG_WARN, "Scheduled automatic reboot (every " +
            String(config.rebootEveryDays) + " day(s) at " +
            String(config.rebootHour) + ":" + String(config.rebootMin) + ")");
        delay(200);
        ESP.restart();
    }

    p.end();
}


// ----------------------------------------------------
// Button evaluation
// ----------------------------------------------------
void SystemManager::handleButton() {
    bool pressed = (digitalRead(BUTTON_PIN) == LOW);
    unsigned long now = millis();

    // --- Button pressed ---
    if (pressed && !buttonWasPressed) {
        buttonWasPressed = true;
        buttonPressStart = now;
    }

    // --- Button released ---
    if (!pressed && buttonWasPressed) {
        buttonWasPressed = false;
        unsigned long pressDuration = now - buttonPressStart;

        // Long press → factory reset
        if (pressDuration > LONG_PRESS_TIME) {
            Log(LOG_WARN, "SystemManager: LONG PRESS → FACTORY RESET");
            triggerFactoryReset();
            return;
        }

        // Sustained press → WiFi reset. Without this, 1s..15s was a dead zone.
        // The 5x short-press sequence below is the other way to trigger it.
        if (pressDuration >= MEDIUM_PRESS_TIME) {
            Log(LOG_WARN, "SystemManager: SUSTAINED PRESS → WIFI RESET");
            triggerWiFiReset();
            return;
        }

        // Short press → count
        if (pressDuration < SHORT_PRESS_MAX) {
            shortPressCount++;
            lastButtonRelease = now;
        }
    }

    // --- Evaluate short presses ---
    if (shortPressCount > 0 && (now - lastButtonRelease > MULTI_PRESS_TIMEOUT)) {

        if (shortPressCount == 1) {
            Log(LOG_INFO, "SystemManager: 1x short press → AP TEMPORARY");
            triggerAPTemporary();
        }

        if (shortPressCount >= 5) {
            Log(LOG_WARN, "SystemManager: 5x short press → WIFI RESET");
            triggerWiFiReset();
        }

        shortPressCount = 0;
    }
}

// ----------------------------------------------------
// Event functions
// ----------------------------------------------------
void SystemManager::triggerAPTemporary() {
    if (onAPTemporary) onAPTemporary();
}

void SystemManager::triggerWiFiReset() {
    if (onWiFiReset) onWiFiReset();
}

void SystemManager::triggerFactoryReset() {

    Log(LOG_ERROR, "SystemManager: FACTORY RESET initiated");
    delay(200);

    // ----------------------------------------------------
    // Delete ALL saved data
    // ----------------------------------------------------
    Preferences p;

    Log(LOG_WARN, "SystemManager: FULL NVS ERASE");
    nvs_flash_erase();
    nvs_flash_init();

    Log(LOG_WARN, "SystemManager: all data cleared → restarting...");
    delay(500);

    ESP.restart();
}

void SystemManager::printCpuStats() {
    int load0 = -1;
    int load1 = -1;
    parseRunTimeStats(load0, load1, nullptr);

    if (load0 >= 0 && load1 >= 0) {
        Log(LOG_DEBUG,
            "CPU: idle0=" + String(100 - load0) + "% idle1=" + String(100 - load1) +
            "% load0=" + String(load0) + "% load1=" + String(load1) + "%");
    }
}

String SystemManager::buildCpuJson() {
    int load0 = -1;
    int load1 = -1;
    std::vector<CpuTaskStat> tasks;
    parseRunTimeStats(load0, load1, &tasks);

    String json = F("\"cpu\":{");
    json += "\"load0\":";
    json += (load0 >= 0 ? String(load0) : String("\"n/a\""));
    json += ",\"load1\":";
    json += (load1 >= 0 ? String(load1) : String("\"n/a\""));
    json += ",\"tasks\":[";

    for (size_t i = 0; i < tasks.size(); i++) {
        if (i > 0) json += ",";
        json += "{\"name\":\"" + tasks[i].name + "\",";
        json += "\"runtime\":" + String(tasks[i].runtime) + ",";
        json += "\"percent\":" + String(tasks[i].percent) + "}";
    }

    json += "]}";
    return json;
}

String SystemManager::buildMemJson() {
    String json = F("\"mem\":{");

    // MALLOC_CAP_8BIT, not ESP.getFreeHeap(): the ESP.* helpers report
    // MALLOC_CAP_INTERNAL, which also covers IRAM. On this chip IRAM is 32-bit
    // only and can never serve a String/std::vector/char allocation, so an
    // "internal" free block can be large and still useless to the sketch --
    // exactly the 34,804 B block that once made the heap look healthy while
    // 8-bit allocations were failing.
    int32_t heapTotal = (int32_t)heap_caps_get_total_size(MALLOC_CAP_8BIT);
    int32_t heapFree  = (int32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    json += "\"heap_total\":" + String(heapTotal) + ",";
    json += "\"heap_free\":" + String(heapFree) + ",";
    json += "\"heap_used\":" + String(heapTotal - heapFree > 0 ? heapTotal - heapFree : 0) + ",";
    json += "\"heap_min_free\":" + String((int32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)) + ",";
    json += "\"heap_largest\":"  + String((int32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    if (psramFound()) {
        int32_t psramTotal = ESP.getPsramSize();
        int32_t psramFree  = ESP.getFreePsram();
        json += ",\"psram_total\":" + String(psramTotal) + ",";
        json += "\"psram_free\":" + String(psramFree) + ",";
        json += "\"psram_used\":" + String(psramTotal - psramFree > 0 ? psramTotal - psramFree : 0) + ",";
        json += "\"psram_min_free\":" + String(ESP.getMinFreePsram()) + ",";
        json += "\"psram_largest\":" + String(ESP.getMaxAllocPsram());
    }

    json += "}";
    return json;
}

// ----------------------------------------------------
// Read-only getters (TFT STATUS screen)
// ----------------------------------------------------
uint32_t SystemManager::getFreeHeap() {
    // MALLOC_CAP_8BIT, matching buildMemJson(): ESP.getFreeHeap() reports
    // MALLOC_CAP_INTERNAL, which also counts IRAM that 8-bit allocations can
    // never use.
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
}

void SystemManager::getCpuLoads(uint8_t &core0, uint8_t &core1) {
    int load0 = -1;
    int load1 = -1;
    parseRunTimeStats(load0, load1, nullptr);
    core0 = (load0 >= 0) ? (uint8_t)load0 : 0;
    core1 = (load1 >= 0) ? (uint8_t)load1 : 0;
}

// ----------------------------------------------------
// Callback-Variablen
// ----------------------------------------------------
std::function<void()> SystemManager::onAPTemporary = nullptr;
std::function<void()> SystemManager::onWiFiReset = nullptr;