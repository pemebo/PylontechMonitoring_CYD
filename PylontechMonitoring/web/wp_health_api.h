#pragma once
#include "api_core.h"
#include "auth.h"
#include "../config.h"
#include "../py_parser_pwr.h"

extern HealthStatus health;

inline void registerHealthAPI() {

    // ---------------------------------------------------------
    // GET /api/health
    // ---------------------------------------------------------
    // Public read-only view (auth only required for Write actions below)
    apiGet("/api/health", []() {

        DynamicJsonDocument doc(12288);

        auto timeHm = [](int m) {
            char b[8];
            snprintf(b, sizeof(b), "%02d:%02d", m / 60, m % 60);
            return String(b);
        };

        // MODULES
        JsonArray mods = doc.createNestedArray("modules");
        for (auto &m : health.modules) {
            JsonObject o = mods.createNestedObject();
            o["index"]    = m.index;
            o["status"]   = m.statusText;
            o["tempMax"]  = m.tempMax;
            o["cellMin"]  = m.cellMin;
            o["cellMax"]  = m.cellMax;
            o["cellDiff"] = m.cellDiff;

            // Today's extremes (shared day-store, fed by PWR or BAT). The cell
            // number and capture time are only meaningful when they came from a
            // BAT frame, so they are suppressed entirely while BAT parsing is
            // disabled (the store may still hold earlier BAT-sourced values).
            bool bat = config.battery.enableBat;
            CellDayStats &cs = cellDayStats[m.index];
            o["todayMin"]      = cs.todayMin_mV   >= 0 ? cs.todayMin_mV   / 1000.0f : -1.0f;
            o["todayMinCell"]  = bat ? cs.todayMinCell : 0;
            o["todayMinTime"]  = (bat && cs.todayMinMin >= 0) ? timeHm(cs.todayMinMin) : "";
            o["todayMax"]      = cs.todayMax_mV   >= 0 ? cs.todayMax_mV   / 1000.0f : -1.0f;
            o["todayMaxCell"]  = bat ? cs.todayMaxCell : 0;
            o["todayMaxTime"]  = (bat && cs.todayMaxMin >= 0) ? timeHm(cs.todayMaxMin) : "";
            o["deltaToday"]    = cs.todayDelta_mV >= 0 ? cs.todayDelta_mV / 1000.0f : -1.0f;
            o["deltaTodayTime"]= (bat && cs.todayDeltaMin >= 0) ? timeHm(cs.todayDeltaMin) : "";
        }

        // STACK
        JsonObject st = doc.createNestedObject("stack");
        st["cellMin"]  = health.stackCellMin;
        st["cellMax"]  = health.stackCellMax;
        st["cellDiff"] = health.stackCellDiff;

        // LIST HELPERS
        auto addList = [&](JsonArray arr, const std::vector<int> &v) {
            for (int x : v) arr.add(x);
        };

        addList(doc.createNestedArray("ok"),           health.okModules);
        addList(doc.createNestedArray("warn"),         health.warnModules);
        addList(doc.createNestedArray("error"),        health.errorModules);

        // HEALTH HISTORY EVENTS (date, module, text) — newest first
        JsonArray evArr = doc.createNestedArray("events");
        for (auto &e : health.events) {
            JsonObject o = evArr.createNestedObject();
            o["date"]   = e.time;
            o["module"] = e.module;
            o["event"]  = e.text;
        }

        // STRONGEST + COLOR
        doc["strongest"] = health.strongestMessage;
        doc["color"]     = health.color;

        // NEW: CONFIG VALUES
        JsonObject cfg = doc.createNestedObject("config");
        cfg["cellDiffWarn"]  = config.battery.cellDiffWarn;
        cfg["cellDiffError"] = config.battery.cellDiffError;

        String out;
        serializeJson(doc, out);
        apiJson(out);
    });

    // ---------------------------------------------------------
    // POST /api/health  (update thresholds)
    // ---------------------------------------------------------
    apiPostAuth("/api/health", []() {

        if (!server.hasArg("plain"))
            return apiError(400, F("Missing body"));

        StaticJsonDocument<512> req;
        if (deserializeJson(req, server.arg("plain")))
            return apiError(400, F("Invalid JSON"));

        // NEW: UPDATE CONFIG VALUES
        config.battery.cellDiffWarn  = req["cellDiffWarn"]  | config.battery.cellDiffWarn;
        config.battery.cellDiffError = req["cellDiffError"] | config.battery.cellDiffError;

        config.save();
        apiText(F("Health config saved"));
    });

    // ---------------------------------------------------------
    // GET /api/health/reset
    // ---------------------------------------------------------
    apiGetAuth("/api/health/reset", []() {
        config.wipeHealthHistory();
        health.modAlarmState.clear();
        health.modThreshState.clear();
        refreshHealthFromSnapshot();
        apiText(F("OK"));
    });
}
