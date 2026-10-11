#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <time.h>

#include "py_wifimanager.h"
#include "config.h"
#include "py_log.h"

using namespace WiFiManagerModule;

// ----------------------------------------------------
// Internal state
// ----------------------------------------------------
static Preferences prefs;

static bool apActive          = false;
static bool staConnecting     = false;
static unsigned long staStartTime = 0;

static unsigned long apDisableTimer    = 0;
static unsigned long apDisableDuration = 0;

static unsigned long staLostTime = 0;
static unsigned long lastRetry   = 0;

static bool otaStarted       = false;

// Radio enable state + deferred disable (disable must run after the HTTP
// response has been flushed, so it is pended and executed in loop()).
static bool enabled             = true;
static bool pendingDisable      = false;
static unsigned long pendingDisableAt = 0;

// Delay between requestDisable() and the actual radio shutdown. Long enough
// for the HTTP response to reach the browser before the AP/STA disappears.
static const unsigned long DISABLE_DELAY_MS = 700;

// NTP / time handling
static bool ntpInitialSynced     = false;
static unsigned long lastNtpResyncMillis = 0;

// Periodic AP self-check
static unsigned long lastAPCheck = 0;

// ----------------------------------------------------
// Helpers
// ----------------------------------------------------

// Check if STA has a valid IP
static bool hasValidIP() {
    return WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

// Start Access Point
static void startAP() {
    if (!enabled) return;
    if (apActive) return;

    Log(LOG_INFO, "WiFiManager: starting AP");
    WiFi.softAP(config.apSSID.c_str(), config.apPass.c_str());
    apActive = true;

    Log(LOG_INFO, "WiFiManager: AP IP = " + WiFi.softAPIP().toString());
}

// Stop Access Point
static void stopAP() {
    if (!apActive) return;

    Log(LOG_INFO, "WiFiManager: stopping AP");
    WiFi.softAPdisconnect(false);
    apActive = false;
}

// Start STA with given credentials
static void startSTA(const String& ssid, const String& pass) {
    if (!enabled) return;
    Log(LOG_INFO, "WiFiManager: STA connecting to " + ssid);
    staConnecting = true;
    staStartTime  = millis();
    WiFi.begin(ssid.c_str(), pass.c_str());
}

// Load STA credentials from NVS or config and start STA
static void startSTAFromPrefsOrSecrets() {
    if (!enabled) return;

    prefs.begin("wifi", true);
    String ssid = prefs.getString("ssid", config.wifiSSID);
    String pass = prefs.getString("pass", config.wifiPass);
    prefs.end();

    if (ssid.length() == 0 || pass.length() < 8) {
        Log(LOG_WARN, "WiFiManager: no valid credentials → AP only");
        startAP();
        staConnecting = false;
        return;
    }

    startSTA(ssid, pass);
}

// Fully stop the radio (AP + STA). Called by the deferred disable path.
static void stopRadio() {
    Log(LOG_WARN, "WiFiManager: disabling WiFi radio");

    // Tear down OTA + mDNS before the radio goes off, otherwise their UDP
    // listeners/sockets survive the radio-off and a later re-enable would
    // restart on top of stale state.
    if (otaStarted) {
        ArduinoOTA.end();
    }
    MDNS.end();

    WiFi.disconnect(true, true);   // disconnect STA + turn radio off
    WiFi.softAPdisconnect(true);   // stop AP
    WiFi.mode(WIFI_OFF);

    apActive      = false;
    staConnecting = false;
    enabled       = false;

    // Clear stale state so a later re-enable starts clean.
    apDisableDuration = 0;
    apDisableTimer    = 0;
    staLostTime       = 0;
    otaStarted        = false;   // loop() re-runs ArduinoOTA.begin() after re-enable
}


WifiStatus WiFiManagerModule::getStatus() {
    WifiStatus s;

    bool sta = (WiFi.status() == WL_CONNECTED && hasValidIP());

    if (sta) {
        s.mode = "STA";
        s.connected = true;
    } else if (apActive) {
        s.mode = "AP";
        s.connected = false;
    } else {
        s.mode = "none";
        s.connected = false;
    }

    s.ssid = WiFi.SSID();
    s.rssi = WiFi.RSSI();
    s.ip   = WiFi.localIP().toString();
    s.mac  = WiFi.macAddress();

    return s;
}

// Apply timezone using manual offset calculation (handles DST)
static void applyTimezoneFromConfig() {
    String tzString = findPosixForTimezone(config.timezone);
    setenv("TZ", tzString.c_str(), 1);
    tzset();

    // Calculate DST offset and adjust time
    int dstOffset = config.getDSTOffsetForTimezone();
    Log(LOG_INFO, "WiFiManager: timezone set to " + config.timezone +
                  " (TZ=" + tzString + ", DST offset=" + String(dstOffset) + ")");
}


// Trigger NTP sync via configTime (non-blocking)
static void triggerNtpSync() {

    // Manual time mode → no NTP
    if (config.manual_mode) {
        Log(LOG_INFO, "WiFiManager: manual time mode → skipping NTP sync");
        return;
    }

    // Gateway as NTP server
    if (config.use_gateway_ntp) {
        IPAddress gw = WiFi.gatewayIP();
        if (gw != IPAddress(0,0,0,0)) {
            config.ntpServer = gw.toString();
            Log(LOG_INFO, "WiFiManager: using gateway as NTP server: " + config.ntpServer);
        } else {
            Log(LOG_WARN, "WiFiManager: gateway not available → fallback to pool.ntp.org");
            config.ntpServer = "pool.ntp.org";
        }
    }

    // Manual NTP server
    else if (config.manual_ntp) {
        Log(LOG_INFO, "WiFiManager: using manual NTP server: " + config.ntpServer);
    }

    // Default NTP server
    else {
        config.ntpServer = "pool.ntp.org";
        Log(LOG_INFO, "WiFiManager: using default NTP server: pool.ntp.org");
    }

    // Get NTP time FIRST, then apply timezone
    configTime(0, 0, config.ntpServer.c_str());
    applyTimezoneFromConfig();
    lastNtpResyncMillis = millis();

    Log(LOG_INFO, "WiFiManager: NTP sync requested, server=" + config.ntpServer);
}

// Handle NTP initial sync + periodic resync
static void handleNtpLogic() {
    // Initial sync: wait until system time becomes valid
    if (!ntpInitialSynced && config.isSystemTimeValid()) {
        ntpInitialSynced = true;

        // Now apply TZ first
        applyTimezoneFromConfig();

        // Fetch time again (now with DST)
        String t = config.getCurrentTimeString();
        config.currentTime = t;
        config.save();

        Log(LOG_INFO, "WiFiManager: NTP initial sync → " + t);
    }

    // Periodic resync every ntpResyncInterval seconds
    if (ntpInitialSynced &&
        (millis() - lastNtpResyncMillis > config.ntpResyncInterval * 1000UL)) {

        Log(LOG_INFO, "WiFiManager: NTP periodic resync");
        triggerNtpSync();
    }
}

// ----------------------------------------------------
// Public API
// ----------------------------------------------------
void WiFiManagerModule::begin() {
    // Radio disabled in config: leave WiFi off and stay inert until a Boot
    // button gesture or the web toggle re-enables it.
    if (!config.wifiEnabled) {
        Log(LOG_INFO, "WiFiManager: WiFi disabled in config → radio off");
        WiFi.mode(WIFI_OFF);
        apActive      = false;
        staConnecting = false;
        enabled       = false;
        return;
    }

    Log(LOG_INFO, "WiFiManager: init dual mode (WIFI_AP_STA)");
    enabled = true;
    WiFi.mode(WIFI_AP_STA);
    delay(100);

    // Make sure AP is OFF at start, we control it manually
    WiFi.softAPdisconnect(true);
    apActive = false;
    Log(LOG_INFO, "WiFiManager: forced AP OFF at startup");

    // Start STA from stored credentials or config
    startSTAFromPrefsOrSecrets();
}

void WiFiManagerModule::loop() {

    // ----------------------------------------------------
    // Deferred disable: the HTTP response must reach the
    // browser before the radio is turned off.
    // ----------------------------------------------------
    if (pendingDisable && millis() - pendingDisableAt >= DISABLE_DELAY_MS) {
        pendingDisable = false;
        stopRadio();
    }

    // Radio off → none of the auto-revival paths may run.
    if (!enabled) return;

    // ----------------------------------------------------
    // Monitor STA connection attempts
    // ----------------------------------------------------
    if (staConnecting) {
        wl_status_t st = WiFi.status();

        if (st == WL_CONNECTED && hasValidIP()) {
            Log(LOG_INFO, "WiFiManager: STA connected, IP=" + WiFi.localIP().toString());
            staConnecting = false;
            staLostTime   = 0;

            // If AP is active, schedule shutdown in 90s
            if (apActive) {
                apDisableTimer    = millis();
                apDisableDuration = 90000;
                Log(LOG_INFO, "WiFiManager: AP will stop in 90s (STA connected)");
            }

            // Start OTA once
            if (!otaStarted) {
                ArduinoOTA.setHostname(config.hostname.c_str());
                if (MDNS.begin(config.hostname.c_str())) {
                    Log(LOG_INFO, "WiFiManager: mDNS started: " + config.hostname);
                } else {
                    Log(LOG_WARN, "WiFiManager: mDNS start failed");
                }
                ArduinoOTA.begin();
                otaStarted = true;
                Log(LOG_INFO, "WiFiManager: OTA ready");
            }

            // Trigger NTP sync on successful STA connect
            triggerNtpSync();
        }

        // Connection timeout → AP fallback
        if (millis() - staStartTime > 15000 && st != WL_CONNECTED) {
            Log(LOG_WARN, "WiFiManager: STA connect timeout → AP fallback");
            staConnecting = false;
            startAP();
        }
    }

    // ----------------------------------------------------
    // STA lost → AP after 15s, periodic retry
    // ----------------------------------------------------
    if (!staConnecting && WiFi.status() != WL_CONNECTED) {

        if (staLostTime == 0)
            staLostTime = millis();

        // After 15s without STA → AP fallback
        if (!apActive && millis() - staLostTime > 15000) {
            Log(LOG_WARN, "WiFiManager: STA lost >15s → AP fallback");
            startAP();
            apDisableDuration = 0; // keep AP until STA is back
        }

        // Retry STA every 30s
        if (millis() - lastRetry > 30000) {
            lastRetry = millis();
            Log(LOG_INFO, "WiFiManager: retry STA");
            startSTAFromPrefsOrSecrets();
        }
    }

    // ----------------------------------------------------
    // AP auto-disable after timer
    // ----------------------------------------------------
    if (apActive && apDisableDuration > 0) {
        if (millis() - apDisableTimer > apDisableDuration) {
            Log(LOG_INFO, "WiFiManager: AP timer expired → stop AP");
            stopAP();
            apDisableDuration = 0;
        }
    }

    // ----------------------------------------------------
    // AP self-check every 5 minutes
    // ----------------------------------------------------
    if (millis() - lastAPCheck > 300000) { // 5 minutes
        lastAPCheck = millis();

        bool staConnected = (WiFi.status() == WL_CONNECTED && hasValidIP());

        // If STA is connected and AP is still on → schedule shutdown in 90s
        if (staConnected && apActive) {
            apDisableTimer    = millis();
            apDisableDuration = 90000;
            Log(LOG_INFO, "WiFiManager: AP scheduled to stop in 90s (periodic check)");
        }

        // If STA is not connected and AP is off → turn AP on
        if (!staConnected && !apActive) {
            Log(LOG_WARN, "WiFiManager: STA not connected → AP ON (periodic check)");
            startAP();
        }
    }

    // ----------------------------------------------------
    // OTA + NTP handling
    // ----------------------------------------------------
    if (WiFi.status() == WL_CONNECTED) {
        if (otaStarted) {
            ArduinoOTA.handle();
        }
        handleNtpLogic();
    }
}

// ----------------------------------------------------
// Set WLAN credentials and connect
// ----------------------------------------------------
void WiFiManagerModule::connect(const String& ssid, const String& pass) {

    if (ssid.length() == 0 || pass.length() < 8) {
        Log(LOG_WARN, "WiFiManager: invalid credentials → abort");
        return;
    }

    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();

    Log(LOG_INFO, "WiFiManager: new credentials saved: " + ssid);

    startSTA(ssid, pass);
}

// ----------------------------------------------------
// WiFi reset (clear credentials, AP only)
// ----------------------------------------------------
void WiFiManagerModule::resetWiFi() {
    prefs.begin("wifi", false);
    prefs.clear();
    prefs.end();

    Log(LOG_WARN, "WiFiManager: WiFi reset → AP only");

    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP_STA);
    delay(100);

    apDisableDuration = 0;
    startAP();
}

// ----------------------------------------------------
// Start temporary AP for given duration
// ----------------------------------------------------
void WiFiManagerModule::startTemporaryAP(unsigned long durationMs) {
    Log(LOG_INFO, "WiFiManager: temporary AP for " + String(durationMs) + " ms");

    startAP();
    apDisableTimer    = millis();
    apDisableDuration = durationMs;
}

// ----------------------------------------------------
// Defer the radio shutdown (HTTP response must flush first)
// ----------------------------------------------------
void WiFiManagerModule::requestDisable() {
    Log(LOG_INFO, "WiFiManager: disable requested (deferred)");
    pendingDisable   = true;
    pendingDisableAt = millis();
}

// ----------------------------------------------------
// Enable WiFi (no-op when already on). Disabling is a separate deferred
// path: the web API persists the flag, flushes the response, then calls
// requestDisable().
// ----------------------------------------------------
void WiFiManagerModule::enable() {
    // Cancel any armed deferred shutdown: re-enabling during the pending
    // window must win, otherwise the stop would fire and kill the radio.
    pendingDisable = false;

    if (!enabled) {
        Log(LOG_INFO, "WiFiManager: enabling WiFi");
        WiFi.mode(WIFI_AP_STA);
        enabled = true;
        startSTAFromPrefsOrSecrets();
    }

    // Always converge the persisted flag, even if the radio was still on
    // during the pending window (so the UI state is restored).
    if (!config.wifiEnabled) {
        config.wifiEnabled = true;
        config.save();
    }
}

// ----------------------------------------------------
// WiFi scan result as JSON
// ----------------------------------------------------
String WiFiManagerModule::scanJson() {
    int n = WiFi.scanNetworks();
    DynamicJsonDocument doc(1024);
    JsonArray arr = doc.createNestedArray("nets");

    for (int i = 0; i < n; i++) {
        JsonObject o = arr.createNestedObject();
        o["ssid"] = WiFi.SSID(i);
        o["rssi"] = WiFi.RSSI(i);
        o["enc"]  = WiFi.encryptionType(i);
    }

    String out;
    serializeJson(doc, out);
    return out;
}

void WiFiManagerModule::setManualTime(int year, int month, int day, int hour, int minute) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = minute;
    t.tm_sec  = 0;
    // Let the configured timezone rules decide the DST offset; the wall-clock
    // hour is used exactly as entered.
    t.tm_isdst = -1;

    time_t ts = mktime(&t);
    struct timeval now = { .tv_sec = ts, .tv_usec = 0 };
    settimeofday(&now, nullptr);

    ntpInitialSynced = true;

    applyTimezoneFromConfig();

    String cur = config.getCurrentTimeString();
    config.currentTime = cur;
    config.save();

    Log(LOG_WARN, "WiFiManager: manual time set → " + cur);
}