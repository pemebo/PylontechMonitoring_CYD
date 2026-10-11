#pragma once
#include "api_core.h"
#include <ArduinoJson.h>
#include "../py_wifimanager.h"
#include "../py_mqtt.h"
#include "../py_display.h"
#include "../config.h"

extern PyMqtt py_mqtt;

// ---------------------------------------------------------
// WiFi API
// ---------------------------------------------------------
static void apiWifiGet() {
    WifiStatus s = WiFiManagerModule::getStatus();

    StaticJsonDocument<256> doc;
    doc["connected"] = s.connected;
    doc["ssid"]      = s.ssid;
    doc["rssi"]      = s.rssi;
    doc["ip"]        = s.ip;
    doc["mac"]       = s.mac;
    doc["enabled"]   = config.wifiEnabled;

    String out;
    serializeJson(doc, out);
    apiJson(out);
}

static void apiWifiPost() {
    if (!server.hasArg("plain"))
        return apiError(400, F("Missing body"));

    StaticJsonDocument<256> req;
    if (deserializeJson(req, server.arg("plain")))
        return apiError(400, F("Invalid JSON"));

    String ssid = req["ssid"] | "";
    String pass = req["pass"] | "";

    if (ssid.isEmpty())
        return apiError(400, F("SSID missing"));

    WiFiManagerModule::connect(ssid, pass);
    apiText(F("Connecting…"));
}

static void apiWifiScan() {
    apiJson(WiFiManagerModule::scanJson());
}

// Enable / disable the WiFi radio. Disabling persists the flag, sends the
// response (so it reaches the browser) and only then defers the radio stop.
static void apiWifiSetEnabled() {
    if (!server.hasArg("plain"))
        return apiError(400, F("Missing body"));

    StaticJsonDocument<128> req;
    if (deserializeJson(req, server.arg("plain")))
        return apiError(400, F("Invalid JSON"));

    bool en = req["enabled"] | false;

    if (en) {
        WiFiManagerModule::enable();
    } else {
        config.wifiEnabled = false;
        config.save();
    }

    StaticJsonDocument<128> doc;
    doc["ok"]      = true;
    doc["enabled"] = en;

    String out;
    serializeJson(doc, out);
    apiJson(out);

    // Trigger the deferred stop only after the response is queued.
    if (!en) {
        WiFiManagerModule::requestDisable();
    }
}

// ---------------------------------------------------------
// MQTT API
// ---------------------------------------------------------
static void apiMqttGet() {
    StaticJsonDocument<256> doc;

    doc["enabled"] = config.mqtt.enabled;
    doc["server"]  = config.mqtt.server;
    doc["port"]    = config.mqtt.port;
    doc["user"]    = config.mqtt.user;
    doc["topic"]   = config.mqtt.prefix;

    String out;
    serializeJson(doc, out);
    apiJson(out);
}

static void apiMqttPost() {
    if (!server.hasArg("plain"))
        return apiError(400, F("Missing body"));

    StaticJsonDocument<256> req;
    if (deserializeJson(req, server.arg("plain")))
        return apiError(400, F("Invalid JSON"));

    config.mqtt.enabled = req["enabled"] | false;
    config.mqtt.server  = req["server"]  | "";
    config.mqtt.port    = req["port"]    | 1883;
    config.mqtt.user    = req["user"]    | "";

    String pass = req["pass"] | "";
    if (!pass.isEmpty()) config.mqtt.pass = pass;

    config.mqtt.prefix = req["topic"] | "Pylontech";

    config.save();
    py_mqtt.begin();

    apiText(F("MQTT saved"));
}

// ---------------------------------------------------------
// TIME / NTP API
// ---------------------------------------------------------
static void apiTimeGet() {
    StaticJsonDocument<256> doc;

    doc["manual_mode"]     = config.manual_mode;
    doc["manual_date"]     = config.manual_date;
    doc["manual_time"]     = config.manual_time;

    doc["use_gateway_ntp"] = config.use_gateway_ntp;
    doc["manual_ntp"]      = config.manual_ntp;
    doc["server"]          = config.ntpServer;

    doc["timezone"]        = config.timezone;

    String out;
    serializeJson(doc, out);
    apiJson(out);
}

static void apiTimePost() {
    if (!server.hasArg("plain"))
        return apiError(400, F("Missing body"));

    StaticJsonDocument<256> req;
    if (deserializeJson(req, server.arg("plain")))
        return apiError(400, F("Invalid JSON"));

    config.manual_mode     = req["manual_mode"]     | false;
    config.manual_date     = req["manual_date"]     | "";
    config.manual_time     = req["manual_time"]     | "";

    config.use_gateway_ntp = req["use_gateway_ntp"] | false;
    config.manual_ntp      = req["manual_ntp"]      | true;
    config.ntpServer       = req["server"]          | "pool.ntp.org";

    config.timezone        = req["timezone"]        | "Africa/Casablanca";

    config.save();

    // Apply the manual clock immediately when manual mode is enabled and a
    // complete date + time were supplied (the UI sends YYYY-MM-DD / HH:MM).
    // Without this the clock stays unset and NTP is skipped in manual mode.
    if (config.manual_mode &&
        config.manual_date.length() > 0 &&
        config.manual_time.length() > 0) {
        int y, mo, d, h, mi;
        if (sscanf(config.manual_date.c_str(), "%d-%d-%d", &y, &mo, &d) == 3 &&
            sscanf(config.manual_time.c_str(), "%d:%d", &h, &mi) == 2) {
            WiFiManagerModule::setManualTime(y, mo, d, h, mi);
        }
    }

    apiText(F("Time saved"));
}

// ---------------------------------------------------------
// NETWORK API
// ---------------------------------------------------------
static void apiNetworkGet() {
    StaticJsonDocument<256> doc;

    doc["dhcp"] = !config.useStaticIP;
    doc["ip"]   = config.ipAddr;
    doc["mask"] = config.subnetMask;
    doc["gw"]   = config.gateway;
    doc["dns"]  = config.dns;

    String out;
    serializeJson(doc, out);
    apiJson(out);
}

static void apiNetworkPost() {
    if (!server.hasArg("plain"))
        return apiError(400, F("Missing body"));

    StaticJsonDocument<256> req;
    if (deserializeJson(req, server.arg("plain")))
        return apiError(400, F("Invalid JSON"));

    config.useStaticIP = !(req["dhcp"] | true);
    config.ipAddr      = req["ip"]   | "";
    config.subnetMask  = req["mask"] | "";
    config.gateway     = req["gw"]   | "";
    config.dns         = req["dns"]  | "";

    config.save();
    apiText(F("Network saved"));
}

// ---------------------------------------------------------
// PASSWORD API
// ---------------------------------------------------------
static void apiPasswordPost() {
    if (!server.hasArg("plain"))
        return apiError(400, F("Missing body"));

    StaticJsonDocument<128> req;
    if (deserializeJson(req, server.arg("plain")))
        return apiError(400, F("Invalid JSON"));

    String pass = req["password"] | "";
    pass.trim();

    if (pass.length() == 0)
        return apiText(F("Password unchanged"));

    if (pass.length() > 20)
        return apiError(400, F("Max 20 characters"));

    for (size_t i = 0; i < pass.length(); i++) {
        if (!isalnum((unsigned char)pass[i]))
            return apiError(400, F("Only alphanumeric characters (A-Z a-z 0-9)"));
    }

    config.configPassword = pass;
    config.save();
    apiText(F("Password saved"));
}

// ---------------------------------------------------------
// AUTOMATIC REBOOT API
// ---------------------------------------------------------
static void apiAutorebootGet() {
    StaticJsonDocument<128> doc;

    char time[6];
    snprintf(time, sizeof(time), "%02u:%02u", config.rebootHour, config.rebootMin);

    doc["enabled"] = config.autoReboot;
    doc["time"]    = time;
    doc["days"]    = config.rebootEveryDays;

    String out;
    serializeJson(doc, out);
    apiJson(out);
}

static void apiAutorebootPost() {
    if (!server.hasArg("plain"))
        return apiError(400, F("Missing body"));

    StaticJsonDocument<256> req;
    if (deserializeJson(req, server.arg("plain")))
        return apiError(400, F("Invalid JSON"));

    String time = req["time"] | "";
    int   hour  = -1, min = -1;

    if (time.length() == 5 && time[2] == ':') {
        hour = time.substring(0, 2).toInt();
        min  = time.substring(3, 5).toInt();
    }

    if (hour < 0 || hour > 23 || min < 0 || min > 59)
        return apiError(400, F("Time must be in HH:MM format (00-23 / 00-59)"));

    int days = req["days"] | 1;
    if (days < 1 || days > 365)
        return apiError(400, F("Days must be between 1 and 365"));

    config.autoReboot      = req["enabled"] | false;
    config.rebootHour      = (uint8_t)hour;
    config.rebootMin       = (uint8_t)min;
    config.rebootEveryDays = (uint16_t)days;

    config.save();

    // Clear the schedule anchor so the next matching window reboots; the
    // interval is then counted from that first reboot.
    config.resetRebootAnchor();

    apiText(F("Automatic restart saved"));
}

// ---------------------------------------------------------
// LANGUAGE API
// ---------------------------------------------------------
static void apiLanguageGet() {
    StaticJsonDocument<64> doc;
    doc["language"] = config.language;

    String out;
    serializeJson(doc, out);
    apiJson(out);
}

static void apiLanguagePost() {
    if (!server.hasArg("plain"))
        return apiError(400, F("Missing body"));

    StaticJsonDocument<128> req;
    if (deserializeJson(req, server.arg("plain")))
        return apiError(400, F("Invalid JSON"));

    String lang = req["language"] | "";
    if (lang != "en" && lang != "es")
        return apiError(400, F("Unsupported language"));

    config.language = lang;
    config.save();
    display.setLanguage(lang);

    StaticJsonDocument<64> doc;
    doc["ok"]       = true;
    doc["language"] = lang;

    String out;
    serializeJson(doc, out);
    apiJson(out);
}
