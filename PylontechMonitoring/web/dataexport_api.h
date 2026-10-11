#pragma once
#include <WebServer.h>
#include <ArduinoJson.h>
#include <time.h>
#include "../config.h"
#include "../py_sdcard.h"
#include "../py_scheduler.h"
#include "auth.h"

extern WebServer server;
extern AppConfig config;
extern PySdCard sdCard;
extern PyScheduler py_scheduler;

// The export settings of the Data Export page travel with the start request
// (the Save Config button is gone) and are clamped exactly like
// POST /api/sdcard/config does. The settings are split by export family:
// battery data exports persist record rotation only, the BMS History
// export persists the timeout only. Each helper returns true when a value
// actually changed, so the caller writes NVS once and only when it has to.
static bool applyDataExportSettings(JsonDocument& req) {
    bool changed = false;

    if (!req["recordsPerFile"].isNull()) {
        uint32_t val = req["recordsPerFile"].as<uint32_t>();
        if (val < 100) val = 100;
        if (val > 100000) val = 100000;
        if (val != sdCardConfig.recordsPerFile) {
            sdCardConfig.recordsPerFile = val;
            changed = true;
        }
    }

    return changed;
}

static bool applyBmsExportSettings(JsonDocument& req) {
    bool changed = false;

    if (!req["exportTimeoutMs"].isNull()) {
        uint32_t val = req["exportTimeoutMs"].as<uint32_t>();
        if (val < 60000) val = 60000;
        if (val > 3600000) val = 3600000;
        if (val != sdCardConfig.exportTimeoutMs) {
            sdCardConfig.exportTimeoutMs = val;
            changed = true;
        }
    }

    return changed;
}

static void registerDataExportAPI() {

    // ---------------------------------------------------------
    // GET /api/sdcard/config
    // ---------------------------------------------------------
    server.on("/api/sdcard/config", HTTP_GET, []() {
        if (!authOk()) return authFail();

        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "application/json", "");
        server.sendContent("{");

        // "logPwr"/"logBat" are the persisted checkbox selection (what to
        // export). "enabled" is kept for backward compatibility only - the
        // Export button is the activation, it never gates an export.
        server.sendContent("\"enabled\":"); server.sendContent(sdCardConfig.enabled ? "true" : "false"); server.sendContent(",");
        server.sendContent("\"selectPwr\":"); server.sendContent(sdCardConfig.logPwr ? "true" : "false"); server.sendContent(",");
        server.sendContent("\"selectBat\":"); server.sendContent(sdCardConfig.logBat ? "true" : "false"); server.sendContent(",");
        server.sendContent("\"recordsPerFile\":"); server.sendContent(String(sdCardConfig.recordsPerFile)); server.sendContent(",");
        server.sendContent("\"exportTimeoutMs\":"); server.sendContent(String(sdCardConfig.exportTimeoutMs));
        server.sendContent(",\"sdReady\":"); server.sendContent(sdCard.isReady() ? "true" : "false");

        // Query intervals from Battery config
        server.sendContent(",\"queryIntervalPwr\":"); server.sendContent(String(config.battery.intervalPwr / 1000));
        server.sendContent(",\"queryIntervalBat\":"); server.sendContent(String(config.battery.intervalBat / 1000));
        server.sendContent(",\"enableBat\":"); server.sendContent(config.battery.enableBat ? "true" : "false");

        server.sendContent("}");
    });

    // ---------------------------------------------------------
    // POST /api/sdcard/config
    // ---------------------------------------------------------
    server.on("/api/sdcard/config", HTTP_POST, []() {
        if (!authOk()) return authFail();

        if (!server.hasArg("plain")) {
            server.send(400, "text/plain", "Missing body");
            return;
        }

        DynamicJsonDocument req(4096);
        if (deserializeJson(req, server.arg("plain"))) {
            server.send(400, "text/plain", "Invalid JSON");
            return;
        }

        // Validate and update config. "logPwr"/"logBat" store the selection;
        // "selectPwr"/"selectBat" are accepted as aliases for the same thing.
        // "enabled" is stored but never blocks an export.
        if (!req["enabled"].isNull()) sdCardConfig.enabled = req["enabled"].as<bool>();
        if (!req["logPwr"].isNull()) sdCardConfig.logPwr = req["logPwr"].as<bool>();
        if (!req["logBat"].isNull()) sdCardConfig.logBat = req["logBat"].as<bool>();
        if (!req["selectPwr"].isNull()) sdCardConfig.logPwr = req["selectPwr"].as<bool>();
        if (!req["selectBat"].isNull()) sdCardConfig.logBat = req["selectBat"].as<bool>();

        if (!req["recordsPerFile"].isNull()) {
            uint32_t val = req["recordsPerFile"].as<uint32_t>();
            if (val < 100) val = 100;
            if (val > 100000) val = 100000;
            sdCardConfig.recordsPerFile = val;
        }

        if (!req["exportTimeoutMs"].isNull()) {
            uint32_t val = req["exportTimeoutMs"].as<uint32_t>();
            if (val < 60000) val = 60000;
            if (val > 3600000) val = 3600000;
            sdCardConfig.exportTimeoutMs = val;
        }

        config.saveSdCardConfig();

        // Return updated config
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "application/json", "");
        server.sendContent("{");
        server.sendContent("\"enabled\":"); server.sendContent(sdCardConfig.enabled ? "true" : "false"); server.sendContent(",");
        server.sendContent("\"selectPwr\":"); server.sendContent(sdCardConfig.logPwr ? "true" : "false"); server.sendContent(",");
        server.sendContent("\"selectBat\":"); server.sendContent(sdCardConfig.logBat ? "true" : "false"); server.sendContent(",");
        server.sendContent("\"recordsPerFile\":"); server.sendContent(String(sdCardConfig.recordsPerFile)); server.sendContent(",");
        server.sendContent("\"exportTimeoutMs\":"); server.sendContent(String(sdCardConfig.exportTimeoutMs));
        server.sendContent(",\"sdReady\":"); server.sendContent(sdCard.isReady() ? "true" : "false");
        server.sendContent(",\"queryIntervalPwr\":"); server.sendContent(String(config.battery.intervalPwr / 1000));
        server.sendContent(",\"queryIntervalBat\":"); server.sendContent(String(config.battery.intervalBat / 1000));
        server.sendContent(",\"enableBat\":"); server.sendContent(config.battery.enableBat ? "true" : "false");
        server.sendContent("}");
    });

    // ---------------------------------------------------------
    // POST /api/sdcard/export/data
    //
    // One common Export for battery data. The two page checkboxes decide what
    // the session writes: {"pwr":true,"bat":true} covers both kinds in a single
    // session. The selection, the optional From/To window and the record
    // rotation travel with the start request and are persisted here, so the
    // page state survives a reload. The export timeout belongs to BMS exports
    // only and is never read or persisted by this endpoint.
    // ---------------------------------------------------------
    server.on("/api/sdcard/export/data", HTTP_POST, []() {
        if (!authOk()) return authFail();

        if (sdCard.isExporting()) {
            server.send(409, "text/plain", "Export already in progress");
            return;
        }

        if (!sdCard.isReady()) {
            server.send(400, "text/plain", "SD card not ready");
            return;
        }

        uint32_t timeFrom = 0, timeTo = 0;
        std::vector<int> modules;

        // The stored selection is the default; the request may override it.
        // These locals hold what the request asked for; sdCardConfig is not
        // mutated until every validation below has passed.
        bool wantPwr = sdCardConfig.logPwr;
        bool wantBat = sdCardConfig.logBat;
        bool havePwr = false, haveBat = false;

        // Kept in scope so the record rotation can be clamped/persisted via
        // applyDataExportSettings() only after validation.
        DynamicJsonDocument req(2048);
        bool haveBody = server.hasArg("plain");
        if (haveBody) {
            if (deserializeJson(req, server.arg("plain"))) {
                server.send(400, "text/plain", "Invalid JSON");
                return;
            }
            if (!req["timeFrom"].isNull()) timeFrom = req["timeFrom"].as<uint32_t>();
            if (!req["timeTo"].isNull()) timeTo = req["timeTo"].as<uint32_t>();

            havePwr = !req["pwr"].isNull() || !req["selectPwr"].isNull();
            haveBat = !req["bat"].isNull() || !req["selectBat"].isNull();
            if (havePwr) wantPwr = req["pwr"].isNull() ? req["selectPwr"].as<bool>() : req["pwr"].as<bool>();
            if (haveBat) wantBat = req["bat"].isNull() ? req["selectBat"].as<bool>() : req["bat"].as<bool>();

            JsonArray mods = req["modules"].as<JsonArray>();
            for (JsonVariant v : mods) {
                if (!v.isNull()) modules.push_back(v.as<int>());
            }
        }

        if (!wantPwr && !wantBat) {
            server.send(400, "text/plain", "Select Module data, Cells data or both before exporting");
            return;
        }
        if (wantBat && !config.battery.enableBat) {
            server.send(400, "text/plain", "Cells data must be enabled on the Cells page before exporting");
            return;
        }
        if (timeFrom > 0 && timeTo > 0 && timeFrom > timeTo) {
            server.send(400, "text/plain", "Start of the time range is after its end");
            return;
        }
        // A From/To window requires a valid clock; a To already in the past
        // would finish the export immediately.
        if ((timeFrom > 0 || timeTo > 0) && !config.isSystemTimeValid()) {
            server.send(400, "text/plain", "System time is not set; sync NTP before scheduling an export");
            return;
        }
        if (timeTo > 0 && config.isSystemTimeValid() && timeTo <= (uint32_t)time(nullptr)) {
            server.send(400, "text/plain", "End of the time range is in the past");
            return;
        }

        // All checks passed: only now persist the selection and the record
        // rotation, so a rejected request never mutates NVS.
        bool changed = false;
        if (havePwr || haveBat) {
            changed = (sdCardConfig.logPwr != wantPwr) || (sdCardConfig.logBat != wantBat);
            sdCardConfig.logPwr = wantPwr;
            sdCardConfig.logBat = wantBat;
        }
        // exportTimeoutMs from the body is deliberately ignored for data exports.
        if (haveBody && applyDataExportSettings(req)) changed = true;
        if (changed) config.saveSdCardConfig();

        if (sdCard.startDataExport(timeFrom, timeTo, modules, wantPwr, wantBat)) {
            server.send(202, "text/plain", "Data export started (" + sdCard.getKindsJson() + ")");
        } else {
            server.send(500, "text/plain", "Failed to start data export");
        }
    });

    // ---------------------------------------------------------
    // POST /api/sdcard/export/history
    // ---------------------------------------------------------
    server.on("/api/sdcard/export/history", HTTP_POST, []() {
        if (!authOk()) return authFail();

        if (sdCard.isExporting()) {
            server.send(409, "text/plain", "Export already in progress");
            return;
        }

        if (!sdCard.isReady()) {
            server.send(400, "text/plain", "SD card not ready");
            return;
        }

        int module = 0;
        // Kept in scope so the timeout is clamped/persisted only after the
        // module validation below has passed.
        DynamicJsonDocument req(1024);
        bool haveBody = server.hasArg("plain");
        if (haveBody) {
            if (deserializeJson(req, server.arg("plain"))) {
                server.send(400, "text/plain", "Invalid JSON");
                return;
            }
            if (!req["module"].isNull()) module = req["module"].as<int>();
        }

        if (module <= 0) {
            server.send(400, "text/plain", "Module number required");
            return;
        }

        // Save Config is gone: this start request persists the BMS History
        // export timeout. Written before the start call, which reads it.
        if (haveBody && applyBmsExportSettings(req)) config.saveSdCardConfig();

        if (sdCard.startHistoryExport(module)) {
            server.send(202, "text/plain", "History export started");
        } else {
            server.send(500, "text/plain", "Failed to start history export");
        }
    });

    // ---------------------------------------------------------
    // POST /api/sdcard/export/stop
    // ---------------------------------------------------------
    server.on("/api/sdcard/export/stop", HTTP_POST, []() {
        if (!authOk()) return authFail();

        sdCard.stopExport();
        server.send(200, "text/plain", "Export stopped");
    });

    // ---------------------------------------------------------
    // GET /api/sdcard/export/status
    // ---------------------------------------------------------
    server.on("/api/sdcard/export/status", HTTP_GET, []() {
        if (!authOk()) return authFail();

        String statusJson = sdCard.getExportStatusJson();
        // Room for every key plus the "kinds" array, with ArduinoJson's
        // overhead on top of the ~470 byte payload.
        DynamicJsonDocument doc(4096);
        if (deserializeJson(doc, statusJson)) {
            server.send(500, "text/plain", "Failed to build export status");
            return;
        }

        // Add additional UI-relevant fields
        bool isExporting = sdCard.isExporting();
        doc["buttonText"] = isExporting ? "Stop export" : "Export";
        doc["optionsLocked"] = isExporting;
        doc["exportKind"] = (int)sdCard.getExportKind();
        // A data session can cover both kinds at once, so the page needs the
        // list, not a single value. getExportStatusJson() already emits "kinds"
        // as a real JSON array and deserializing keeps it an array - do not
        // overwrite it with a string here.
        doc["selectPwr"] = sdCardConfig.logPwr;
        doc["selectBat"] = sdCardConfig.logBat;

        // Query interval info for UI
        String intervalInfo = "Note: PWR/BAT exports use the query interval configured in Battery data / Cells pages (currently: PWR=" + String(config.battery.intervalPwr / 1000) + "s, BAT=" + String(config.battery.intervalBat / 1000) + "s)";
        doc["queryIntervalInfo"] = intervalInfo;

        // Bat parser requirement for BAT export
        doc["batParserRequired"] = !config.battery.enableBat;

        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "application/json", "");
        String out;
        serializeJson(doc, out);
        server.sendContent(out);
    });
}