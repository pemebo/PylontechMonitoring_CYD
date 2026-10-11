#pragma once
#include <ArduinoJson.h>
#include <vector>
#include "api_core.h"
#include "../config.h"
#include "../py_parser_pwr.h"
#include "../py_cellstore.h"
#include "../py_scheduler.h"

extern AppConfig config;
extern PyScheduler py_scheduler;

// "Cell voltages" / "Cell SOCs" section of the Modules page.
// Public read-only view of the per-module cell snapshot fed from BAT frames.
inline void registerCellsAPI() {

    // ---------------------------------------------------------
    // GET /api/cells
    // ---------------------------------------------------------
    apiGet("/api/cells", []() {

        DynamicJsonDocument doc(8192);
        doc["enableBat"] = config.battery.enableBat;
        doc["intervalBat"] = config.battery.intervalBat;
        doc["batLoading"] = py_scheduler.isBatLoading();

        JsonArray mods = doc.createNestedArray("modules");
        for (auto &m : currentPwrModules()) {
            if (!m.present) continue;

            JsonObject o = mods.createNestedObject();
            o["index"]  = m.index;

            JsonArray volts = o.createNestedArray("voltages");
            JsonArray socs  = o.createNestedArray("socs");

            if ((size_t)m.index < cellStore.size() && cellStore[m.index].have) {
                for (auto &c : cellStore[m.index].cells) {
                    volts.add(c.volt_mV);
                    socs.add(c.soc);
                }
            }
        }

        String out;
        serializeJson(doc, out);
        apiJson(out);
    });

    // ---------------------------------------------------------
    // POST /api/cells/load  (send "bat N" per present module)
    // ---------------------------------------------------------
    apiPost("/api/cells/load", []() {
        if (config.battery.enableBat) {
            return apiError(409, F("BAT polling is managed by the scheduler"));
        }

        std::vector<String> commands;
        for (auto &m : currentPwrModules()) {
            if (!m.present) continue;
            commands.push_back("bat " + String(m.index));
        }
        if (commands.empty()) return apiError(400, F("No modules available"));
        if (!py_scheduler.tryEnqueueBatBatch(commands)) {
            return apiError(409, F("BAT batch already loading"));
        }
        apiText(F("OK"));
    });
}
