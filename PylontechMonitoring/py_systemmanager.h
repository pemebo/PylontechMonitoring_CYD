#pragma once
#include <Arduino.h>
#include <vector>

namespace SystemManager {

    // One CPU statistics entry per FreeRTOS task
    struct CpuTaskStat {
        String name;
        uint32_t runtime;   // Ticks
        uint8_t percent;    // %
    };

    // Callbacks for events
    extern std::function<void()> onAPTemporary;
    extern std::function<void()> onWiFiReset;

    void begin();
    void loop();

    // internal functions
    void handleButton();
    void printCpuStats();
    void checkScheduledReboot();

    // CPU load and load values as JSON ("load0","load1","tasks")
    String buildCpuJson();
    // Memory values as JSON ("mem": heap + optional psram)
    String buildMemJson();

    // Read-only getters for the TFT STATUS screen.
    // Free heap in bytes (MALLOC_CAP_8BIT, the same value buildMemJson()
    // reports). Returns 0 when the value is unavailable.
    uint32_t getFreeHeap();
    // Per-core CPU load in percent (0-100). A core whose runtime stats are not
    // available yet reports 0.
    void getCpuLoads(uint8_t &core0, uint8_t &core1);

    // Trigger events
    void triggerAPTemporary();
    void triggerWiFiReset();
    void triggerFactoryReset();
}