#pragma once
#include <ArduinoJson.h>
#include "../wp_webserver.h"
#include "auth.h"
#include "../py_parser_stat.h"
#include "../config.h"
#include "../py_log.h"
#include "../py_scheduler.h"
#include "../py_parser_pwr.h"

static void handleApiStatValues();
static void handleApiStatSet();

static void registerStatAPI() {
    server.on("/api/stat/values", HTTP_GET, []() {
        if (!authOk()) return authFail();
        handleApiStatValues();
    });

    server.on("/api/stat/set", HTTP_POST, []() {
        if (!authOk()) return authFail();
        handleApiStatSet();
    });
}

static void handleApiStatValues() {
    const StatData& st = currentStatData();

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");

    server.sendContent("{");

    // CONFIG
    server.sendContent("\"config\":{");
    server.sendContent("\"intervalStat\":");
    server.sendContent(String(config.battery.intervalStat));
    server.sendContent(",");
    server.sendContent("\"enableStat\":");
    server.sendContent(config.battery.enableStat ? "true" : "false");
    server.sendContent("},");

    // MQTT
    server.sendContent("\"mqtt\":{");
    server.sendContent("\"topicStat\":\"");
    server.sendContent(config.mqtt.topicStat);
    server.sendContent("\"");
    server.sendContent("},");

    // HEADERS
    server.sendContent("\"headers\":[");
    for (size_t i = 0; i < st.fields.size(); i++) {
        if (i > 0) server.sendContent(",");
        server.sendContent("\"");
        server.sendContent(st.fields[i].name);
        server.sendContent("\"");
    }
    server.sendContent("],");

    // VALUES
    server.sendContent("\"values\":[");
    for (size_t i = 0; i < st.fields.size(); i++) {
        if (i > 0) server.sendContent(",");
        server.sendContent("\"");
        server.sendContent(st.fields[i].raw);
        server.sendContent("\"");
    }
    server.sendContent("],");

    // FIELDS (NVS fields only!)
    server.sendContent("\"fields\":[");

    bool first = true;
    for (auto &pf : st.fields) {

        // Return only NVS fields
        if (!config.battery.fieldsStat.count(pf.name)) {
            continue;
        }

        const FieldConfig &f = config.battery.fieldsStat.at(pf.name);

        DynamicJsonDocument doc(256);
        JsonObject o = doc.to<JsonObject>();

        o["name"]        = pf.name;
        o["display"]     = f.display;
        o["factor"]      = f.factor;
        o["unit"]        = f.unit;
        o["sendMQTT"]    = f.mqtt;
        o["sendPayload"] = f.send;
        o["raw"]         = pf.raw;
        o["value"]       = pf.raw;

        String tmp;
        serializeJson(o, tmp);

        if (!first) server.sendContent(",");
        first = false;

        server.sendContent(tmp);
    }

    server.sendContent("]");

    server.sendContent("}");
}

static void applyStatFieldsText(const String& text) {
    const int len = text.length();
    int pos = 0;

    while (pos < len) {
        int nl = text.indexOf('\n', pos);
        String line = (nl < 0) ? text.substring(pos) : text.substring(pos, nl);
        pos = (nl < 0) ? len : nl + 1;
        line.trim();
        if (line.length() == 0) continue;

        // name|display|factor|unit|mqtt
        int p1 = line.indexOf('|');
        int p2 = (p1 < 0) ? -1 : line.indexOf('|', p1 + 1);
        int p3 = (p2 < 0) ? -1 : line.indexOf('|', p2 + 1);
        int p4 = (p3 < 0) ? -1 : line.indexOf('|', p3 + 1);
        if (p1 < 0 || p2 < 0 || p3 < 0 || p4 < 0) continue;

        String name    = line.substring(0, p1);
        String display = line.substring(p1 + 1, p2);
        String factor  = line.substring(p2 + 1, p3);
        String unit    = line.substring(p3 + 1, p4);
        bool   mqtt    = line.substring(p4 + 1).toInt() != 0;

        if (name.length() == 0) continue;

        if (!config.battery.fieldsStat.count(name)) {
            FieldConfig fc;
            fc.label   = name;
            fc.display = name;
            fc.factor  = "1";
            fc.unit    = "";
            fc.mqtt    = false;
            fc.send    = false;
            config.battery.fieldsStat[name] = fc;
        }

        FieldConfig &fc = config.battery.fieldsStat[name];
        fc.display = display;
        fc.label   = display;
        fc.factor  = factor;
        fc.unit    = unit;
        fc.mqtt    = mqtt;
        // fc.send is intentionally preserved: the Statistics page has no Send control.
    }
}

static void handleApiStatSet() {

    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Missing body");
        return;
    }

    String body = server.arg("plain");

    // New wire format: a small JSON header on the first line, then one packed
    // "name|display|factor|unit|mqtt" record per line. Keeping the 51-field table
    // out of the JSON tree is what lets this fit in the fragmented heap.
    int nl = body.indexOf('\n');
    String header     = (nl < 0) ? body : body.substring(0, nl);
    String fieldsText = (nl < 0) ? String() : body.substring(nl + 1);

    // A stale cached page still sends the old single-object body (fields as a JSON
    // array). Detect it and ask for a reload instead of attempting a huge parse.
    if (nl < 0 && body.indexOf("\"fields\"") >= 0) {
        server.send(400, "text/plain", "Stale Statistics page: reload the UI (Ctrl+F5)");
        return;
    }

    JsonDocument req;
    DeserializationError err = deserializeJson(req, header);
    if (err) {
        Log(LOG_ERROR, String("STAT set: parse failed: ") + err.c_str() +
                       " (header " + String(header.length()) +
                       " B, body " + String(body.length()) + " B)");
        server.send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
        return;
    }

    // CONFIG
    config.battery.intervalStat = req["config"]["intervalStat"] | config.battery.intervalStat;
    config.battery.enableStat   = req["config"]["enableStat"]   | config.battery.enableStat;

    // MQTT
    config.mqtt.topicStat = req["mqtt"]["topicStat"] | config.mqtt.topicStat;

    // FIELDS (parsed from the packed text, no JSON document)
    applyStatFieldsText(fieldsText);

    discoveryStatNeeded = true;

    config.save();

    // Fetch a STAT frame now when the parser is enabled, so the Statistics page and
    // the dashboard SOH populate immediately instead of waiting for the periodic poll.
    if (config.battery.enableStat) {
        for (const auto& m : currentPwrModules()) {
            if (!m.present) continue;
            py_scheduler.enqueue("stat " + String(m.index));
        }
    }

    server.send(200, "text/plain", "STAT settings saved");
}
