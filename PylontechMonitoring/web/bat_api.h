#pragma once
#include <ArduinoJson.h>
#include "../wp_webserver.h"
#include "auth.h"
#include "../py_parser_bat.h"
#include "../config.h"
#include "../py_sdcard.h"

extern AppConfig config;
extern PySdCard sdCard;

// The BAT export issues one `bat N` burst per interval and every reply is a
// single module's 15 cells, so the SD/heap load scales with the module count.
// A conservative floor of 5 s per present module keeps the per-row free-space
// scan (a full FAT-table walk) from fragmenting the heap during an export.
static int presentModuleCount() {
    PwrBuffer* pwr = pwrUseA ? &pwrA : &pwrB;
    int n = 0;
    for (const auto& m : pwr->modules) {
        if (m.present) n++;
    }
    return n;
}

static unsigned long minBatIntervalMs() {
    int n = presentModuleCount();
    return (n > 0) ? (5000UL * (unsigned long)n) : 0UL;
}

static void handleApiBatCells();
static void handleApiBatSet();

static void registerBatAPI() {
    server.on("/api/bat/cells", HTTP_GET, []() {
        if (!authOk()) return authFail();
        handleApiBatCells();
    });

    server.on("/api/bat/set", HTTP_POST, []() {
        if (!authOk()) return authFail();
        handleApiBatSet();
    });
}

static void handleApiBatCells() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");

    server.sendContent("{");

    const std::vector<String> &hdr = currentBatHeader();
    const std::vector<BatData>  &cs  = currentBatCells();
    const std::vector<String>  *v0  = cs.empty() ? nullptr : &cs[0].values;

    // CONFIG
    server.sendContent("\"config\":{");
    server.sendContent("\"intervalBat\":");
    server.sendContent(String(config.battery.intervalBat));
    server.sendContent(",");
    server.sendContent("\"enableBat\":");
    server.sendContent(config.battery.enableBat ? "true" : "false");
    server.sendContent(",");
    server.sendContent("\"presentModules\":");
    server.sendContent(String(presentModuleCount()));
    server.sendContent(",");
    server.sendContent("\"minIntervalBatMs\":");
    server.sendContent(String(minBatIntervalMs()));
    server.sendContent("},");

    // MQTT
    server.sendContent("\"mqtt\":{");
    server.sendContent("\"topicBat\":\"");
    server.sendContent(config.mqtt.topicBat);
    server.sendContent("\",");
    server.sendContent("\"cellPrefix\":\"");
    server.sendContent(config.mqtt.cellPrefix);
    server.sendContent("\"");
    server.sendContent("},");

    // HEADERS
    server.sendContent("\"headers\":[");
    for (size_t i = 0; i < hdr.size(); i++) {
        if (i > 0) server.sendContent(",");
        server.sendContent("\"");
        server.sendContent(hdr[i]);
        server.sendContent("\"");
    }
    server.sendContent("],");

    // VALUES
    server.sendContent("\"values\":[");
    for (size_t i = 0; i < (v0 ? v0->size() : 0); i++) {
        if (i > 0) server.sendContent(",");
        server.sendContent("\"");
        server.sendContent((*v0)[i]);
        server.sendContent("\"");
    }
    server.sendContent("],");

    // FIELDS (NVS fields only!)
    server.sendContent("\"fields\":[");

    bool first = true;
    for (size_t i = 0; i < hdr.size(); i++) {
        const String &name = hdr[i];
        if (!config.battery.fieldsBat.count(name)) continue;
        const FieldConfig &f = config.battery.fieldsBat.at(name);

        String raw = (v0 && i < v0->size()) ? (*v0)[i] : String();

        DynamicJsonDocument doc(256);
        JsonObject o = doc.to<JsonObject>();

        o["name"]        = name;
        o["display"]     = f.display;
        o["factor"]      = f.factor;
        o["unit"]        = f.unit;
        o["sendMQTT"]    = f.mqtt;
        o["sendPayload"] = f.send;
        o["raw"]         = raw;
        o["value"]       = raw;

        String tmp;
        serializeJson(o, tmp);

        if (!first) server.sendContent(",");
        first = false;

        server.sendContent(tmp);
    }

    server.sendContent("]");

    server.sendContent("}");
}

static void handleApiBatSet() {

    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Missing body");
        return;
    }

    DynamicJsonDocument req(16384);
    if (deserializeJson(req, server.arg("plain"))) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    // Check if export is running and Bat parser is being disabled
    bool newEnableBat = req["config"]["enableBat"] | config.battery.enableBat;
    if (!newEnableBat && config.battery.enableBat) {
        // A running BAT export reads BAT frames only: dropping the parser would
        // leave it with a stale buffer and truncate the CSV files. A combined
        // data session counts too - the "kinds" list tells whether BAT is in it.
        if (sdCard.isExporting() && sdCard.getKindsJson().indexOf("\"bat\"") >= 0) {
            server.send(409, "text/plain", "Cannot disable the BAT parser while a BAT export is running");
            return;
        }
    }

    JsonVariant intervalValue = req["config"]["intervalBat"];
    if (!intervalValue.isNull()) {
        long intervalMs = intervalValue.as<long>();
        if (intervalMs < 10000) {
            server.send(400, "text/plain", "BAT query interval must be at least 10 seconds");
            return;
        }
    }

    // CONFIG
    config.battery.intervalBat = intervalValue | config.battery.intervalBat;
    config.battery.enableBat   = req["config"]["enableBat"]   | config.battery.enableBat;

    // MQTT
    config.mqtt.topicBat   = req["mqtt"]["topicBat"]   | config.mqtt.topicBat;
    config.mqtt.cellPrefix = req["mqtt"]["cellPrefix"] | config.mqtt.cellPrefix;

    // FIELDS
    JsonArray arr = req["fields"];
    for (JsonObject f : arr) {

        String name = f["name"] | "";
        if (name.length() == 0) continue;

        if (!config.battery.fieldsBat.count(name)) {
            FieldConfig fc;
            fc.label   = name;
            fc.display = name;
            fc.factor  = "1";
            fc.unit    = "";
            fc.mqtt    = false;
            fc.send    = false;
            config.battery.fieldsBat[name] = fc;
        }

        FieldConfig &fc = config.battery.fieldsBat[name];

        fc.display = f["display"]     | fc.display;
        fc.label   = f["display"]     | fc.label;
        fc.factor  = f["factor"]      | fc.factor;
        fc.unit    = f["unit"]        | fc.unit;
        fc.mqtt    = f["sendMQTT"]    | false;
        fc.send    = f["sendPayload"] | false;
    }

	discoveryBatNeeded  = true;

    config.save();

    String msg = "BAT settings saved";
    unsigned long minMs = minBatIntervalMs();
    if (minMs > 0 && config.battery.intervalBat < minMs) {
        msg += ". Warning: with " + String(presentModuleCount()) +
               " module(s) the BAT interval should be at least " +
               String(minMs / 1000) +
               " s (5 s per module); a shorter interval can fragment the heap during an SD export.";
    }
    server.send(200, "text/plain", msg);
}
