
#include "py_log.h"
#include <Arduino.h>
#include <Preferences.h>
#include "esp_log.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdio.h>

extern AppConfig config;

// Web log: fixed circular-in-place buffer (no heap, no fragmentation).
// Protected by a FreeRTOS mutex because Log() runs from both tasks.
static char webLogBuf[2048];
static size_t webLogLen = 0;
static SemaphoreHandle_t webLogMux = NULL;

void WebLogInit() {
    if (webLogMux == NULL) webLogMux = xSemaphoreCreateMutex();
}

// ---------------------------------------------------------
// Timestamp: YYYY.MM.DD hh:mm:ss,ms  (written into caller buffer)
// ---------------------------------------------------------
static void getTimestampWithMs(char* out, size_t sz) {
    time_t now;
    time(&now);

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    unsigned long ms = millis() % 1000;

    snprintf(
        out, sz,
        "%04d.%02d.%02d %02d:%02d:%02d,%03lu",
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec,
        ms
    );
}

// ---------------------------------------------------------
// Web-Log Buffer (static, bounded to 2047 chars, oldest dropped)
// ---------------------------------------------------------
void WebLog(const char* msg) {
    if (webLogMux) xSemaphoreTake(webLogMux, portMAX_DELAY);

    size_t add = strlen(msg);
    const size_t cap = sizeof(webLogBuf) - 1;

    // A line larger than the whole buffer: wipe and write a truncated line.
    if (add + 1 >= cap) {
        webLogLen = 0;
        add = cap - 1;
        memcpy(webLogBuf, msg, add);
        webLogBuf[add] = '\n';
        webLogBuf[add + 1] = '\0';
        webLogLen = add + 1;
        if (webLogMux) xSemaphoreGive(webLogMux);
        return;
    }

    // Drop the oldest bytes until the new line fits. 512-byte chunks keep the
    // ring meaningful in a 2 KB buffer: the old 2048 clamp equals the whole
    // buffer, which would wipe it on every append once full.
    while (webLogLen + add + 1 > cap) {
        size_t drop = (webLogLen + add + 1) - cap;
        if (drop < 512) drop = 512;
        if (drop >= webLogLen) { webLogLen = 0; break; }
        memmove(webLogBuf, webLogBuf + drop, webLogLen - drop);
        webLogLen -= drop;
    }

    memcpy(webLogBuf + webLogLen, msg, add);
    webLogLen += add;
    webLogBuf[webLogLen++] = '\n';
    webLogBuf[webLogLen] = '\0';

    if (webLogMux) xSemaphoreGive(webLogMux);
}

// Pointer to the live buffer; must be read while holding WebLogWriteLock().
const char* WebLogCStr() {
    return webLogBuf;
}

void WebLogWriteLock() {
    if (webLogMux) xSemaphoreTake(webLogMux, portMAX_DELAY);
}

void WebLogWriteUnlock() {
    if (webLogMux) xSemaphoreGive(webLogMux);
}

// ---------------------------------------------------------
// Main-Logfunction
// ---------------------------------------------------------
void Log(LogLevel lvl, const String& msg) {

    // --- Log-Level-Filter ---
    if ((lvl == LOG_INFO  && !config.logInfo)  ||
        (lvl == LOG_WARN  && !config.logWarn)  ||
        (lvl == LOG_ERROR && !config.logError) ||
        (lvl == LOG_DEBUG && !config.logDebug)) {
        return;
    }

    // --- Timestamp ---
    char ts[40];
    getTimestampWithMs(ts, sizeof(ts));

    // --- Prefix ---
    const char* prefix;
    switch (lvl) {
        case LOG_INFO:  prefix = "[INFO] ";  break;
        case LOG_WARN:  prefix = "[WARN] ";  break;
        case LOG_ERROR: prefix = "[ERROR] "; break;
        case LOG_DEBUG: prefix = "[DEBUG] "; break;
        default:        prefix = "";         break;
    }

    // --- Build the whole line on the stack (no heap churn per message) ---
    char line[640];
    snprintf(line, sizeof(line), "%s %s%s", ts, prefix, msg.c_str());

    // --- ESP_LOG ---
    switch (lvl) {
        case LOG_INFO:  ESP_LOGI("Pylontech", "%s", line); break;
        case LOG_WARN:  ESP_LOGW("Pylontech", "%s", line); break;
        case LOG_ERROR: ESP_LOGE("Pylontech", "%s", line); break;
        case LOG_DEBUG: ESP_LOGD("Pylontech", "%s", line); break;
    }

    // --- Web-Log ---
    WebLog(line);

    // --- Serial ---
    Serial.println(line);
}
