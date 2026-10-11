#include "config.h"
#include "py_log.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include <TimeLib.h>



AppConfig config;


static const size_t CHUNK_SIZE = 1500;

HealthStatus health;

// Per-module cell "today" extremes (web Health + TFT CELLS screen)
CellDayStats cellDayStats[MAX_MODULES + 1];

// SD Card configuration instance
AppConfig::SdCardConfig sdCardConfig;


// ----------------------------------------------------
//  Helper: Save JSON in chunks
// ----------------------------------------------------
void AppConfig::saveJsonChunked(const char* ns, const char* prefix, const String& json) {

    Log(LOG_INFO, String("NVS-CHUNK: Saving JSON for namespace '") + ns + "', prefix '" + prefix + "'");
    Log(LOG_INFO, String("NVS-CHUNK: JSON length = ") + json.length());

    Preferences p;
    p.begin(ns, false);

    // Alte Chunks entfernen
    for (int i = 0; i < 50; i++) {
        String key = String(prefix) + "_" + i;
        if (p.isKey(key.c_str())) {
            p.remove(key.c_str());
        }
    }

    int index = 0;
    int written = 0;

    size_t len = json.length();

    while (index * CHUNK_SIZE < len) {

        size_t start = index * CHUNK_SIZE;
        size_t end   = start + CHUNK_SIZE;
        if (end > len) end = len;

        String part;
        part.reserve(CHUNK_SIZE);
        for (size_t i = start; i < end; i++) {
            part += json[i];
        }

        String key = String(prefix) + "_" + index;

        bool ok = p.putString(key.c_str(), part);
        if (!ok) {
            Log(LOG_ERROR, String("NVS-CHUNK: FAILED writing chunk #") + index);
            break;
        }

        index++;
        written++;
    }

    Log(LOG_INFO, String("NVS-CHUNK: Total chunks written = ") + written);

    p.end();
}
// ----------------------------------------------------
//  Helper: Load JSON from chunks
// ----------------------------------------------------
String AppConfig::loadJsonChunked(const char* ns, const char* prefix) {
    Preferences p;
    p.begin(ns, true);

    String json = "";
    for (int i = 0; i < 50; i++) {
        String key = String(prefix) + "_" + String(i);
        if (!p.isKey(key.c_str())) break;
        json += p.getString(key.c_str(), "");
    }

    p.end();
    return json;
}

// ----------------------------------------------------
//  Generate hostname from MAC address
// ----------------------------------------------------
String AppConfig::generateHostname() {
    uint64_t mac = ESP.getEfuseMac();
    uint16_t last = mac & 0xFFFF;

    char buf[32];
    snprintf(buf, sizeof(buf), "pylontech-%04X", last);
    return String(buf);
}

// ----------------------------------------------------
//  Generate Time
// ----------------------------------------------------
// Fill timeinfo from the current system time. The timezone (including DST) is
// already applied by the POSIX TZ configured via setenv()/tzset(), so
// localtime_r() alone yields the correct local time. Returns false until the
// system time is valid.
static bool fillLocalTime(struct tm &timeinfo) {
    time_t now = time(nullptr);
    if (now < 1700000000) {
        return false; // No valid time yet
    }

    localtime_r(&now, &timeinfo);
    return true;
}

String AppConfig::getCurrentTimeString() {
    struct tm timeinfo;
    if (!fillLocalTime(timeinfo)) return "";

    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M", &timeinfo);

    return String(buf);
}

// Date + time, used by the Health event history (events span days)
String AppConfig::getCurrentDateTimeString() {
    struct tm timeinfo;
    if (!fillLocalTime(timeinfo)) return "";

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);

    return String(buf);
}

// YYYYMMDD of the local calendar day (DST-aware). -1 while time invalid.
int AppConfig::getCurrentDayKey() {
    struct tm timeinfo;
    if (!fillLocalTime(timeinfo)) return -1;

    return (timeinfo.tm_year + 1900) * 10000 + (timeinfo.tm_mon + 1) * 100 + timeinfo.tm_mday;
}

// Minutes since local midnight (DST-aware). -1 while time invalid.
int AppConfig::getCurrentMinutesOfDay() {
    struct tm timeinfo;
    if (!fillLocalTime(timeinfo)) return -1;

    return timeinfo.tm_hour * 60 + timeinfo.tm_min;
}

// Get DST offset for current timezone (Europe: last Sun Mar - last Sun Oct)
int AppConfig::getDSTOffsetForTimezone() {
    time_t now = time(nullptr);

    // Use TimeLib for accurate date extraction
    tmElements_t tm;
    breakTime(now, tm);

    int month = tm.Month;  // 1-12
    int day = tm.Day;
    int weekday = tm.Wday; // 0=Sunday, 1=Monday, etc.

    // Europe timezones: DST is last Sunday of March to last Sunday of October
    if (config.timezone.startsWith("Europe/")) {
        // Check if in DST period
        bool afterLastSunMarch = false;
        bool beforeLastSunOct = true;

        if (month >= 3 && month <= 4) {
            // Check if after last Sunday of March
            if (month == 3) {
                // Calculate last Sunday of March
                // March has 31 days. Last Sunday is when (31 - day) % 7 == 0
                int daysUntilLastSunday = (weekday + 6) % 7;
                int lastSunMarch = day + daysUntilLastSunday;
                if (day >= lastSunMarch) afterLastSunMarch = true;
            } else if (month == 4) {
                afterLastSunMarch = true;
            }
        }

        if (month >= 9 && month <= 10) {
            // Check if before last Sunday of October
            if (month == 9) {
                beforeLastSunOct = true;
            } else if (month == 10) {
                // Calculate last Sunday of October
                int daysUntilLastSunday = (weekday + 6) % 7;
                int lastSunOct = day + daysUntilLastSunday;
                if (day < lastSunOct) beforeLastSunOct = true;
            }
        }

        return afterLastSunMarch && beforeLastSunOct ? 3600 : 0;
    }

    return 0;
}

bool AppConfig::isSystemTimeValid() {
    time_t now;
    time(&now);
    return (now > 1700000000); // > 2023-01-01
}

// ----------------------------------------------------
//  Completely erase NVS
// ----------------------------------------------------
void AppConfig::clearNVS() {
    Preferences p;
    p.begin("config", false);
    p.clear();
    p.end();

    p.begin("battery_pwr", false);
    p.clear();
    p.end();

    p.begin("battery_bat", false);
    p.clear();
    p.end();

    p.begin("battery_stat", false);
    p.clear();
    p.end();

    Log(LOG_WARN, "NVS cleared");
}

// ----------------------------------------------------
//  Set default values (without deleting, without reboot)
// ----------------------------------------------------
void AppConfig::factoryDefaults() {

    // Battery intervals
    battery.intervalPwr  = 60000;
    battery.intervalBat  = 300000;
    battery.intervalStat = 1800000;

    battery.enableBat  = true;
    battery.enableStat = true;
    battery.useFahrenheit = false;

    // MQTT defaults
    mqtt.enabled = false;
    mqtt.server  = "";
    mqtt.port    = 1883;
    mqtt.user    = "";
    mqtt.pass    = "";

    mqtt.prefix     = "Pylontech";
    mqtt.topicStack = "Stack";
    mqtt.topicPwr   = "pwr";
    mqtt.topicBat   = "bat";
    mqtt.topicStat  = "stat";
    mqtt.mode       = "active";

    // Hostname / AP
    hostname = generateHostname();
    apSSID   = hostname;

    // WiFi radio enabled
    wifiEnabled = true;

    // NTP
    ntpServer = "pool.ntp.org";
    // Manual time mode
    manual_mode = false;
    manual_date = "";
    manual_time = "";

    use_gateway_ntp = false;   // factory default: manual NTP server
    manual_ntp      = true;

    // Default PWR fields
    battery.fieldsPwr.clear();

    auto add = [&](std::map<String, FieldConfig>& map, String key, String label, String factor, String unit, bool active){
        FieldConfig f;
            f.label = label;
            f.display = label;   // NEW: default display name = label
            f.factor = factor;
            f.unit = unit;
            f.mqtt = active;
            f.send = active;

            map[key] = f;

    };

    add(battery.fieldsPwr, "Volt",    "Voltage",     "0.001", "V",  true);
    add(battery.fieldsPwr, "Curr",    "Current",     "0.001", "A",  true);
    add(battery.fieldsPwr, "Tempr",   "Temperature", "0.001", "°C", true);
    add(battery.fieldsPwr, "Coulomb", "SOC",         "1",     "%",  true);

    // BAT + STAT empty by default
    battery.fieldsBat.clear();
    battery.fieldsStat.clear();

    currentTime     = "";
    lastPwrUpdate   = "";
    detectedModules = 0;
    lastMqttContact = "";

    configPassword = "12345";

    // Automatic scheduled reboot
    autoReboot      = false;
    rebootHour      = 3;
    rebootMin       = 0;
    rebootEveryDays = 1;

    Log(LOG_INFO, "factoryDefaults(): default fields set");
}

// ----------------------------------------------------
//  Reset the scheduled-reboot anchor
// ----------------------------------------------------
void AppConfig::resetRebootAnchor() {
    Preferences p;
    p.begin("config", false);
    p.remove("rbt_last");
    p.end();
}

// ----------------------------------------------------
//  Load ONLY system configuration
// ----------------------------------------------------
void AppConfig::loadSystemConfig() {
    Preferences p;
    p.begin("config", true);

    deviceName = p.getString("devname", deviceName);
    hostname   = p.getString("hostname", hostname);
    wifiSSID   = p.getString("wifi_ssid", wifiSSID);
    wifiPass   = p.getString("wifi_pass", wifiPass);
    apSSID     = p.getString("ap_ssid", apSSID);
    apPass     = p.getString("ap_pass", apPass);
    setupDone  = p.getBool("setup", setupDone);
    wifiEnabled = p.getBool("wifi_en", wifiEnabled);

    language = p.getString("lang", language);
    if (language != "es") language = "en";   // only en/es are supported

    useStaticIP = p.getBool("net_static", useStaticIP);
    ipAddr      = p.getString("net_ip", ipAddr);
    subnetMask  = p.getString("net_mask", subnetMask);
    gateway     = p.getString("net_gw", gateway);
    dns         = p.getString("net_dns", dns);

    ntpServer         = p.getString("ntp_srv", ntpServer);
    timezone          = p.getString("tz_name", timezone);
    daylightSaving    = p.getBool("tz_dst", daylightSaving);
    ntpResyncInterval = p.getULong("ntp_resync", ntpResyncInterval);
    // Manual time mode
    manual_mode = p.getBool("manual_mode", false);
    manual_date = p.getString("manual_date", "");
    manual_time = p.getString("manual_time", "");

    // NTP modes
    use_gateway_ntp = p.getBool("use_gateway_ntp", false);
    manual_ntp      = p.getBool("manual_ntp", true);

    mqtt.enabled = p.getBool("mqtt_en", mqtt.enabled);
    mqtt.server  = p.getString("mqtt_srv", mqtt.server);
    mqtt.port    = p.getUShort("mqtt_port", mqtt.port);
    mqtt.user    = p.getString("mqtt_user", mqtt.user);
    mqtt.pass    = p.getString("mqtt_pass", mqtt.pass);

    mqtt.prefix     = p.getString("mqtt_prefix", mqtt.prefix);
    mqtt.topicStack = p.getString("mqtt_t_stack", mqtt.topicStack);
    mqtt.topicPwr   = p.getString("mqtt_t_pwr",   mqtt.topicPwr);
    mqtt.topicBat   = p.getString("mqtt_t_bat",   mqtt.topicBat);
    mqtt.topicStat  = p.getString("mqtt_t_stat",  mqtt.topicStat);
    mqtt.mode       = p.getString("mqtt_mode",    mqtt.mode);
    mqtt.cellPrefix = p.getString("mqtt_cellprefix", mqtt.cellPrefix);

    currentTime     = p.getString("cur_time", currentTime);
    lastPwrUpdate   = p.getString("pwr_last", lastPwrUpdate);
    detectedModules = p.getUShort("pwr_mods", detectedModules);
    lastMqttContact = p.getString("mqtt_last", lastMqttContact);

    configPassword = p.getString("cfg_pass", "12345");

    autoReboot      = p.getBool("rbt_en", autoReboot);
    rebootHour      = p.getUChar("rbt_hour", rebootHour);
    rebootMin       = p.getUChar("rbt_min", rebootMin);
    rebootEveryDays = p.getUShort("rbt_days", rebootEveryDays);

    logInfo  = p.getBool("log_info",  true);
    logWarn  = p.getBool("log_warn",  true);
    logError = p.getBool("log_error", true);
    logDebug = p.getBool("log_debug", false);

    p.end();

    // ----------------------------------------------------
    // Defaults laden, wenn Config leer ist
    // ----------------------------------------------------
    if (hostname.length() == 0 || apSSID.length() == 0) {
        Log(LOG_WARN, "Config empty → applying factory defaults");
        factoryDefaults();
        setupDone = true;
        saveSystemConfig();
    }
}


// ----------------------------------------------------
//  Save ONLY system configuration
// ----------------------------------------------------
void AppConfig::saveSystemConfig() {
    Preferences p;
    p.begin("config", false);

    p.putString("devname", deviceName);
    p.putString("hostname", hostname);
    p.putString("wifi_ssid", wifiSSID);
    p.putString("wifi_pass", wifiPass);
    p.putString("ap_ssid", apSSID);
    p.putString("ap_pass", apPass);
    p.putBool("setup", setupDone);
    p.putBool("wifi_en", wifiEnabled);
    p.putString("lang", language);

    p.putBool("net_static", useStaticIP);
    p.putString("net_ip", ipAddr);
    p.putString("net_mask", subnetMask);
    p.putString("net_gw", gateway);
    p.putString("net_dns", dns);

    p.putString("ntp_srv", ntpServer);
    p.putString("tz_name", timezone);
    p.putBool("tz_dst", daylightSaving);
    p.putULong("ntp_resync", ntpResyncInterval);
    // Manual time mode
    p.putBool("manual_mode", manual_mode);
    p.putString("manual_date", manual_date);
    p.putString("manual_time", manual_time);
    p.remove("manual_dst");   // purge legacy key: DST now comes from the timezone rules

    // NTP modes
    p.putBool("use_gateway_ntp", use_gateway_ntp);
    p.putBool("manual_ntp", manual_ntp);

    p.putBool("mqtt_en", mqtt.enabled);
    p.putString("mqtt_srv", mqtt.server);
    p.putUShort("mqtt_port", mqtt.port);
    p.putString("mqtt_user", mqtt.user);
    p.putString("mqtt_pass", mqtt.pass);

    p.putString("mqtt_prefix", mqtt.prefix);
    p.putString("mqtt_t_stack", mqtt.topicStack);
    p.putString("mqtt_t_pwr",   mqtt.topicPwr);
    p.putString("mqtt_t_bat",   mqtt.topicBat);
    p.putString("mqtt_t_stat",  mqtt.topicStat);
    p.putString("mqtt_mode",    mqtt.mode);
    p.putString("mqtt_cellprefix", mqtt.cellPrefix);

    p.putString("cur_time", currentTime);
    p.putString("pwr_last", lastPwrUpdate);
    p.putUShort("pwr_mods", detectedModules);
    p.putString("mqtt_last", lastMqttContact);

    p.putString("cfg_pass", configPassword);

    p.putBool("rbt_en", autoReboot);
    p.putUChar("rbt_hour", rebootHour);
    p.putUChar("rbt_min", rebootMin);
    p.putUShort("rbt_days", rebootEveryDays);

    p.putBool("log_info",  logInfo);
    p.putBool("log_warn",  logWarn);
    p.putBool("log_error", logError);
    p.putBool("log_debug", logDebug);

    p.end();
}

// ----------------------------------------------------
//  PWR CONFIG
// ----------------------------------------------------
void AppConfig::loadPwrConfig() {
    Preferences p;
    p.begin("battery_pwr", true);

    battery.intervalPwr = p.getULong("interval", battery.intervalPwr);
    battery.enableBat   = p.getBool("enabled", battery.enableBat);

    // NEW: Load thresholds
    battery.cellDiffWarn  = p.getFloat("cellDiffWarn",  battery.cellDiffWarn);
    battery.cellDiffError = p.getFloat("cellDiffError", battery.cellDiffError);

    p.end();
}

void AppConfig::savePwrConfig() {
    Preferences p;
    p.begin("battery_pwr", false);

    p.putULong("interval", battery.intervalPwr);
    p.putBool("enabled", battery.enableBat);

    // NEW: Save thresholds
    p.putFloat("cellDiffWarn", battery.cellDiffWarn);
    p.putFloat("cellDiffError", battery.cellDiffError);

    p.end();
}

// ----------------------------------------------------
//  PWR FIELDS (JSON + chunks)
// ----------------------------------------------------
void AppConfig::savePwrFields() {
    DynamicJsonDocument doc(3000);
    JsonArray arr = doc.createNestedArray("fields");

    for (auto &kv : battery.fieldsPwr) {
        const String& name = kv.first;
        const FieldConfig& fc = kv.second;

        String packed;
        packed.reserve(80);
        packed += name; packed += "|";
        packed += fc.display; packed += "|";
        packed += fc.factor; packed += "|";
        packed += fc.unit; packed += "|";
        packed += (fc.mqtt ? "1" : "0"); packed += "|";
        packed += (fc.send ? "1" : "0");

        arr.add(packed);
    }

    String json;
    json.reserve(3000);
    serializeJson(doc, json);

    saveJsonChunked("battery_pwr", "pwr", json);
}

void AppConfig::loadPwrFields() {
    String json = loadJsonChunked("battery_pwr", "pwr");
    if (json.length() == 0) return;

    DynamicJsonDocument doc(3000);
    if (deserializeJson(doc, json)) return;

    JsonArray arr = doc["fields"];
    if (arr.isNull()) return;

    battery.fieldsPwr.clear();

    for (JsonVariant v : arr) {
        const char* packed = v.as<const char*>();
        if (!packed) continue;

        const char* p = packed;

        int p1 = strchr(p, '|') - p;
        int p2 = strchr(p + p1 + 1, '|') - p;
        int p3 = strchr(p + p2 + 1, '|') - p;
        int p4 = strchr(p + p3 + 1, '|') - p;
        int p5 = strchr(p + p4 + 1, '|') - p;

        String name(p, p1);
        String display(p + p1 + 1, p2 - p1 - 1);
        String factor(p + p2 + 1, p3 - p2 - 1);
        String unit(p + p3 + 1, p4 - p3 - 1);
        bool mqtt = (*(p + p4 + 1) == '1');
        bool send = (*(p + p5 + 1) == '1');

        FieldConfig fc;
        fc.label = display;
        fc.display = display;
        fc.factor = factor;
        fc.unit = unit;
        fc.mqtt = mqtt;
        fc.send = send;

        battery.fieldsPwr[name] = fc;
    }
}

void AppConfig::saveBatConfig() {
    Preferences p;
    p.begin("battery_bat", false);

    p.putULong("interval", battery.intervalBat);
    p.putBool("enabled", battery.enableBat);

    p.end();
}

// ----------------------------------------------------
//  BAT FIELDS (JSON + chunks)
// ----------------------------------------------------
void AppConfig::saveBatFields() {
    DynamicJsonDocument doc(3000);
    JsonArray arr = doc.createNestedArray("fields");

    for (auto &kv : battery.fieldsBat) {
        const String& name = kv.first;
        const FieldConfig& fc = kv.second;

        String packed;
        packed.reserve(80);
        packed += name; packed += "|";
        packed += fc.display; packed += "|";
        packed += fc.factor; packed += "|";
        packed += fc.unit; packed += "|";
        packed += (fc.mqtt ? "1" : "0"); packed += "|";
        packed += (fc.send ? "1" : "0");

        arr.add(packed);
    }

    String json;
    json.reserve(3000);
    serializeJson(doc, json);

    saveJsonChunked("battery_bat", "bat", json);
}

// ----------------------------------------------------
//  BAT CONFIG
// ----------------------------------------------------
void AppConfig::loadBatConfig() {
    Preferences p;
    p.begin("battery_bat", true);

    battery.intervalBat = p.getULong("interval", battery.intervalBat);
    // Floor matches the Cells page minimum (10 s): a stored value below it is
    // bumped on load, so the API, the UI and a boot cannot disagree.
    if (battery.intervalBat < 10000UL) battery.intervalBat = 10000UL;
    battery.enableBat   = p.getBool("enabled", battery.enableBat);

    p.end();
}

void AppConfig::loadBatFields() {
    String json = loadJsonChunked("battery_bat", "bat");
    if (json.length() == 0) return;

    DynamicJsonDocument doc(3000);
    if (deserializeJson(doc, json)) return;

    JsonArray arr = doc["fields"];
    if (arr.isNull()) return;

    battery.fieldsBat.clear();

    for (JsonVariant v : arr) {
        const char* packed = v.as<const char*>();
        if (!packed) continue;

        const char* p = packed;

        int p1 = strchr(p, '|') - p;
        int p2 = strchr(p + p1 + 1, '|') - p;
        int p3 = strchr(p + p2 + 1, '|') - p;
        int p4 = strchr(p + p3 + 1, '|') - p;
        int p5 = strchr(p + p4 + 1, '|') - p;

        String name(p, p1);
        String display(p + p1 + 1, p2 - p1 - 1);
        String factor(p + p2 + 1, p3 - p2 - 1);
        String unit(p + p3 + 1, p4 - p3 - 1);
        bool mqtt = (*(p + p4 + 1) == '1');
        bool send = (*(p + p5 + 1) == '1');

        FieldConfig fc;
        fc.label = display;
        fc.display = display;
        fc.factor = factor;
        fc.unit = unit;
        fc.mqtt = mqtt;
        fc.send = send;

        battery.fieldsBat[name] = fc;
    }
}

// ----------------------------------------------------
//  STAT CONFIG
// ----------------------------------------------------
void AppConfig::loadStatConfig() {
    Preferences p;
    p.begin("battery_stat", true);

    battery.intervalStat = p.getULong("interval", battery.intervalStat);
    battery.enableStat   = p.getBool("enabled", battery.enableStat);

    p.end();
}

void AppConfig::saveStatConfig() {
    Preferences p;
    p.begin("battery_stat", false);

    p.putULong("interval", battery.intervalStat);
    p.putBool("enabled", battery.enableStat);

    p.end();
}

// ----------------------------------------------------
//  STAT FIELDS (packed newline-delimited text + chunks)
//
//  Stored format: one field per line, "name|display|factor|unit|mqtt|send".
//  Built and parsed by hand on purpose: an ArduinoJson document for 51 fields
//  needs several KB, which does not exist on the fragmented heap.
// ----------------------------------------------------
void AppConfig::saveStatFields() {
    String packed;
    packed.reserve(battery.fieldsStat.size() * 48 + 8);

    for (auto &kv : battery.fieldsStat) {
        const String& name = kv.first;
        const FieldConfig& fc = kv.second;

        packed += name;       packed += "|";
        packed += fc.display; packed += "|";
        packed += fc.factor;  packed += "|";
        packed += fc.unit;    packed += "|";
        packed += (fc.mqtt ? "1" : "0"); packed += "|";
        packed += (fc.send ? "1" : "0"); packed += "\n";
    }

    saveJsonChunked("battery_stat", "stat", packed);
}

void AppConfig::loadStatFields() {
    String data = loadJsonChunked("battery_stat", "stat");
    if (data.length() == 0) return;

    auto applyLine = [&](const String& line) {
        int p1 = line.indexOf('|');
        int p2 = (p1 < 0) ? -1 : line.indexOf('|', p1 + 1);
        int p3 = (p2 < 0) ? -1 : line.indexOf('|', p2 + 1);
        int p4 = (p3 < 0) ? -1 : line.indexOf('|', p3 + 1);
        int p5 = (p4 < 0) ? -1 : line.indexOf('|', p4 + 1);
        if (p1 < 0 || p2 < 0 || p3 < 0 || p4 < 0 || p5 < 0) return;

        String name = line.substring(0, p1);
        if (name.length() == 0) return;

        FieldConfig fc;
        fc.display = line.substring(p1 + 1, p2);
        fc.label   = fc.display;
        fc.factor  = line.substring(p2 + 1, p3);
        fc.unit    = line.substring(p3 + 1, p4);
        fc.mqtt    = (line[p4 + 1] == '1');
        fc.send    = (line[p5 + 1] == '1');

        battery.fieldsStat[name] = fc;
    };

    // Legacy format was a JSON object {"fields":["name|display|factor|unit|mqtt|send",...]}.
    // Migrate it best-effort; the new format is plain packed lines.
    if (data[0] == '{') {
        DynamicJsonDocument doc(4096);
        if (deserializeJson(doc, data)) return;
        JsonArray arr = doc["fields"];
        if (arr.isNull()) return;
        battery.fieldsStat.clear();
        for (JsonVariant v : arr) {
            const char* packed = v.as<const char*>();
            if (packed) applyLine(String(packed));
        }
        return;
    }

    battery.fieldsStat.clear();
    const int len = data.length();
    int pos = 0;
    while (pos < len) {
        int nl = data.indexOf('\n', pos);
        String line = (nl < 0) ? data.substring(pos) : data.substring(pos, nl);
        pos = (nl < 0) ? len : nl + 1;
        line.trim();
        if (line.length() > 0) applyLine(line);
    }
}

// ----------------------------------------------------
//  SD CARD CONFIG
// ----------------------------------------------------
void AppConfig::loadSdCardConfig() {
    Preferences p;
    p.begin("sdcard", true);

    sdCardConfig.enabled        = p.getBool("enabled", sdCardConfig.enabled);
    sdCardConfig.logPwr         = p.getBool("logPwr", sdCardConfig.logPwr);
    sdCardConfig.logBat         = p.getBool("logBat", sdCardConfig.logBat);
    sdCardConfig.recordsPerFile = p.getULong("recPerFile", sdCardConfig.recordsPerFile);
    sdCardConfig.exportTimeoutMs = p.getULong("expTimeout", sdCardConfig.exportTimeoutMs);

    p.end();
}

void AppConfig::saveSdCardConfig() {
    Preferences p;
    p.begin("sdcard", false);

    p.putBool("enabled", sdCardConfig.enabled);
    p.putBool("logPwr", sdCardConfig.logPwr);
    p.putBool("logBat", sdCardConfig.logBat);
    p.putULong("recPerFile", sdCardConfig.recordsPerFile);
    p.putULong("expTimeout", sdCardConfig.exportTimeoutMs);

    p.end();
}

// ----------------------------------------------------
//  SCREEN CONFIG
// ----------------------------------------------------
void AppConfig::loadScreenConfig() {
    Preferences p;
    p.begin("screen", true);

    screen.enabled    = p.getBool("enabled", screen.enabled);
    screen.touch      = p.getBool("touch", screen.touch);
    screen.brightness = p.getUChar("bright", screen.brightness);
    screen.offHour    = p.getUChar("off_h", screen.offHour);
    screen.offMin     = p.getUChar("off_m", screen.offMin);
    screen.onHour     = p.getUChar("on_h", screen.onHour);
    screen.onMin      = p.getUChar("on_m", screen.onMin);
    screen.refreshSec = p.getUChar("refresh_sec", screen.refreshSec);
    screen.darkMode   = p.getBool("dark", screen.darkMode);
    screen.autoOff    = p.getBool("autoOff", screen.autoOff);
    screen.autoOffSec = p.getUShort("autoOffSec", screen.autoOffSec);

    // Single "sched" flag. Migrate from the old layout (two independent toggles).
    if (p.isKey("sched")) {
        screen.scheduled = p.getBool("sched", screen.scheduled);
    } else {
        bool tOff = p.getBool("timed_off", false);
        bool tOn  = p.getBool("timed_on", false);
        screen.scheduled = tOff || tOn;
    }

    p.end();
}

void AppConfig::saveScreenConfig() {
    Preferences p;
    p.begin("screen", false);

    p.putBool("enabled", screen.enabled);
    p.putBool("touch", screen.touch);
    p.putUChar("bright", screen.brightness);
    p.putBool("sched", screen.scheduled);
    p.putUChar("off_h", screen.offHour);
    p.putUChar("off_m", screen.offMin);
    p.putUChar("on_h", screen.onHour);
    p.putUChar("on_m", screen.onMin);
    p.putUChar("refresh_sec", screen.refreshSec);
    p.putBool("dark", screen.darkMode);
    p.putBool("autoOff", screen.autoOff);
    p.putUShort("autoOffSec", screen.autoOffSec);

    // Drop the legacy independent-toggled keys now stored under "sched".
    if (p.isKey("timed_off")) p.remove("timed_off");
    if (p.isKey("timed_on"))  p.remove("timed_on");

    p.end();
}

// ----------------------------------------------------
//  HEALTH HISTORY (persistent across reboots, NVS chunks)
//  Only the event list is stored; the transition state
//  (modAlarmState/modThreshState) is RAM-only.
// ----------------------------------------------------
void AppConfig::saveHealthHistory() {
    DynamicJsonDocument doc(8192);
    JsonArray ev = doc.createNestedArray("events");
    for (auto &e : health.events) {
        JsonObject o = ev.createNestedObject();
        o["t"] = e.time;
        o["m"] = e.module;
        o["x"] = e.text;
    }
    String json;
    serializeJson(doc, json);
    saveJsonChunked("config", "hev", json);
}

void AppConfig::loadHealthHistory() {
    String json = loadJsonChunked("config", "hev");
    if (json.length() == 0) {
        health.events.clear();
        return;
    }

    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, json)) return;

    health.events.clear();
    JsonArray ev = doc["events"];
    for (JsonObject o : ev) {
        HealthEvent e;
        e.time   = o["t"] | "";
        e.module = o["m"] | 0;
        e.text   = o["x"] | "";
        health.events.push_back(e);
    }
}

void AppConfig::wipeHealthHistory() {
    Preferences p;
    p.begin("config", false);
    for (int i = 0; i < 50; i++) {
        String key = "hev_" + String(i);
        if (p.isKey(key.c_str())) p.remove(key.c_str());
    }
    p.end();
    health.events.clear();
}

// ----------------------------------------------------
//  Main load() and save()
// ----------------------------------------------------
void AppConfig::load() {
    loadSystemConfig();

    loadPwrConfig();
    loadPwrFields();

    loadBatConfig();
    loadBatFields();

    loadStatConfig();
    loadStatFields();

    loadScreenConfig();

    loadSdCardConfig();

    // Persistent Health History (survives reboots; only Reset clears it)
    loadHealthHistory();

    // ---------------------------------------------------------
    // One-time migration: per-module "today" cell stats on the display require
    // BAT polling for every present module. The web toggle was off on devices
    // that never enabled it, so only the initial "bat 1" ever ran and only
    // module 1 showed cell/day data. Force enableBat=true exactly once; after
    // the marker is written the web checkbox is authoritative again.
    // ---------------------------------------------------------
    {
        Preferences m;
        m.begin("config", false);
        if (!m.getBool("mig_cellstats", false)) {
            m.putBool("mig_cellstats", true);
            battery.enableBat = true;

            Preferences bp;
            bp.begin("battery_pwr", false);
            bp.putBool("enabled", true);
            bp.end();

            Preferences bb;
            bb.begin("battery_bat", false);
            bb.putBool("enabled", true);
            bb.end();

            Log(LOG_INFO, "Migration: enableBat forced to true (per-module day stats)");
        }
        m.end();
    }

    // One-time: a previous diagnostic session left debug logging persisted on.
    // At ~8 KB free 8-bit DRAM it destabilizes the device (Task 2 has no
    // exception boundary), so clear it once. The web UI stays authoritative.
    {
        Preferences m;
        m.begin("config", false);
        if (!m.getBool("mig_logdebug_off", false)) {
            m.putBool("mig_logdebug_off", true);
            logDebug = false;
            m.putBool("log_debug", false);
        }
        m.end();
    }
}

void AppConfig::save() {
    saveSystemConfig();

    savePwrConfig();
    savePwrFields();

    saveBatConfig();
    saveBatFields();

    saveStatConfig();
    saveStatFields();

    saveScreenConfig();

    saveSdCardConfig();
}


String findPosixForTimezone(const String& tzName) {

    File f = SPIFFS.open("/timezone.json", "r");
    if (!f) {
        Serial.println("ERROR: timezone.json not found");
        return "UTC0";
    }

    DynamicJsonDocument doc(20000);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.println("ERROR: timezone.json parse failed");
        return "UTC0";
    }

    // Durch alle Regionen iterieren
    for (JsonPair region : doc.as<JsonObject>()) {
        JsonArray arr = region.value().as<JsonArray>();

        for (JsonObject entry : arr) {
            if (entry["tz"].as<String>() == tzName) {
                return entry["posix"].as<String>();
            }
        }
    }

    return "UTC0"; // fallback
}

// ----------------------------------------------------
//  Uptime helper
// ----------------------------------------------------
String AppConfig::uptimeString() {
    unsigned long ms = millis();
    unsigned long s  = ms / 1000;
    unsigned long m  = s / 60;
    unsigned long h  = m / 60;
    unsigned long d  = h / 24;

    char buf[64];
    snprintf(buf, sizeof(buf), "%lu d %02lu:%02lu:%02lu",
             d, h % 24, m % 60, s % 60);

    return String(buf);
}

// Buffer

PwrBuffer pwrA;
PwrBuffer pwrB;
volatile bool pwrUseA = true;
volatile uint32_t pwrFrameSeq = 0;

BatBuffer batA;
BatBuffer batB;
volatile bool batUseA = true;
volatile uint32_t batFrameSeq = 0;

StatBuffer statA;
StatBuffer statB;
volatile bool statUseA = true;

BatteryMode g_batteryMode = BatteryMode::UNKNOWN;
