#include "py_parser_pwr.h"
#include "py_log.h"
#include "config.h"
#include "py_uart.h"

#include <cctype>
#include <algorithm>
#include <map>

// Per-module parsed-column reserve. A PWR header carries up to ~20 columns;
// reserving once keeps ModuleFields::items from reallocating while it is filled.
static constexpr size_t PWR_FIELDS_RESERVE = 24;

// UART instance
extern PyUart py_uart;

// Global parser data for Web UI
BatteryStack lastParsedStack;
std::vector<String> lastParserHeader;
std::vector<String> lastParserValues;

// New: hub/stack result
ParsedHubData lastParsedHub;

// External battery mode (must exist somewhere in your code)
extern BatteryMode g_batteryMode;

// ---------------------------------------------------------
// Helper: trim whitespace
// ---------------------------------------------------------
static String trimWS(const String& s) {
    String r = s;
    r.trim();
    return r;
}

// ---------------------------------------------------------
// Helper: true when the token is a plain integer (optional leading '-').
// An empty, sign-only or non-numeric value is treated as "absent", never as a
// number: the '-' placeholder must not become 0 and fail the voltage gate.
// ---------------------------------------------------------
static bool isIntegerToken(const String& s) {
    size_t start = (s.length() > 0 && s[0] == '-') ? 1 : 0;
    if (start >= s.length()) return false;   // empty or lone sign, no digits
    for (size_t i = start; i < s.length(); i++) {
        if (!isDigit(s[i])) return false;
    }
    return true;
}

// ---------------------------------------------------------
// Helper: split by whitespace
// ---------------------------------------------------------
static std::vector<String> splitWS(const String& line) {
    std::vector<String> out;
    int start = 0;

    while (start < line.length()) {
        while (start < line.length() && isspace((unsigned char)line[start])) start++;
        if (start >= line.length()) break;

        int end = start;
        while (end < line.length() && !isspace((unsigned char)line[end])) end++;

        out.push_back(line.substring(start, end));
        start = end;
    }
    return out;
}

// ---------------------------------------------------------
// Forward declarations for mode-specific parsers
// ---------------------------------------------------------
static ParseResult parsePwrFrameStackMode(const String& raw,
                                          BatteryStack& stackOut,
                                          std::vector<BatteryModule>& modulesOut);

static ParseResult parsePwrFrameHubMode(const String& raw,
                                        BatteryStack& stackOut,
                                        std::vector<BatteryModule>& modulesOut);

// ---------------------------------------------------------
// Main PWR parser (mode dispatcher)
// ---------------------------------------------------------
ParseResult parsePwrFrame(const String& raw,
                          BatteryStack& stackOut,
                          std::vector<BatteryModule>& modulesOut)
{
    // Only handle frames that belong to "pwr"
    if (py_uart.getLastCommand() != "pwr") {
        Log(LOG_DEBUG, "PWR parser: ignoring frame (last command was '" + py_uart.getLastCommand() + "')");
        return PARSE_IGNORED;
    }

    // Only handle valid frames
    if (!py_uart.isFrameValid()) {
        Log(LOG_WARN, "PWR parser: skipping invalid frame");
        return PARSE_FAIL;
    }

    modulesOut.clear();
    modulesOut.reserve(8);   // reserve outer vector once; module loop never reallocates
    stackOut.reset();
    lastParsedHub.hubs.clear();

    Log(LOG_INFO, "PWR parser: raw frame received, length=" + String(raw.length()));

    // Dispatch by battery mode
    if (g_batteryMode == BatteryMode::STACK) {
        return parsePwrFrameStackMode(raw, stackOut, modulesOut);
    }
    else if (g_batteryMode == BatteryMode::HUB) {
        return parsePwrFrameHubMode(raw, stackOut, modulesOut);
    }
    else {
        Log(LOG_WARN, "PWR parser: unknown battery mode, skipping");
        return PARSE_FAIL;
    }
}

// ---------------------------------------------------------
// Common: split frame into lines and header
// ---------------------------------------------------------
static bool splitFrameLinesAndHeader(const String& raw,
                                     std::vector<String>& lines,
                                     std::vector<String>& header)
{
    lines.clear();
    header.clear();

    int at  = raw.indexOf('@');
    int end = raw.indexOf("$$");
    if (at < 0 || end < 0 || end <= at) {
        Log(LOG_WARN, "PWR parser: no valid @ ... $$ frame found");
        return false;
    }

    lines.reserve(16);
    int pos = at + 1;
    while (pos < end) {
        int nl = raw.indexOf('\n', pos);
        if (nl < 0 || nl >= end) {
            String last = trimWS(raw.substring(pos, end));
            if (last.length() > 0) lines.push_back(last);
            break;
        }
        String line = trimWS(raw.substring(pos, nl));
        if (line.length() > 0) lines.push_back(line);
        pos = nl + 1;
    }

    if (lines.size() < 2) {
        Log(LOG_WARN, "PWR parser: too few lines");
        return false;
    }

    header = splitWS(lines[0]);
    if (header.size() < 3) {
        Log(LOG_WARN, "PWR parser: header too small");
        return false;
    }

    lastParserHeader = header;
    lastParserValues.clear();
    return true;
}

// ---------------------------------------------------------
// Overall stack status (mirrors Example_monitoring decision logic)
// ---------------------------------------------------------
static String getField(const BatteryModule &m, const char *key) {
    const String *p = m.fields.get(key);
    return p ? *p : String();
}

static bool stateIsNormal(const String &s) {
    if (s.length() == 0) return true;   // columna ausente: no penalizar
    if (s == "-") return true;
    return (s == "Normal" || s == "normal");
}

// Mirrors Example_monitoring batteryStack.h isNormal():
// baseState is one of the 4 known states AND all state columns are Normal.
static bool moduleIsNormal(const BatteryModule &m) {
    String base = getField(m, "Base.St");
    if (base.length() == 0) base = getField(m, "Base");

    if (base != "Charge" && base != "Dischg" && base != "Idle" && base != "Balance") {
        return false;
    }

    if (!stateIsNormal(getField(m, "Volt.St"))) return false;
    if (!stateIsNormal(getField(m, "Curr.St"))) return false;
    if (!stateIsNormal(getField(m, "Temp.St"))) return false;

    // Extra state columns (e.g. SysAlarm.St, b_v_st, b_t_st equivalents)
    for (const auto &f : m.fields.all()) {
        const String &k = f.name;
        if (k == "Base.St" || k == "Base") continue;
        if (k == "Volt.St" || k == "Curr.St" || k == "Temp.St") continue;
        if (k.length() >= 3 && k.indexOf(".St") >= 0) {
            if (!stateIsNormal(f.raw)) return false;
        }
    }
    return true;
}

// Decision scheme:
//   any module abnormal            -> "Alarm!"
//   all modules charging           -> "Charging"
//   all modules discharging        -> "Discharging"
//   all modules idle               -> "Idle"
//   otherwise (none in alarm)      -> "Balance"
//   no modules at all              -> "Disconnected"
static String computeStackStatus(const std::vector<BatteryModule>& modules) {
    int n = 0;
    int alarmCnt = 0;
    int chargeCnt = 0;
    int dischargeCnt = 0;
    int idleCnt = 0;

    for (const auto &m : modules) {
        if (!m.present) continue;
        n++;

        String base = getField(m, "Base.St");
        if (base.length() == 0) base = getField(m, "Base");

        if (!moduleIsNormal(m)) {
            alarmCnt++;
        }
        else if (base == "Charge") {
            chargeCnt++;
        }
        else if (base == "Dischg") {
            dischargeCnt++;
        }
        else if (base == "Idle") {
            idleCnt++;
        }
        else {
            // Module individually in "Balance" -> counts as balancing (normal)
        }
    }

    if (n == 0) return "Disconnected";
    if (alarmCnt > 0) return "Alarm!";
    if (chargeCnt == n) return "Charging";
    if (dischargeCnt == n) return "Discharging";
    if (idleCnt == n) return "Idle";
    return "Balance";
}

// Round-Half-Up average, 0 if no elements
static int roundAvg(long sum, int n) {
    if (n <= 0) return 0;
    return (int)((sum + n / 2) / n);
}

// SOC strategy: average of module SOCs unless the stack is actively
// discharging (negative stack current), in which case the minimum is shown
// (conservative). Using the measured stack current instead of the per-module
// status avoids falling to the minimum while charging just because a module
// is balancing / full / idle and does not report "Charge".
static int computeStackSoc(long sumSoc, int minSoc, int n, long totalCurrent_mA) {
    if (totalCurrent_mA < 0) return minSoc;
    return roundAvg(sumSoc, n);
}

// ---------------------------------------------------------
// HEALTH EVALUATION (shared by STACK and HUB modes)
// Produces per-module status plus a transition-triggered event
// history shown on the web Health page table.
// ---------------------------------------------------------

#define MAX_HEALTH_EVENTS 20

static bool healthHistoryDirty = false;
static void flushHealthHistory() {
    if (!healthHistoryDirty) return;
    healthHistoryDirty = false;
    config.saveHealthHistory();
}

static bool healthFieldBad(const String &s) {
    if (s.length() == 0) return false;
    if (s == "-") return false;
    if (s == "Normal" || s == "normal") return false;
    return true;
}

static void addHealthEvent(int module, const String &text) {
    HealthEvent e;
    e.time   = config.getCurrentDateTimeString();
    e.module = module;
    e.text   = text;
    health.events.insert(health.events.begin(), e);
    if (health.events.size() > MAX_HEALTH_EVENTS)
        health.events.pop_back();
    healthHistoryDirty = true;
}

static void evaluateModuleHealth(const std::vector<BatteryModule>& modulesOut, bool withEvents) {
    health.modules.clear();
    health.okModules.clear();
    health.warnModules.clear();
    health.errorModules.clear();
    health.stackCellMin = 0;
    health.stackCellMax = 0;
    health.stackCellDiff = 0;
    health.stackCellDiffMax = 0;

    // Alarm fields: any value other than "Normal" flags the module as Error.
    static const char *ALARM_FIELDS[] = {
        "Volt.St", "Curr.St", "Temp.St", "B.V.St", "B.T.St", "M.T.St", "SysAlarm.St"
    };
    const size_t ALARM_FIELD_COUNT = sizeof(ALARM_FIELDS) / sizeof(ALARM_FIELDS[0]);

    for (auto &m : modulesOut) {
        if (!m.present) continue;
        int idx = m.index;

        ModuleHealth mh;
        mh.index = idx;

        int t1 = m.temperature;
        int t2 = m.fields.asString("Thigh").toInt();
        if (t1 > 0) mh.tempMax = t1 / 1000.0f;
        if (t2 > 0 && t2 > t1) mh.tempMax = t2 / 1000.0f;

        String vlow  = m.fields.asString("Vlow");
        String vhigh = m.fields.asString("Vhigh");
        if (vlow != "-" && vhigh != "-") {
            int low  = vlow.toInt();
            int high = vhigh.toInt();
            if (low > 1000 && low < 5000 && high > 1000 && high < 5000) {
                mh.cellMin = low / 1000.0f;
                mh.cellMax = high / 1000.0f;
                mh.cellDiff = mh.cellMax - mh.cellMin;
                if (mh.cellDiff > health.stackCellDiffMax)
                    health.stackCellDiffMax = mh.cellDiff;

                if (health.stackCellMin == 0 || mh.cellMin < health.stackCellMin)
                    health.stackCellMin = mh.cellMin;
                if (mh.cellMax > health.stackCellMax)
                    health.stackCellMax = mh.cellMax;
            }
        }

        // --- Alarm fields (normal -> abnormal transition only) ---
        std::set<String> prevAbnormal;

        std::set<String> curAbnormal;

        if (withEvents) {
            auto pit = health.modAlarmState.find(idx);
            if (pit != health.modAlarmState.end()) prevAbnormal = pit->second;
        }

        for (size_t i = 0; i < ALARM_FIELD_COUNT; i++) {
            const char *k = ALARM_FIELDS[i];
            String val = m.fields.asString(k);
            if (healthFieldBad(val)) {
                String key(k);
                curAbnormal.insert(key);
                if (withEvents && prevAbnormal.find(key) == prevAbnormal.end()) {
                    addHealthEvent(idx, key + ": " + val);
                }
            }
        }

        // Base.St alarm when module is not charging / discharging / idle
        String base = m.fields.asString("Base.St");
        if (base.length() == 0) base = m.fields.asString("Base");
        bool baseBad = (base.length() > 0 && base != "-"
                     && base != "Charge" && base != "Dischg" && base != "Idle");
        if (baseBad) {
            curAbnormal.insert("Base.St");
            if (withEvents && prevAbnormal.find("Base.St") == prevAbnormal.end()) {
                addHealthEvent(idx, "Base.St: " + base);
            }
        }

        if (withEvents) {
            health.modAlarmState[idx] = curAbnormal;
            if (curAbnormal.empty()) health.modAlarmState.erase(idx);
        }

        // --- Threshold levels (Warning / Error, transition only) ---
        int lvl = 0;
        if (mh.cellDiff > config.battery.cellDiffError) lvl = 2;
        else if (mh.cellDiff > config.battery.cellDiffWarn) lvl = 1;

        if (withEvents) {
            int prevLvl = 0;
            auto tit = health.modThreshState.find(idx);
            if (tit != health.modThreshState.end()) prevLvl = tit->second;

            if (lvl > prevLvl) {
                addHealthEvent(idx, (lvl == 2) ? "Error: threshold reached"
                                               : "Warning: threshold reached");
            }
            health.modThreshState[idx] = lvl;
            if (lvl == 0) health.modThreshState.erase(idx);
        }

        // --- Status text (web Status cell, Event-format, comma-joined) ---
        String st = "";
        for (const String &k : curAbnormal) {
            String text = (k == "Base.St") ? ("Base.St: " + base)
                                           : (k + ": " + m.fields.asString(k));
            if (!st.isEmpty()) st += ", ";
            st += text;
        }
        if (lvl == 2) {
            if (!st.isEmpty()) st += ", ";
            st += "Error: threshold reached";
        }
        else if (lvl == 1) {
            if (!st.isEmpty()) st += ", ";
            st += "Warning: threshold reached";
        }
        mh.statusText = st.isEmpty() ? "Normal" : st;

        // --- Module status ---
        bool modError = false;
        bool modWarn  = false;

        if (!curAbnormal.empty() || mh.cellDiff > config.battery.cellDiffError)
            modError = true;
        else if (mh.cellDiff > config.battery.cellDiffWarn)
            modWarn = true;

        if (modError) {
            mh.status = "Error";
            health.errorModules.push_back(idx);
        }
        else if (modWarn) {
            mh.status = "Warning";
            health.warnModules.push_back(idx);
        }
        else {
            mh.status = "OK";
            health.okModules.push_back(idx);
        }

        health.modules.push_back(mh);
    }

    health.stackCellDiff = health.stackCellMax - health.stackCellMin;

    if (!health.errorModules.empty()) {
        health.color = "red";
        health.strongestMessage = "Error in modules";
    }
    else if (!health.warnModules.empty()) {
        health.color = "yellow";
        health.strongestMessage = "Warning in modules";
    }
    else {
        health.color = "green";
        health.strongestMessage = "OK";
    }
}

// Re-evaluate the current health summary from the last PWR frame.
// Used by the web Reset button: refreshes header/table immediately
// without generating events (withEvents=false).
void refreshHealthFromSnapshot() {
    evaluateModuleHealth(currentPwrModules(), false);
}

// ---------------------------------------------------------
// STACK MODE PARSER (original behavior, minimal changes)
// ---------------------------------------------------------
static ParseResult parsePwrFrameStackMode(const String& raw,
                                          BatteryStack& stackOut,
                                          std::vector<BatteryModule>& modulesOut)
{
    static std::vector<String> lines;
    static std::vector<String> header;
    if (!splitFrameLinesAndHeader(raw, lines, header)) {
        return PARSE_FAIL;
    }

    int baseIndex = -1;
    int timeIndex = -1;

    for (size_t h = 0; h < header.size(); h++) {
        if (header[h] == "Base.St" || header[h] == "Base") {
            baseIndex = h;
        }
        if (header[h] == "Time") {
            timeIndex = h;
        }
    }

    // Frame-level voltage gate: the first module with a voltage outside the
    // plausible LFP window is remembered here and rejects the whole frame after
    // the loop, so one corrupt column cannot leak wild values downstream.
    int badVoltModule = -1;
    int badVoltMv     = 0;

    for (size_t i = 1; i < lines.size(); i++) {

        std::vector<String> cols = splitWS(lines[i]);
        if (cols.empty()) continue;

        // Merge date + time if split
        if (timeIndex >= 0 && cols.size() > (size_t)timeIndex + 1) {
            String datePart = cols[timeIndex];
            String timePart = cols[timeIndex + 1];

            bool looksLikeDate = (datePart.indexOf('-') > 0 || datePart.indexOf('/') > 0);
            bool looksLikeTime = (timePart.indexOf(':') > 0);

            if (looksLikeDate && looksLikeTime) {
                cols[timeIndex] = datePart + " " + timePart;
                cols.erase(cols.begin() + timeIndex + 1);
            }
        }

        if (cols.size() < header.size()) continue;

        // Absent → end of list
        if (baseIndex >= 0 && cols[baseIndex] == "Absent") {
            Log(LOG_INFO, "PWR parser (STACK): Absent detected at line " + String(i));
            break;
        }

        // First valid line for Web UI
        if (lastParserValues.empty()) {
            lastParserValues = cols;
        }

        BatteryModule mod;
        mod.fields.reserve(PWR_FIELDS_RESERVE);
        mod.present = true;
        mod.hub = 0;
        mod.stack = 0;
        bool voltNumeric = false;

        for (size_t c = 0; c < header.size(); c++) {
            const String& col = header[c];
            const String& value = cols[c];

            mod.fields.set(col, value);

            if (col == "Power" || col == "Battery") {
                mod.index = value.toInt();
            }
            else if (col == "Volt") {
                voltNumeric = isIntegerToken(value);
                if (voltNumeric) mod.voltage_mV = value.toInt();
            }
            else if (col == "Curr") {
                mod.current_mA = value.toInt();
            }
            else if (col == "Tempr") {
                mod.temperature = value.toInt();
            }
            else if (col == "Coulomb" || col == "SOC") {
                String v = value;
                v.replace("%", "");
                mod.soc = v.toInt();
            }
        }

        // Voltage is validated at frame level (see badVoltModule above): an
        // out-of-range value flags the frame even when this row would also be
        // skipped below as implausible for a non-voltage reason. A placeholder
        // ("-") is not numeric and must not flag the frame.
        if (voltNumeric && badVoltModule < 0 &&
            (mod.voltage_mV < BAT_MOD_V_MIN_MV || mod.voltage_mV > BAT_MOD_V_MAX_MV)) {
            badVoltModule = mod.index;
            badVoltMv     = mod.voltage_mV;
        }

        bool plausible = true;
        plausible &= voltNumeric;
        plausible &= (mod.index >= 1 && mod.index <= 32);
        plausible &= (mod.temperature > 1000 && mod.temperature < 60000);
        plausible &= (mod.soc >= 1 && mod.soc <= 100);

        if (!plausible) {
            Log(LOG_WARN, "PWR parser (STACK): skipping implausible module line " + String(i));
            continue;
        }

        modulesOut.push_back(mod);
    }

    if (badVoltModule >= 0) {
        Log(LOG_DEBUG, "PWR parser (STACK): rejecting frame, module " + String(badVoltModule) +
                       " voltage " + String(badVoltMv) + " mV outside " +
                       String(BAT_MOD_V_MIN_MV) + "-" + String(BAT_MOD_V_MAX_MV) + " mV");
        modulesOut.clear();
        return PARSE_RANGE_INVALID;
    }

    if (modulesOut.empty()) {
        Log(LOG_WARN, "PWR parser (STACK): no modules parsed");
        return PARSE_FAIL;
    }

    // Stack calculation (unchanged logic)
    int count = modulesOut.size();
    stackOut.batteryCount = count;
    config.detectedModules = count;

    stackOut.status = computeStackStatus(modulesOut);

    long sumVolt = 0;
    long sumCurr = 0;
    long sumSoc  = 0;
    int minSoc   = 999;
    int maxTemp  = -999;

    for (auto& m : modulesOut) {
        sumVolt += m.voltage_mV;
        sumCurr += m.current_mA;
        sumSoc  += m.soc;

        if (m.soc < minSoc) minSoc = m.soc;
        if (m.temperature > maxTemp) maxTemp = m.temperature;
    }

    stackOut.avgVoltage_mV   = sumVolt / count;
    stackOut.totalCurrent_mA = sumCurr;
    stackOut.soc             = computeStackSoc(sumSoc, minSoc, count, stackOut.totalCurrent_mA);
    stackOut.temperature     = maxTemp;

    config.lastPwrUpdate = config.getCurrentTimeString();

    // Web UI data
    lastParsedStack   = stackOut;

    // Double buffer
    PwrBuffer* target = pwrUseA ? &pwrB : &pwrA;
    target->stack   = stackOut;
    target->modules = modulesOut;
    pwrUseA = !pwrUseA;
    pwrFrameSeq++;

    // Health evaluation: module status + transition-triggered events
    evaluateModuleHealth(modulesOut, true);
    flushHealthHistory();

    Log(LOG_INFO, "PWR parser (STACK): parsed " + String(count) + " modules");
    return PARSE_OK;
}

// ---------------------------------------------------------
// HUB MODE PARSER (Hub + Masterhub)
// ---------------------------------------------------------
static ParseResult parsePwrFrameHubMode(const String& raw,
                                        BatteryStack& stackOut,
                                        std::vector<BatteryModule>& modulesOut)
{
    static std::vector<String> lines;
    static std::vector<String> header;
    if (!splitFrameLinesAndHeader(raw, lines, header)) {
        return PARSE_FAIL;
    }

    int colAddress = -1;
    int colStack   = -1;
    int colModule  = -1;
    int timeIndex  = -1;

    for (size_t h = 0; h < header.size(); h++) {
        if (header[h] == "Address") colAddress = h;
        if (header[h] == "Stack")   colStack   = h;
        if (header[h] == "Module")  colModule  = h;
        if (header[h] == "Time")    timeIndex  = h;
    }

    if (colStack < 0 || colModule < 0) {
        Log(LOG_WARN, "PWR parser (HUB): missing Stack/Module columns");
        return PARSE_FAIL;
    }

    // hubID -> stackID -> modules
    std::map<int, std::map<int, std::vector<BatteryModule>>> hubMap;

    // Frame-level voltage gate (same rule as STACK mode): reject the whole frame
    // when any accepted module's voltage is outside the plausible LFP window.
    int badVoltModule = -1;
    int badVoltMv     = 0;

    for (size_t i = 1; i < lines.size(); i++) {

        std::vector<String> cols = splitWS(lines[i]);
        if (cols.empty()) continue;

        // Merge date + time if split
        if (timeIndex >= 0 && cols.size() > (size_t)timeIndex + 1) {
            String datePart = cols[timeIndex];
            String timePart = cols[timeIndex + 1];

            bool looksLikeDate = (datePart.indexOf('-') > 0 || datePart.indexOf('/') > 0);
            bool looksLikeTime = (timePart.indexOf(':') > 0);

            if (looksLikeDate && looksLikeTime) {
                cols[timeIndex] = datePart + " " + timePart;
                cols.erase(cols.begin() + timeIndex + 1);
            }
        }

        if (cols.size() < header.size()) continue;

        if (lastParserValues.empty()) {
            lastParserValues = cols;
        }

        int hubID    = (colAddress >= 0) ? cols[colAddress].toInt() : 1;
        int stackID  = cols[colStack].toInt();
        int moduleID = cols[colModule].toInt();

        BatteryModule mod;
        mod.fields.reserve(PWR_FIELDS_RESERVE);
        mod.present = true;
        mod.hub     = hubID;
        mod.stack   = stackID;
        mod.index   = moduleID;
        bool voltNumeric = false;

        for (size_t c = 0; c < header.size(); c++) {
            const String& col   = header[c];
            const String& value = cols[c];

            mod.fields.set(col, value);

            if (col == "Volt") {
                voltNumeric = isIntegerToken(value);
                if (voltNumeric) mod.voltage_mV = value.toInt();
            }
            else if (col == "Curr") {
                mod.current_mA = value.toInt();
            }
            else if (col == "Tempr") {
                mod.temperature = value.toInt();
            }
            else if (col == "Coulomb" || col == "SOC") {
                String v = value;
                v.replace("%", "");
                mod.soc = v.toInt();
            }
        }

        // Voltage is validated at frame level (see badVoltModule above): an
        // out-of-range value flags the frame even when this row would also be
        // skipped below as implausible for a non-voltage reason. A placeholder
        // ("-") is not numeric and must not flag the frame.
        if (voltNumeric && badVoltModule < 0 &&
            (mod.voltage_mV < BAT_MOD_V_MIN_MV || mod.voltage_mV > BAT_MOD_V_MAX_MV)) {
            badVoltModule = moduleID;
            badVoltMv     = mod.voltage_mV;
        }

        bool plausible = true;
        plausible &= voltNumeric;
        plausible &= (stackID  >= 1 && stackID  <= 5);
        plausible &= (moduleID >= 1 && moduleID <= 16);
        plausible &= (mod.temperature > 1000 && mod.temperature < 60000);
        plausible &= (mod.soc >= 1 && mod.soc <= 100);

        if (!plausible) {
            Log(LOG_WARN, "PWR parser (HUB): skipping implausible module line " + String(i));
            continue;
        }

        hubMap[hubID][stackID].push_back(mod);
        modulesOut.push_back(mod);
    }

    if (badVoltModule >= 0) {
        Log(LOG_DEBUG, "PWR parser (HUB): rejecting frame, module " + String(badVoltModule) +
                       " voltage " + String(badVoltMv) + " mV outside " +
                       String(BAT_MOD_V_MIN_MV) + "-" + String(BAT_MOD_V_MAX_MV) + " mV");
        modulesOut.clear();
        return PARSE_RANGE_INVALID;
    }

    if (modulesOut.empty()) {
        Log(LOG_WARN, "PWR parser (HUB): no modules parsed");
        return PARSE_FAIL;
    }

    // Sort modules by hub, stack, module
    std::sort(modulesOut.begin(), modulesOut.end(),
        [](const BatteryModule& a, const BatteryModule& b) {
            if (a.hub   != b.hub)   return a.hub   < b.hub;
            if (a.stack != b.stack) return a.stack < b.stack;
            return a.index < b.index;
        });

    // Build per-stack BatteryStack and overall stackOut
    lastParsedHub.hubs.clear();
    BatteryStack total;
    total.reset();

    int totalModules = 0;
    long totalSocSum = 0;
    int totalSocMin  = 999;

    for (auto &hubPair : hubMap) {
        int hubID = hubPair.first;
        for (auto &stackPair : hubPair.second) {
            int stackID = stackPair.first;
            auto &mods  = stackPair.second;

            if (mods.empty()) continue;

            BatteryStack s;
            s.reset();
            s.stackID = stackID;
            s.batteryCount = mods.size();

            long sumVolt = 0;
            long sumCurr = 0;
            long sumSoc  = 0;
            int minSoc   = 999;
            int maxTemp  = -999;

            for (auto &m : mods) {
                sumVolt += m.voltage_mV;
                sumCurr += m.current_mA;
                sumSoc  += m.soc;
                if (m.soc < minSoc) minSoc = m.soc;
                if (m.temperature > maxTemp) maxTemp = m.temperature;
            }

            s.status = computeStackStatus(mods);

            s.avgVoltage_mV   = sumVolt / mods.size();
            s.totalCurrent_mA = sumCurr;
            s.soc             = computeStackSoc(sumSoc, minSoc, (int)mods.size(), s.totalCurrent_mA);
            s.temperature     = maxTemp;

            lastParsedHub.hubs[hubID][stackID] = s;

            // Accumulate into total
            totalModules += s.batteryCount;
            total.batteryCount   += s.batteryCount;
            total.avgVoltage_mV  += s.avgVoltage_mV;
            total.totalCurrent_mA += s.totalCurrent_mA;

            if (total.temperature == 0 || s.temperature > total.temperature)
                total.temperature = s.temperature;

            totalSocSum += sumSoc;
            if (minSoc < totalSocMin) totalSocMin = minSoc;
        }
    }

    if (totalModules > 0) {
        int stackCount = 0;
        for (auto &hubPair : lastParsedHub.hubs)
            stackCount += hubPair.second.size();

        if (stackCount > 0)
            total.avgVoltage_mV /= stackCount;
    }

    stackOut = total;
    stackOut.status = computeStackStatus(modulesOut);
    stackOut.soc    = computeStackSoc(totalSocSum, totalSocMin, totalModules, stackOut.totalCurrent_mA);
    config.detectedModules = totalModules;
    config.lastPwrUpdate = config.getCurrentTimeString();

    // Web UI data
    lastParsedStack   = stackOut;

    // Double buffer
    PwrBuffer* target = pwrUseA ? &pwrB : &pwrA;
    target->stack   = stackOut;
    target->modules = modulesOut;
    pwrUseA = !pwrUseA;
    pwrFrameSeq++;

    // Health evaluation (reuse same logic, now across all modules)
    evaluateModuleHealth(modulesOut, true);
    flushHealthHistory();

    Log(LOG_INFO, "PWR parser (HUB): parsed " + String(totalModules) + " modules");
    return PARSE_OK;
}
