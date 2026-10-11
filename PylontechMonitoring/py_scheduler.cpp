#include <ctype.h>
#include "py_scheduler.h"
#include "py_log.h"
#include "py_parser_pwr.h"   // lastParsedStack + lastParsedModules
#include "config.h"

PyScheduler py_scheduler;

static bool isBatCommand(const String& cmd) {
    return cmd.startsWith("bat ");
}

static bool isBlankCommand(const String& cmd) {
    for (size_t i = 0; i < cmd.length(); i++) {
        if (!isspace(static_cast<unsigned char>(cmd[i]))) {
            return false;
        }
    }
    return true;
}

void PyScheduler::lockQueue() const {
    if (queueMutex != nullptr) {
        xSemaphoreTake(queueMutex, portMAX_DELAY);
    }
}

void PyScheduler::unlockQueue() const {
    if (queueMutex != nullptr) {
        xSemaphoreGive(queueMutex);
    }
}

void PyScheduler::begin(PyUart* u) {
    uart = u;

    if (queueMutex == nullptr) {
        queueMutex = xSemaphoreCreateMutex();
        if (queueMutex == nullptr) {
            Log(LOG_ERROR, "Scheduler: command queue mutex creation failed");
        }
    }

    lockQueue();
    queue.clear();
    commandInFlight = false;
    commandInFlightIsBat = false;
    batCommandsPending = 0;
    initialBatDone = false;
    lastCommandFinished = 0;
    unlockQueue();

    bootTime = millis();

    lastPwr  = millis();
    lastBat  = millis();
    lastStat = millis();

    initialPwrDone  = false;
    initialStatDone = false;

    Log(LOG_INFO, "Scheduler: started");
}

void PyScheduler::enqueue(const String& cmd) {
    if (isBlankCommand(cmd)) return;

    Log(LOG_DEBUG, "Scheduler: enqueue → " + cmd);

    QueuedCommand entry;
    entry.command = cmd;
    entry.isBat = isBatCommand(cmd);

    lockQueue();
    queue.push_back(entry);
    if (entry.isBat) batCommandsPending++;
    unlockQueue();
}

bool PyScheduler::tryEnqueueBatBatch(const std::vector<String>& commands) {
    if (commands.empty()) return false;

    lockQueue();
    if (batCommandsPending > 0) {
        unlockQueue();
        return false;
    }

    for (const auto& cmd : commands) {
        QueuedCommand entry;
        entry.command = cmd;
        entry.isBat = true;
        queue.push_back(entry);
        batCommandsPending++;
    }
    initialBatDone = true;
    unlockQueue();

    Log(LOG_DEBUG, "Scheduler: BAT batch enqueued (" + String(commands.size()) + " commands)");
    return true;
}

void PyScheduler::enqueueDatalistHistory(int module) {
    if (module <= 0) {
        Log(LOG_WARN, "Scheduler: Invalid module for datalist history");
        return;
    }
    String cmd = "datalist history " + String(module);
    Log(LOG_INFO, "Scheduler: enqueue datalist history → " + cmd);
    enqueue(cmd);
}

bool PyScheduler::hasQueuedCommand() const {
    lockQueue();
    bool queued = !queue.empty();
    unlockQueue();
    return queued;
}

bool PyScheduler::isBatLoading() const {
    lockQueue();
    bool loading = batCommandsPending > 0;
    unlockQueue();
    return loading;
}

String PyScheduler::popNextCommand() {
    lockQueue();
    if (commandInFlight || queue.empty()) {
        unlockQueue();
        return "";
    }

    QueuedCommand entry = queue.front();
    queue.erase(queue.begin());
    commandInFlight = true;
    commandInFlightIsBat = entry.isBat;
    unlockQueue();

    Log(LOG_DEBUG, "Scheduler: pop → " + entry.command);
    return entry.command;
}

void PyScheduler::markCommandFinished() {
    lockQueue();
    lastCommandFinished = millis();

    if (commandInFlight) {
        if (commandInFlightIsBat && batCommandsPending > 0) {
            batCommandsPending--;
        }
        commandInFlight = false;
        commandInFlightIsBat = false;
    }

    unlockQueue();
}

void PyScheduler::loop() {
    unsigned long now = millis();

    // ---------------------------------------------------------
    // UNKNOWN MODE → scheduler paused + logging every 30s
    // ---------------------------------------------------------
    if (g_batteryMode == BatteryMode::UNKNOWN) {
        static unsigned long lastLog = 0;
        if (now - lastLog > 30000) {
            Log(LOG_INFO, "Scheduler: UNKNOWN mode → waiting for UART detection");
            lastLog = now;
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
        return;
    }

    // ---------------------------------------------------------
    // HUB MODE → scheduler stays passive + logging every 30s
    // ---------------------------------------------------------
    if (g_batteryMode == BatteryMode::HUB) {
        static unsigned long lastLog = 0;
        if (now - lastLog > 30000) {
            Log(LOG_INFO, "Scheduler: HUB mode active → no commands are sent");
            lastLog = now;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return;
    }

    // ---------------------------------------------------------
    // STACK-MODUS → alles wie bisher
    // ---------------------------------------------------------

    // UART busy? Dann warten
    if (uart->isBusy()) return;

    unsigned long sinceBoot = now - bootTime;

    // ---------------------------------------------------------
    // INITIAL SEQUENCE (runs only once)
    // ---------------------------------------------------------

    // 1) PWR at T+15s
    if (!initialPwrDone && sinceBoot >= 15000) {
        enqueue("pwr");
        Log(LOG_INFO, "Scheduler: INITIAL PWR");
        initialPwrDone = true;
        return;
    }

    // 2) BAT at T+25s
    if (initialPwrDone && !initialBatDone && sinceBoot >= 25000) {
        enqueue("bat 1");
        Log(LOG_INFO, "Scheduler: INITIAL BAT");
        initialBatDone = true;
        return;
    }

    // 3) STAT at T+45s (all present modules, so the Statistics table is
    //    populated right after boot)
    if (initialBatDone && !initialStatDone && sinceBoot >= 45000) {
        // A disabled STAT parser must not build the STAT store at boot; it is
        // fetched on demand when the parser is (re)enabled (see handleApiStatSet).
        if (config.battery.enableStat) {
            bool any = false;
            for (const auto& m : currentPwrModules()) {
                if (!m.present) continue;
                enqueue("stat " + String(m.index));
                any = true;
            }
            if (!any) enqueue("stat 1");
            Log(LOG_INFO, "Scheduler: INITIAL STAT");
        } else {
            Log(LOG_INFO, "Scheduler: INITIAL STAT skipped (enableStat=false)");
        }
        initialStatDone = true;
        return;
    }
    // 4) DISCOVERY at T+50s
    if (initialStatDone && !initialDiscoveryDone && sinceBoot >= 50000) {

        Log(LOG_INFO, "Scheduler: INITIAL DISCOVERY triggered");

        discoveryPwrNeeded  = true;
        discoveryBatNeeded  = true;
        discoveryStatNeeded = true;

        initialDiscoveryDone = true;
        return;
    }


    // ---------------------------------------------------------
    // NORMAL CYCLIC SCHEDULING (after initial sequence)
    // ---------------------------------------------------------

    if (!initialStatDone) return;

    // PWR
    if (now - lastPwr >= config.battery.intervalPwr) {
        enqueue("pwr");
        lastPwr = now;
        Log(LOG_INFO, "Scheduler: PWR scheduled");
    }

    // BAT
    if (now - lastBat >= config.battery.intervalBat) {
        if (config.battery.enableBat || config.battery.cellsScreenActive) {
            for (const auto& m : currentPwrModules()) {
                if (!m.present) continue;
                enqueue("bat " + String(m.index));
            }
            if (config.battery.cellsScreenActive) {
                Log(LOG_INFO, "Scheduler: BAT scheduled for CELLS screen (cellsScreenActive)");
            } else {
                Log(LOG_INFO, "Scheduler: BAT scheduled (enableBat)");
            }
        }
        lastBat = now;
    }

    // STAT
    if (now - lastStat >= config.battery.intervalStat) {
        if (config.battery.enableStat) {
            for (const auto& m : currentPwrModules()) {
                if (!m.present) continue;
                enqueue("stat " + String(m.index));
            }
            Log(LOG_INFO, "Scheduler: STAT scheduled (present modules)");
        }
        lastStat = now;
    }
}
