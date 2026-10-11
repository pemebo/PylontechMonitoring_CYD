#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <map>
#include <set>
#include <vector>

String findPosixForTimezone(const String& tzName);
// Magic-Header-Deklaration
extern const char OTA_MAGIC_HEADER[];
// ---------------------------------------------------------
// FieldConfig
// ---------------------------------------------------------

struct FieldConfig {
    String label;      // UI label
    String display;    // NEW: MQTT display name (CamelCase)
    String factor;
    String unit;
    bool mqtt;
    bool send;
};

// ---------------------------------------------------------
// Screen configuration
// ---------------------------------------------------------
struct ScreenConfig {
    bool     enabled    = true;   // master screen switch: false = display + touch fully off (respected at boot)
    bool     touch      = true;   // touch enabled
    uint8_t  brightness = 60;     // manual brightness 0-100%
    bool     scheduled  = false;  // single toggle for timed power (off + on hours)
    uint8_t  offHour    = 23;
    uint8_t  offMin     = 0;
    uint8_t  onHour     = 7;
    uint8_t  onMin      = 0;
    uint8_t  refreshSec = 10;   // min UI refresh period in seconds (0 = refresh on every data update)
    bool     darkMode   = true; // dark theme (default); false = light theme
    bool     autoOff    = false;    // auto power-off after touch inactivity (default off)
    uint16_t autoOffSec = 30;       // inactivity timeout in seconds (max 9999, 4 digits)
};

// ---------------------------------------------------------
// Battery configuration
// ---------------------------------------------------------
struct BatteryConfig {
    unsigned long intervalPwr  = 60000;
    unsigned long intervalBat  = 300000;
    unsigned long intervalStat = 1800000;

    bool enableBat  = true;
    bool enableStat = false;

    bool useFahrenheit = false;

    std::map<String, FieldConfig> fieldsPwr;
    std::map<String, FieldConfig> fieldsBat;
    std::map<String, FieldConfig> fieldsStat;

    float cellDiffWarn = 0.1f;   // 100 mV warning threshold
    float cellDiffError = 0.15f; // 150 mV error threshold

    bool cellsScreenActive = false; // true when TFT CELLS screen is open; enables BAT polling even if enableBat=false
};

// ---------------------------------------------------------
// MQTT configuration
// ---------------------------------------------------------
struct MqttConfig {
    bool enabled = false;

    String server = "";
    uint16_t port = 1883;
    String user = "";
    String pass = "";

    String prefix     = "Pylontech";
    String topicStack = "Stack";
    String topicPwr   = "pwr";
    String topicBat   = "bat";
    String topicStat  = "stat";
    String cellPrefix = "Cell";   // NEW: configurable cell prefix

    String mode = "active";
};

// ---------------------------------------------------------
// Battery data structures (PWR / BAT / STAT)
// ---------------------------------------------------------
// A parsed name/value field pair. `raw` keeps the device's literal text so the
// web API and MQTT publish exactly what the battery sent.
struct FieldValue {
    String name;
    String raw;
};

// Flat, reusable field list for one module, deliberately NOT std::map<String,String>.
// A map entry is a separate red-black-tree node allocation per column per module,
// rebuilt on every pwr frame and deep-copied into two buffers: measured at ~35 KB
// of the 8-bit DRAM pool and ~400 extra live heap blocks per cycle, which is what
// shatters the heap. A vector allocates its backing store once; after reserve()
// the steady state does no container allocation at all, and field values fit the
// String SSO (14 chars) so they cost nothing either.
class ModuleFields {
public:
    void clear() { items.clear(); }
    void reserve(size_t n) { items.reserve(n); }

    // Find-or-replace by name. Matches the old std::map operator[] "last write
    // wins" semantics without the node allocation.
    void set(const String &name, const String &value) {
        for (auto &f : items) {
            if (f.name == name) { f.raw = value; return; }
        }
        FieldValue fv;
        fv.name = name;
        fv.raw  = value;
        items.push_back(fv);
    }

    // Returns nullptr when absent. Unlike std::map operator[] this never inserts,
    // which removes the accidental node growth the health evaluation caused.
    const String *get(const String &name) const {
        for (const auto &f : items) {
            if (f.name == name) return &f.raw;
        }
        return nullptr;
    }

    // Read with the old operator[] fallback: empty String when absent.
    String asString(const String &name) const {
        const String *v = get(name);
        return v ? *v : String();
    }

    size_t size() const { return items.size(); }

    const std::vector<FieldValue> &all() const { return items; }

private:
    std::vector<FieldValue> items;
};

struct BatteryModule {
    bool present = false;
    int hub   = 0;   // 0 = no hub / stack mode
    int stack = 0;   // 1..5 in hub mode
    int index = 0;   // module index (Power/Module)
    int voltage_mV = 0;
    int current_mA = 0;
    int temperature = 0;
    int soc = 0;
    ModuleFields fields;
};

struct BatteryStack {
    int batteryCount = 0;
    int avgVoltage_mV = 0;
    int totalCurrent_mA = 0;
    int temperature = 0;
    int soc = 0;

    int stackID = 0;   // <--- ADD THIS
    int hubID = 0;     // <--- OPTIONAL, useful for Masterhub

    String status = "";   // overall state: Alarm! / Charging / Discharging / Idle / Balance / Disconnected

    void reset() {
        batteryCount = 0;
        avgVoltage_mV = 0;
        totalCurrent_mA = 0;
        temperature = 0;
        soc = 0;
        status = "";
    }
};

struct BatData {
    int moduleIndex = 0;
    int cellIndex = -1;
    std::vector<String> values;   // index-aligned with the owning buffer's header
};

struct StatField {
    String name;
    String raw;
};

struct StatData {
    int moduleIndex = -1;
    std::vector<StatField> fields;
};

// Compact per-module STAT summary. The web APIs only read a handful of values out of
// the ~51-field STAT frame, so one of these per module replaces the old full StatData
// copy (statTable) and saves ~8 KB of 8-bit DRAM.
struct StatSummary {
    bool   valid = false;
    long   soh = -1;
    long   cycles = -1;
    float  capAh = -1.0f;
    long   ov = -1;
    long   hv = -1;
    long   lv = -1;
    long   uv = -1;
    long   lifeWarn = -1;
    long   lifeAlarm = -1;
    String device;
};

// ---------------------------------------------------------
// Double Buffer
// ---------------------------------------------------------
struct PwrBuffer {
    BatteryStack stack;
    std::vector<BatteryModule> modules;
};

struct BatBuffer {
    std::vector<String> header;
    std::vector<BatData> cells;
    // Module that produced `cells`, published with the frame (the parser sets it
    // before the buffer is toggled). Consumers read this instead of a separate
    // shared index, which could lag behind the frame and file one module's cells
    // under the previous module.
    int moduleIndex = 0;
};

struct StatBuffer {
    StatData stat;
};

// Double buffer for PWR
extern PwrBuffer pwrA;
extern PwrBuffer pwrB;
extern volatile bool pwrUseA;
// Monotonic frame counter, bumped by the parser (Task 1) right after a fresh
// PWR frame is published. Consumers that poll the double buffer (SD export)
// compare it against the value they last processed to detect "new frame"
// without a per-module dirty flag.
extern volatile uint32_t pwrFrameSeq;

// Double buffer for BAT
extern BatBuffer batA;
extern BatBuffer batB;
extern volatile bool batUseA;
extern volatile uint32_t batFrameSeq;

// Double buffer for STAT
extern StatBuffer statA;
extern StatBuffer statB;
extern volatile bool statUseA;


enum ParseResult {
    PARSE_OK,
    PARSE_FAIL,
    PARSE_IGNORED,
    PARSE_RANGE_INVALID
};

#define MAX_MODULES 80

// Plausible LFP voltage windows (mV, inclusive). A PWR/BAT frame with any
// value outside its window is rejected as a whole (see PARSE_RANGE_INVALID).
#define BAT_MOD_V_MIN_MV  44000
#define BAT_MOD_V_MAX_MV  54000
#define BAT_CELL_V_MIN_MV 2800
#define BAT_CELL_V_MAX_MV 3700

// Consecutive frame-level voltage rejections of one flow before a single
// LOG_ERROR is emitted (see py_uart.cpp).
#define VOLTAGE_STREAK_ALERT 10

// ---------------------------------------------------------
// Per-module cell extremes tracker (shared by web Health table
// and the TFT CELLS screen). All writes/reads happen in Task 2.
// times are stored as minutesOfDay (H*60+M, -1 = unknown).
// ---------------------------------------------------------
struct CellDayStats {
    int dayKey = -1;                  // YYYYMMDD, reset at midnight

    // Last frame (instantaneous)
    int lastMax_mV = -1;
    int lastMaxCell = -1;             // 1-based; 0 when unknown (PWR mode)
    int lastMin_mV = -1;
    int lastMinCell = -1;
    int lastDelta_mV = -1;

    // Today's extremes (reset at midnight)
    int todayMax_mV = -1;
    int todayMaxCell = -1;
    int todayMaxMin = -1;
    int todayMin_mV = -1;
    int todayMinCell = -1;
    int todayMinMin = -1;
    int todayDelta_mV = -1;
    int todayDeltaMin = -1;
};

extern CellDayStats cellDayStats[MAX_MODULES + 1];

// ---------------------------------------------------------
// Battery operating mode (Stack / Hub / Unknown)
// ---------------------------------------------------------
enum class BatteryMode {
    UNKNOWN = 0,
    STACK   = 1,
    HUB     = 2
};

// Global mode variable
extern BatteryMode g_batteryMode;


// ---------------------------------------------------------
// Parser / MQTT Flags
// ---------------------------------------------------------
extern bool parserHasData;

extern bool batParserHasData;

extern bool statParserHasData;
extern int  statParserModuleIndex;

extern bool discoveryPwrNeeded;
extern bool discoveryBatNeeded;
extern bool discoveryStatNeeded;

// ---------------------------------------------------------
// AppConfig
// ---------------------------------------------------------
class AppConfig {
public:
    String deviceName = "PylontechMonitor";
    String hostname   = "";
    String wifiSSID   = "";
    String wifiPass   = "";
    String apSSID     = "";
    String apPass     = "";
    bool setupDone    = false;
    bool wifiEnabled  = true;

    // UI language: "en" (default) or "es". Drives web + TFT texts.
    String language   = "en";

    bool useStaticIP = false;
    String ipAddr     = "";
    String subnetMask = "";
    String gateway    = "";
    String dns        = "";

    String ntpServer = "pool.ntp.org";
    bool manual_mode = false;
    bool use_gateway_ntp = false;
    bool manual_ntp = true;
    String manual_date = "";
    String manual_time = "";

    String timezone = "Europe/Berlin";
    bool daylightSaving = true;
    uint32_t ntpResyncInterval = 86400;

    MqttConfig mqtt;
    BatteryConfig battery;

    ScreenConfig screen;

    String firmwareVersion = "0.95";
    String currentTime     = "";
    String lastPwrUpdate   = "";
    uint16_t detectedModules = 0;

    String lastMqttContact = "";

    String configPassword = "12345";

    // Automatic scheduled reboot
    bool     autoReboot      = false;  // master switch for the scheduled reboot
    uint8_t  rebootHour      = 3;      // reboot time hour (0-23)
    uint8_t  rebootMin       = 0;      // reboot time minute (0-59)
    uint16_t rebootEveryDays = 1;      // days between reboots (1-365)

    // SD Card configuration
    struct SdCardConfig {
        bool     enabled       = false;   // master switch: enable SD card logging
        bool     logPwr        = true;    // log PWR data to CSV
        bool     logBat        = true;    // log BAT data to CSV
        uint32_t recordsPerFile = 1000;   // rotate CSV after N records (default 1000)
        uint32_t exportTimeoutMs = 600000; // 10 minutes max per export
    };

    void load();
    void save();

    void loadSystemConfig();
    void saveSystemConfig();

    void loadPwrConfig();
    void savePwrConfig();
    void loadPwrFields();
    void savePwrFields();

    void loadBatConfig();
    void saveBatConfig();
    void loadBatFields();
    void saveBatFields();

    void loadStatConfig();
    void saveStatConfig();
    void loadStatFields();
    void saveStatFields();

    void loadScreenConfig();
    void saveScreenConfig();

    void loadSdCardConfig();
    void saveSdCardConfig();

    void saveHealthHistory();
    void loadHealthHistory();
    void wipeHealthHistory();

    void clearNVS();
    void factoryDefaults();

    // Clears the scheduled-reboot anchor (NVS key "rbt_last") so the next
    // matching window reboots immediately after the schedule is saved.
    void resetRebootAnchor();

    String uptimeString();
    String getCurrentTimeString();
    String getCurrentDateTimeString();
    int getCurrentDayKey();
    int getCurrentMinutesOfDay();
    int getDSTOffsetForTimezone();
    bool isSystemTimeValid();

    bool logInfo  = true;
    bool logWarn  = true;
    bool logError = true;
    bool logDebug = false;

private:
    String generateHostname();

public:
    void saveJsonChunked(const char* ns, const char* prefix, const String& json);
    String loadJsonChunked(const char* ns, const char* prefix);
};
struct ModuleHealth {
    int index = 0;

    float tempMax = 0;      // highest temperature (Tempr or Thigh)
    float cellMin = 0;      // Vlow
    float cellMax = 0;      // Vhigh
    float cellDiff = 0;     // Delta

    String status = "OK";              // OK / Warning / Error (machine value for MQTT)
    String statusText = "Normal";      // web Status cell text (Event-format)
};

// A single health-history entry shown in the web Health table.
struct HealthEvent {
    String time;      // "YYYY-MM-DD HH:MM:SS"
    int module = 0;   // module index that generated the event (0 = none)
    String text;      // e.g. "Volt.St: OV", "Base.St: Standby", "Warning: threshold reached"
};

struct HealthStatus {
    std::vector<ModuleHealth> modules;

    std::vector<int> okModules;
    std::vector<int> warnModules;
    std::vector<int> errorModules;

    // Health History events (newest first). Only produced on state transitions,
    // never repeated while the alert stays active.
    std::vector<HealthEvent> events;

    // Transition tracking state (not exposed via API):
    // module -> set of currently abnormal alarm fields (dedup source)
    std::map<int, std::set<String>> modAlarmState;
    // module -> current threshold level: 0=none, 1=warn, 2=error
    std::map<int, int> modThreshState;

    float stackCellMin = 0;
    float stackCellMax = 0;
    float stackCellDiff = 0;
    float stackCellDiffMax = 0;   // max per-module cell delta (Vhigh - Vlow)

    String strongestMessage = "OK";
    String color = "green"; // green, yellow, red
};

extern HealthStatus health;

extern AppConfig::SdCardConfig sdCardConfig;


extern AppConfig config;