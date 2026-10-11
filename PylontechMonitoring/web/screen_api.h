#pragma once
#include "api_core.h"
#include "auth.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include "../config.h"
#include "../py_display.h"

// WiFi "available" means connected to a network OR in AP mode. Only then may
// the screen be turned off (otherwise there would be no way to turn it back on).
inline bool screenWifiOk() {
    if (WiFi.status() == WL_CONNECTED) return true;
    wifi_mode_t mode = WiFi.getMode();
    return mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA;
}

inline void registerScreenAPI() {

    // ---------------------------------------------------------
    // GET /api/screen
    // ---------------------------------------------------------
    apiGetAuth("/api/screen", []() {

        StaticJsonDocument<512> doc;

        JsonObject cfg = doc.createNestedObject("config");
        cfg["enabled"]    = config.screen.enabled;
        cfg["touch"]      = config.screen.touch;
        cfg["brightness"] = config.screen.brightness;
        cfg["scheduled"]  = config.screen.scheduled;
        cfg["offHour"]    = config.screen.offHour;
        cfg["offMin"]     = config.screen.offMin;
        cfg["onHour"]     = config.screen.onHour;
        cfg["onMin"]      = config.screen.onMin;
        cfg["refreshSec"] = config.screen.refreshSec;
        cfg["darkMode"]   = config.screen.darkMode;
        cfg["autoOff"]    = config.screen.autoOff;
        cfg["autoOffSec"] = config.screen.autoOffSec;

        JsonObject st = doc.createNestedObject("state");
        st["screenOn"] = display.isScreenOn();
        st["wifiOk"]   = screenWifiOk();
        st["timeValid"] = config.isSystemTimeValid();

        String out;
        serializeJson(doc, out);
        apiJson(out);
    });

    // ---------------------------------------------------------
    // POST /api/screen
    // ---------------------------------------------------------
    apiPostAuth("/api/screen", []() {

        if (!server.hasArg("plain"))
            return apiError(400, F("Missing body"));

        StaticJsonDocument<512> req;
        if (deserializeJson(req, server.arg("plain")))
            return apiError(400, F("Invalid JSON"));

        bool wantEnabled = req["enabled"] | config.screen.enabled;
        if (!wantEnabled && !screenWifiOk())
            return apiError(400, F("Screen cannot be turned off while WiFi is unavailable"));

        bool enabledChanged = wantEnabled != config.screen.enabled;

        config.screen.enabled    = wantEnabled;
        config.screen.touch      = req["touch"]      | config.screen.touch;
        config.screen.brightness = (uint8_t)constrain(req["brightness"] | config.screen.brightness, 0, 100);
        config.screen.scheduled  = req["scheduled"]  | config.screen.scheduled;
        config.screen.offHour    = (uint8_t)constrain(req["offHour"] | config.screen.offHour, 0, 23);
        config.screen.offMin     = (uint8_t)constrain(req["offMin"]  | config.screen.offMin, 0, 59);
        config.screen.onHour     = (uint8_t)constrain(req["onHour"]  | config.screen.onHour, 0, 23);
        config.screen.onMin      = (uint8_t)constrain(req["onMin"]   | config.screen.onMin, 0, 59);
        config.screen.refreshSec = (uint8_t)constrain(req["refreshSec"] | config.screen.refreshSec, 0, 255);
        config.screen.darkMode   = req["darkMode"] | config.screen.darkMode;
        config.screen.autoOff    = req["autoOff"] | config.screen.autoOff;
        config.screen.autoOffSec = (uint16_t)constrain(req["autoOffSec"] | config.screen.autoOffSec, 1, 9999);

        // Schedule and Auto power-off are mutually exclusive (the web UI
        // enforces it; a raw API request could still set both). On conflict,
        // Schedule wins.
        if (config.screen.scheduled && config.screen.autoOff)
            config.screen.autoOff = false;

        config.save();

        // Apply live. Brightness always takes effect (respects a runtime-off
        // screen); the screen is only force-switched when the user actually
        // toggled "enabled" — a plain save must not wake the screen over a
        // power-timer, nor leave it dark after re-enabling.
        display.setBrightness(config.screen.brightness);
        display.setTouchEnabled(config.screen.touch);
        display.applyScreenConfig(config.screen);
        if (enabledChanged)
            display.setScreenOn(config.screen.enabled);

        apiText(F("Screen config saved"));
    });
}