#pragma once
#include <ArduinoJson.h>
#include "api_core.h"
#include "../config.h"
#include "../py_parser_pwr.h"

extern HealthStatus health;

// Public read-only view of the per-module snapshot shown on the TFT MODULES
// screen (same double-buffer access as the display task).
inline void registerModulesAPI() {
    apiGet("/api/modules", []() {

        DynamicJsonDocument doc(4096);
        PwrBuffer* pwr = pwrUseA ? &pwrA : &pwrB;

        // Stack overview
        JsonObject stack = doc.createNestedObject("stack");
        stack["soc"]           = pwr->stack.soc;
        stack["status"]        = pwr->stack.status;
        stack["voltage_mV"]    = pwr->stack.avgVoltage_mV;
        stack["current_mA"]    = pwr->stack.totalCurrent_mA;
        stack["temperature"]   = pwr->stack.temperature;
        stack["batteryCount"]  = pwr->stack.batteryCount;
        stack["last_update"]   = config.lastPwrUpdate;

        // Modules
        JsonArray mods = doc.createNestedArray("modules");
        for (auto &m : pwr->modules) {
            if (!m.present) continue;

            JsonObject o = mods.createNestedObject();
            o["index"]          = m.index;
            o["soc"]            = m.soc;
            o["voltage_mV"]     = m.voltage_mV;
            o["current_mA"]     = m.current_mA;
            o["temperature_mC"] = m.temperature;

            float cellDiff = -1;
            for (auto &h : health.modules) {
                if (h.index != m.index) continue;
                cellDiff = h.cellDiff;
                break;
            }

            const String *baseSt = m.fields.get("Base.St");
            if (!baseSt) baseSt = m.fields.get("Base");
            o["cellDiff"] = cellDiff >= 0 ? cellDiff : -1.0;
            o["baseSt"]   = baseSt ? *baseSt : String();
        }

        String out;
        serializeJson(doc, out);
        apiJson(out);
    });
}