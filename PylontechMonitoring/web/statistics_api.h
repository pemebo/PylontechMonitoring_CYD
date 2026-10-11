#pragma once
#include <ArduinoJson.h>
#include "api_core.h"
#include "../config.h"
#include "../py_parser_pwr.h"
#include "../py_parser_stat.h"
#include "../py_scheduler.h"

extern AppConfig config;
extern PyScheduler py_scheduler;

// Statistics section of the Modules page.
// Public read-only view of the per-module STAT snapshot.
inline void registerStatisticsAPI() {

    // ---------------------------------------------------------
    // GET /api/statistics
    // ---------------------------------------------------------
    apiGet("/api/statistics", []() {

        DynamicJsonDocument doc(8192);
        doc["enableStat"] = config.battery.enableStat;

        JsonArray mods = doc.createNestedArray("modules");
        for (auto &m : currentPwrModules()) {
            if (!m.present) continue;
            const StatSummary &sm = statSummary[m.index];
            JsonObject o = mods.createNestedObject();
            o["index"]     = m.index;
            o["device"]    = sm.device;
            o["soh"]       = sm.soh;
            o["cycles"]    = sm.cycles;
            o["capAh"]     = sm.capAh;
            o["ov"]        = sm.ov;
            o["hv"]        = sm.hv;
            o["lv"]        = sm.lv;
            o["uv"]        = sm.uv;
            o["lifeWarn"]  = sm.lifeWarn;
            o["lifeAlarm"] = sm.lifeAlarm;
        }

        String out;
        serializeJson(doc, out);
        apiJson(out);
    });

    // ---------------------------------------------------------
    // POST /api/statistics/load  (send "stat N" per present module)
    // ---------------------------------------------------------
    apiPost("/api/statistics/load", []() {
        int n = 0;
        for (auto &m : currentPwrModules()) {
            if (!m.present) continue;
            py_scheduler.enqueue("stat " + String(m.index));
            n++;
        }
        if (n == 0) return apiError(400, F("No modules available"));
        apiText(F("OK"));
    });
}