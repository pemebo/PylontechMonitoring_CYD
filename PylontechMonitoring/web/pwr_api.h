#pragma once
#include <ArduinoJson.h>
#include "../wp_webserver.h"
#include "auth.h"
#include "../py_parser_pwr.h"
#include "../config.h"

static void handleApiPwrBase();
static void handleApiPwrSet();

static void registerPwrAPI() {
    server.on("/api/pwr/base", HTTP_GET, []() {
        if (!authOk()) return authFail();
        handleApiPwrBase();
    });

    server.on("/api/pwr/set", HTTP_POST, []() {
        if (!authOk()) return authFail();
        handleApiPwrSet();
    });
}

static void handleApiPwrBase() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");

    server.sendContent("{");

    // ---------------------------------------------------------
    // CONFIG BLOCK
    // ---------------------------------------------------------
    server.sendContent("\"config\":{");

    server.sendContent("\"intervalPwr\":");
    server.sendContent(String(config.battery.intervalPwr));
    server.sendContent(",");

    server.sendContent("\"useFahrenheit\":");
    server.sendContent(config.battery.useFahrenheit ? "true" : "false");

    server.sendContent("},");

    // ---------------------------------------------------------
    // MQTT BLOCK
    // ---------------------------------------------------------
    server.sendContent("\"mqtt\":{");

    server.sendContent("\"topicStack\":\"");
    server.sendContent(config.mqtt.topicStack);
    server.sendContent("\",");

    server.sendContent("\"topicPwr\":\"");
    server.sendContent(config.mqtt.topicPwr);
    server.sendContent("\"");

    server.sendContent("},");

    // ---------------------------------------------------------
    // HEADERS
    // ---------------------------------------------------------
    server.sendContent("\"headers\":[");
    for (size_t i = 0; i < lastParserHeader.size(); i++) {
        if (i > 0) server.sendContent(",");
        server.sendContent("\"");
        server.sendContent(lastParserHeader[i]);
        server.sendContent("\"");
    }
    server.sendContent("],");

    // ---------------------------------------------------------
    // VALUES
    // ---------------------------------------------------------
    server.sendContent("\"values\":[");
    for (size_t i = 0; i < lastParserValues.size(); i++) {
        if (i > 0) server.sendContent(",");
        server.sendContent("\"");
        server.sendContent(lastParserValues[i]);
        server.sendContent("\"");
    }
    server.sendContent("],");

    // ---------------------------------------------------------
    // FIELDS (NUR NVS-FELDER!)
    // ---------------------------------------------------------
    server.sendContent("\"fields\":[");

    bool firstField = true;

    for (size_t i = 0; i < lastParserHeader.size(); i++) {

        const String &name = lastParserHeader[i];

        // Return only NVS fields
        if (!config.battery.fieldsPwr.count(name)) {
            continue;   // <--- IMPORTANT!
        }

        const FieldConfig &f = config.battery.fieldsPwr.at(name);

        String raw = "";
        if (i < lastParserValues.size()) raw = lastParserValues[i];

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

        if (!firstField) server.sendContent(",");
        firstField = false;

        server.sendContent(tmp);
    }

    server.sendContent("]");

    server.sendContent("}");
}

static void handleApiPwrSet() {

    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Missing body");
        return;
    }

    DynamicJsonDocument req(16384);
    if (deserializeJson(req, server.arg("plain"))) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    // CONFIG
    config.battery.intervalPwr = req["config"]["intervalPwr"] | config.battery.intervalPwr;
    config.battery.useFahrenheit = req["config"]["useFahrenheit"] | config.battery.useFahrenheit;

    // MQTT
    config.mqtt.topicStack = req["mqtt"]["topicStack"] | config.mqtt.topicStack;
    config.mqtt.topicPwr   = req["mqtt"]["topicPwr"]   | config.mqtt.topicPwr;

    // FIELDS
    JsonArray arr = req["fields"];
    for (JsonObject f : arr) {

        String name = f["name"] | "";
        if (name.length() == 0) continue;

        if (!config.battery.fieldsPwr.count(name)) {
            FieldConfig fc;
            fc.label   = name;
            fc.display = name;
            fc.factor  = "1";
            fc.unit    = "";
            fc.mqtt    = false;
            fc.send    = false;
            config.battery.fieldsPwr[name] = fc;
        }

        FieldConfig &fc = config.battery.fieldsPwr[name];

        fc.display = f["display"]     | fc.display;
        fc.label   = f["display"]     | fc.label;
        fc.factor  = f["factor"]      | fc.factor;
        fc.unit    = f["unit"]        | fc.unit;
        fc.mqtt    = f["sendMQTT"]    | false;
        fc.send    = f["sendPayload"] | false;
    }

    discoveryPwrNeeded = true;
    config.save();

    server.send(200, "text/plain", "PWR settings saved");
}
