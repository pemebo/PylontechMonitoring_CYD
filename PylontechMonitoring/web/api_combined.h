#pragma once
#include "api_core.h"
#include "auth.h"

#include <SPIFFS.h>
#include <WiFi.h>

#include "../py_mqtt.h"
#include "../py_parser_pwr.h"
#include "../py_parser_stat.h"
#include "../py_log.h"
#include "../py_systemmanager.h"
#include "../py_sdcard.h"
#include "../config.h"
#include "filemanager.h"

extern AppConfig config;
extern PyMqtt py_mqtt;
extern BatteryStack lastParsedStack;
extern HealthStatus health;
extern PySdCard sdCard;

// Helper function for chunked JSON
static inline void sendChunk(const String &s) {
    server.sendContent(s);
}

// ---------------------------------------------------------
// Volume routing shared by the file-manager endpoints.
//
// The rule is the one /fm/list already uses: "/sd" and "/sd/..." address the SD
// card, every other path addresses SPIFFS. The browser sends the file name in
// the "file" argument (data/pages_filemanager.html); a containing directory may
// additionally be given as "path".
// ---------------------------------------------------------
struct FmTarget {
    bool   onSd = false;
    String path;          // path inside the resolved volume, leading "/"
    bool   bare = false;  // name only, no directory given -> search both roots
};

static inline bool fmOnSdPath(const String &p) {
    return p == "/sd" || p.startsWith("/sd/");
}

static inline String fmStripSd(const String &p) {
    return (p == "/sd" || p == "/sd/") ? String("/") : p.substring(3);
}

// Resolve the requested file onto a volume. Returns false when the request is
// unusable (empty name, or a path that tries to escape its root).
static inline bool resolveFmTarget(const String &fileArg, const String &dirArg, FmTarget &out) {
    String p = fileArg;
    p.trim();
    if (p.length() == 0) return false;

    // A bare name plus a directory argument makes an absolute path.
    if (p.indexOf('/') < 0 && dirArg.length() > 0) {
        String dir = dirArg;
        if (!dir.startsWith("/")) dir = "/" + dir;
        if (!dir.endsWith("/")) dir += "/";
        p = dir + p;
    }
    if (!p.startsWith("/")) p = "/" + p;

    // Refuse traversal before either filesystem is touched.
    if (p.indexOf("..") >= 0) return false;

    out.bare = (p.indexOf('/', 1) < 0);
    if (fmOnSdPath(p)) {
        out.onSd = true;
        out.path = fmStripSd(p);
    } else {
        out.onSd = false;
        out.path = p;
    }
    return true;
}

// Content type for a downloaded file, so a CSV export opens in a spreadsheet
// instead of being saved as an opaque blob.
static inline const char *fmContentType(const String &name) {
    if (name.endsWith(".html") || name.endsWith(".htm")) return "text/html";
    if (name.endsWith(".js"))                            return "application/javascript";
    if (name.endsWith(".css"))                            return "text/css";
    if (name.endsWith(".json"))                           return "application/json";
    if (name.endsWith(".csv"))                            return "text/csv";
    if (name.endsWith(".txt"))                            return "text/plain";
    if (name.endsWith(".svg"))                            return "image/svg+xml";
    if (name.endsWith(".png"))                            return "image/png";
    if (name.endsWith(".jpg") || name.endsWith(".jpeg"))   return "image/jpeg";
    if (name.endsWith(".gif"))                            return "image/gif";
    if (name.endsWith(".ico"))                            return "image/x-icon";
    return "application/octet-stream";
}

// Strip directories and quotes from a name before it goes into a header value.
static inline String fmSafeFileName(const String &p) {
    int slash = p.lastIndexOf('/');
    String n = (slash >= 0) ? p.substring(slash + 1) : p;
    n.replace("\"", "");
    n.replace("\r", "");
    n.replace("\n", "");
    return n;
}

// Average of the per-module SOH values; "null" when no module has a valid one.
static inline String averageSoh() {
    float sum = 0;
    int count = 0;
    for (auto &m : currentPwrModules()) {
        if (!m.present) continue;
        const StatSummary &sm = statSummary[m.index];
        if (sm.soh > 0 && sm.soh <= 100) {
            sum += sm.soh;
            count++;
        }
    }
    if (count == 0) return "null";
    return String(sum / count, 1);
}

inline void registerCombinedAPI() {

    // ---------------------------------------------------------
    // 1) Dashboard API
    // ---------------------------------------------------------
    apiGet("/api/dashboard", []() {

        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, F("application/json"), "");
        sendChunk(F("{"));

        // WiFi
        sendChunk(F("\"wifi\":{"));
        sendChunk(F("\"mode\":\"STA\","));
        sendChunk("\"ssid\":\"" + WiFi.SSID() + "\",");
        sendChunk("\"ip\":\"" + WiFi.localIP().toString() + "\",");
        sendChunk("\"rssi\":" + String(WiFi.RSSI()));
        sendChunk(F("},"));

        // MQTT
        sendChunk(F("\"mqtt\":{"));
        sendChunk("\"connected\":" + String(py_mqtt.isConnected() ? "true" : "false") + ",");
        sendChunk("\"enabled\":" + String(config.mqtt.enabled ? "true" : "false") + ",");
        sendChunk("\"server\":\"" + config.mqtt.server + "\",");
        sendChunk("\"port\":" + String(config.mqtt.port) + ",");
        sendChunk("\"last_contact\":\"" + config.lastMqttContact + "\"");
        sendChunk(F("},"));

        // Battery
        sendChunk(F("\"battery\":{"));
        sendChunk("\"modules\":" + String(lastParsedStack.batteryCount) + ",");
        sendChunk("\"last_update\":\"" + config.lastPwrUpdate + "\",");
        sendChunk(F("\"overview\":{"));
        sendChunk("\"soc\":" + String(lastParsedStack.soc) + ",");
        sendChunk("\"status\":\"" +
            (lastParsedStack.status.length() > 0 ? lastParsedStack.status : String("Disconnected")) + "\",");
        sendChunk("\"voltage_mV\":" + String(lastParsedStack.avgVoltage_mV) + ",");
        sendChunk("\"current_mA\":" + String(lastParsedStack.totalCurrent_mA) + ",");
        // 64-bit product: mV × mA exceeds 2^31 above ~42 A at 50 V, which
        // wrapped to a negative wattage when computed in a 32-bit long.
        sendChunk("\"powerW\":" + String((long)((long long)lastParsedStack.avgVoltage_mV * lastParsedStack.totalCurrent_mA / 1000000LL)) + ",");
        sendChunk("\"temperature\":" + String(lastParsedStack.temperature));
        sendChunk(F("}"));
        sendChunk(F("},"));

        // Health
        sendChunk(F("\"health\":{"));
        sendChunk("\"status\":\"" + health.strongestMessage + "\",");
        sendChunk("\"color\":\"" + health.color + "\",");
        sendChunk("\"cellMin\":" + String(health.stackCellMin) + ",");
        sendChunk("\"cellMax\":" + String(health.stackCellMax) + ",");
        sendChunk("\"cellDiff\":" + String(health.stackCellDiff) + ",");
        // Millivolt precision: per-module deltas are usually a few mV, and the
        // default 2-decimal String() would truncate 1 mV to "0.00".
        sendChunk("\"maxCellDiff\":" + String(health.stackCellDiffMax, 3) + ",");
        sendChunk("\"soh\":" + averageSoh());
        sendChunk(",\"events\":" + String((int)health.events.size()));
        sendChunk(F("},"));

        // SD Card
        sdCard.refreshSpaceUsage();
        sendChunk(F("\"sdCard\":{"));
        if (sdCard.isReady()) {
            sendChunk(F("\"status\":\"ok\","));
            sendChunk("\"freeSpacePercent\":" + String(sdCard.getFreeSpacePercent()) + ",");
            sendChunk("\"totalBytes\":" + String(sdCard.getTotalBytes()) + ",");
            sendChunk("\"usedBytes\":" + String(sdCard.getUsedBytes()));
        } else {
            sendChunk(F("\"status\":\"unavailable\""));
        }
        sendChunk(F("},"));

        // System
        time_t now; time(&now);
        struct tm t; localtime_r(&now, &t);
        char buf[32]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);

        sendChunk(F("\"system\":{"));
        sendChunk("\"time\":\"" + String(buf) + "\",");
        sendChunk("\"uptime\":\"" + config.uptimeString() + "\",");
        sendChunk("\"version\":\"" + config.firmwareVersion + "\",");
        sendChunk(SystemManager::buildCpuJson());
        sendChunk(",");
        sendChunk(SystemManager::buildMemJson());
        sendChunk(F("}"));

        sendChunk(F("}"));
        sendChunk("");
    });

    // ---------------------------------------------------------
    // 2) Filemanager API
    // ---------------------------------------------------------
    apiGetAuth("/filemanager", []() {
        // If the layout exists in SPIFFS, serve it (the layout auto-loads the
        // Filemanager view with the sidebar). Otherwise fall back to the
        // standalone PROGMEM page (e.g. first provisioning with empty SPIFFS).
        if (SPIFFS.exists("/layout.html")) {
            File f = SPIFFS.open("/layout.html");
            if (f) {
                String html = f.readString();
                f.close();
                html.replace("__LANG__", config.language);
                server.send(200, F("text/html"), html);
                return;
            }
        }

        server.setContentLength(strlen_P(FILEMANAGER_PAGE));
        server.send(200, F("text/html"), "");
        server.sendContent_P(FILEMANAGER_PAGE);
        server.sendContent("");
    });

    // SD-aware directory listing. "/sd" and "/sd/..." list the SD card, every
    // other path lists SPIFFS. Kept here in the registered route rather than in
    // a separate, never-registered API module.
    apiGetAuth("/fm/list", []() {
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, F("application/json"), "");

        String path = server.hasArg("path") ? server.arg("path") : "/";

        if (path.length() == 0) path = "/";
        if (path != "/" && !path.startsWith("/")) path = "/" + path;

        bool onSd = (path == "/sd" || path.startsWith("/sd/"));
        String sdPath = (path == "/sd" || path == "/sd/") ? "/" : path.substring(3);

        if (onSd && !sdCard.isReady()) {
            server.sendContent("[]");
            server.sendContent("");
            return;
        }

        server.sendContent("[");

        File dir = onSd ? SD.open(sdPath) : SPIFFS.open(path);
        File entry = dir.openNextFile();
        bool first = true;

        while (entry) {
            if (!first) server.sendContent(",");
            first = false;

            String type = entry.isDirectory() ? "dir" : "file";
            long size = entry.isDirectory() ? 0 : entry.size();

            server.sendContent("{\"name\":\"");
            server.sendContent(entry.name());
            server.sendContent("\",\"size\":");
            server.sendContent(String(size));
            server.sendContent(",\"type\":\"");
            server.sendContent(type);
            server.sendContent("\"");

            if (type == "dir") {
                server.sendContent(",\"link\":\"/fm/list?path=");
                server.sendContent(path);
                if (!path.endsWith("/")) server.sendContent("/");
                server.sendContent(entry.name());
                server.sendContent("\"");
            }

            server.sendContent("}");

            entry = dir.openNextFile();
        }
        dir.close();

        server.sendContent("]");
        server.sendContent("");
    });

    // ---------------------------------------------------------
    // Authenticated file download, SD and SPIFFS.
    //
    // The body is streamed in 512-byte chunks with an unknown content length,
    // so peak memory does not grow with the file size - a multi-megabyte SD
    // export costs the same RAM as a 56-byte CSV.
    // ---------------------------------------------------------
    apiGetAuth("/fm/download", []() {
        if (!server.hasArg("file"))
            return apiError(400, F("Missing file"));

        String file = server.arg("file");
        String dir  = server.hasArg("path") ? server.arg("path") : "";

        FmTarget t;
        if (!resolveFmTarget(file, dir, t))
            return apiError(400, F("Invalid path"));

        // A bare name with no directory: look in SPIFFS first, then SD, so a
        // page served before the path was added still works.
        if (t.bare) {
            if (SPIFFS.exists(t.path)) {
                t.onSd = false;
            } else if (sdCard.isReady() && SD.exists(t.path)) {
                t.onSd = true;
            } else {
                return apiError(404, F("File not found"));
            }
        }

        if (t.onSd && !sdCard.isReady())
            return apiError(503, F("SD card not available"));

        File f = t.onSd ? SD.open(t.path.c_str(), FILE_READ)
                        : SPIFFS.open(t.path.c_str(), FILE_READ);
        if (!f || f.isDirectory()) {
            if (f) f.close();
            return apiError(404, F("File not found"));
        }

        String name = fmSafeFileName(t.path);

        // sendHeader() must be called before send(): the header block is
        // serialized from the queue inside _prepareHeader().
        server.sendHeader(F("Content-Disposition"),
                          String("attachment; filename=\"") + name + "\"");
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, fmContentType(name), "");

        uint8_t buf[512];
        size_t n;
        while ((n = f.read(buf, sizeof(buf))) > 0)
            server.sendContent((const char *)buf, n);

        // Zero-length chunk terminates the chunked body.
        server.sendContent("", 0);
        f.close();
    });

    // Delete a file from SD or SPIFFS, using the same volume rule as /fm/list.
    apiGetAuth("/fm/delete", []() {
        if (!server.hasArg("file"))
            return apiError(400, F("Missing file"));

        String file = server.arg("file");
        String dir  = server.hasArg("path") ? server.arg("path") : "";

        FmTarget t;
        if (!resolveFmTarget(file, dir, t))
            return apiError(400, F("Invalid path"));

        if (t.bare) {
            if (SPIFFS.exists(t.path)) {
                t.onSd = false;
            } else if (sdCard.isReady() && SD.exists(t.path)) {
                t.onSd = true;
            } else {
                return apiError(404, F("File not found"));
            }
        }

        if (t.onSd && !sdCard.isReady())
            return apiError(503, F("SD card not available"));

        if (t.onSd ? !SD.remove(t.path) : !SPIFFS.remove(t.path))
            return apiError(404, F("File not found"));

        apiText(F("OK"));
    });

    // Bulk-delete the files directly inside one directory, non-recursive:
    // sub-directories are left untouched. Uses the same volume rule as
    // /fm/list ("/sd" and "/sd/..." address the card, everything else SPIFFS).
    //
    // The names are collected before anything is removed: removing an entry
    // while an openNextFile() iteration is in flight can invalidate the
    // directory handle. Only files are collected; directories are skipped.
    apiGetAuth("/fm/deleteall", []() {
        bool   hasPath = server.hasArg("path");
        String path    = hasPath ? server.arg("path") : "/";
        path.trim();

        // An explicit but empty path is bad input; a missing one defaults to root.
        if (hasPath && path.length() == 0)
            return apiError(400, F("Invalid path"));
        if (path.length() == 0) path = "/";
        if (!path.startsWith("/")) path = "/" + path;

        // Refuse traversal before either filesystem is touched.
        if (path.indexOf("..") >= 0)
            return apiError(400, F("Invalid path"));

        bool   onSd = fmOnSdPath(path);
        String dir  = onSd ? fmStripSd(path) : path;

        if (onSd && !sdCard.isReady())
            return apiError(503, F("SD card not available"));

        File handle = onSd ? SD.open(dir) : SPIFFS.open(dir);
        if (!handle || !handle.isDirectory()) {
            if (handle) handle.close();
            return apiError(404, F("Directory not found"));
        }

        std::vector<String> names;
        File entry = handle.openNextFile();
        while (entry) {
            if (!entry.isDirectory()) {
                // File::name() is the basename in the ESP32 core; strip a
                // leading slash defensively so the join never doubles up.
                String name = entry.name();
                if (name.startsWith("/")) name = name.substring(1);
                if (name.length() > 0) {
                    String full = dir;
                    if (!full.endsWith("/")) full += "/";
                    full += name;
                    names.push_back(full);
                }
            }
            entry = handle.openNextFile();
        }
        handle.close();

        int deleted = 0;
        int failed  = 0;
        for (const String &full : names) {
            if (onSd ? SD.remove(full) : SPIFFS.remove(full)) deleted++;
            else                                             failed++;
        }

        apiJson(String("{\"deleted\":") + deleted + ",\"failed\":" + failed + "}");
    });

    // Upload to SD or SPIFFS, using the same volume rule as /fm/list.
    // The target directory is optional: without "path" the file lands in
    // SPIFFS root exactly as before; with "path=/sd..." it lands on the card.
    // fmUploadStatus carries an upload abort out of the sub-handler into the
    // final response, so the client learns why the upload failed instead of
    // always seeing "OK".
    static int fmUploadStatus = 200;
    server.on("/fm/upload", HTTP_POST,
        []() {
            if (!authOk()) return apiError(401, F("Unauthorized"));
            if (fmUploadStatus != 200) {
                const char *msg = (fmUploadStatus == 400) ? "Invalid path"
                                : (fmUploadStatus == 503) ? "SD card not available"
                                                          : "Write failed";
                return apiError(fmUploadStatus, msg);
            }
            apiText(F("OK"));
        },
        []() {
            HTTPUpload &u = server.upload();
            static File f;

            if (u.status == UPLOAD_FILE_START) {
                fmUploadStatus = 200;
                if (!authOk()) {
                    server.send(401, F("text/plain"), F("Unauthorized"));
                    return;
                }
                String dir = server.hasArg("path") ? server.arg("path") : "";
                FmTarget t;
                if (!resolveFmTarget(u.filename, dir, t)) { fmUploadStatus = 400; return; }
                if (t.onSd) {
                    if (!sdCard.isReady()) { fmUploadStatus = 503; return; }
                    f = SD.open(t.path.c_str(), FILE_WRITE);
                } else {
                    f = SPIFFS.open(t.path.c_str(), FILE_WRITE);
                }
                if (!f) fmUploadStatus = 500;
            }
            else if (u.status == UPLOAD_FILE_WRITE) {
                if (f) f.write(u.buf, u.currentSize);
            }
            else if (u.status == UPLOAD_FILE_END) {
                if (f) f.close();
            }
        }
    );

    // ---------------------------------------------------------
    // 3) Runtime API
    // ---------------------------------------------------------
    apiGet("/api/log", []() {
        // Reused snapshot: the WebServer writes the body after the handler
        // returns, so copy the live buffer under the log mutex into one
        // persistent String (no fresh 8KB allocation per poll).
        static String logSnapshot;
        WebLogWriteLock();
        logSnapshot = WebLogCStr();
        WebLogWriteUnlock();
        apiText(logSnapshot);
    });

    apiGet("/api/log/level", []() {
        StaticJsonDocument<128> doc;
        doc["info"]  = config.logInfo;
        doc["warn"]  = config.logWarn;
        doc["error"] = config.logError;
        doc["debug"] = config.logDebug;

        String out;
        serializeJson(doc, out);
        apiJson(out);
    });

    apiPostAuth("/api/log/level", []() {
        if (!server.hasArg("plain"))
            return apiError(400, F("Missing body"));

        StaticJsonDocument<128> req;
        if (deserializeJson(req, server.arg("plain")))
            return apiError(400, F("Invalid JSON"));

        config.logInfo  = req["info"]  | true;
        config.logWarn  = req["warn"]  | true;
        config.logError = req["error"] | true;
        config.logDebug = req["debug"] | false;

        config.save();
        apiText(F("Log level updated"));
    });
}
