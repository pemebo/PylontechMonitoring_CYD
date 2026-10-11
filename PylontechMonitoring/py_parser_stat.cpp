#include "py_parser_stat.h"
#include "py_log.h"
#include "py_uart.h"
#include <utility>   // std::move — avoid deep-copy heap churn (bad_alloc fix)

extern PyUart py_uart;

// Per-module STAT summary (index 1..MAX_MODULES)
StatSummary statSummary[MAX_MODULES + 1];

// ---------------------------------------------------------
// Helper: trim whitespace
// ---------------------------------------------------------
static String trimWS(const String& s) {
    String r = s;
    r.trim();
    return r;
}

// Look up a STAT field by lowercase name: exact match first, then substring.
static String statFieldValue(const std::vector<StatField>& fields, const char* key) {
    String k(key);
    for (const auto& f : fields) {
        String n = f.name; n.toLowerCase(); n.trim();
        if (n == k) return f.raw;
    }
    for (const auto& f : fields) {
        String n = f.name; n.toLowerCase(); n.trim();
        if (n.indexOf(k) >= 0) return f.raw;
    }
    return "";
}
static long statFieldInt(const std::vector<StatField>& fields, const char* key) {
    String s = statFieldValue(fields, key);
    return (s.length() == 0) ? -1 : (long)s.toDouble();
}
static void fillStatSummary(int idx, const std::vector<StatField>& fields) {
    StatSummary& sm = statSummary[idx];
    sm.valid = true;
    sm.device = statFieldValue(fields, "device address");
    sm.soh = statFieldInt(fields, "soh");
    sm.cycles = statFieldInt(fields, "cycle times");
    String cap = statFieldValue(fields, "dsg cap");
    sm.capAh = (cap.length() == 0) ? -1.0f : (float)(cap.toDouble() / 1000.0);
    sm.ov = statFieldInt(fields, "bat ov times");
    sm.hv = statFieldInt(fields, "bat hv times");
    sm.lv = statFieldInt(fields, "bat lv times");
    sm.uv = statFieldInt(fields, "bat uv times");
    sm.lifeWarn = statFieldInt(fields, "lifewarn times");
    sm.lifeAlarm = statFieldInt(fields, "lifealarm times");
}

// ---------------------------------------------------------
// STAT parser
// ---------------------------------------------------------
ParseResult parseStatFrame(int /*moduleIndex*/,
                           const String& raw,
                           StatData& out)
{
    out.fields.clear();
    // A STAT frame pushes ~51 fields, so reserve the real maximum up front.
    out.fields.reserve(51);
    out.moduleIndex = -1;

    // ---------------------------------------------------------
    // 1) Check if this frame belongs to STAT
    // ---------------------------------------------------------
    String last = py_uart.getLastCommand();
    String lastLower = last;
    lastLower.trim();
    lastLower.toLowerCase();

    if (!lastLower.startsWith("stat")) {
        Log(LOG_DEBUG, "STAT parser: ignoring frame (last command was '" + last + "')");
        return PARSE_IGNORED;
    }

    // Extract module index
    int idx = lastLower.substring(4).toInt();
    if (idx <= 0 || idx > 16) {
        Log(LOG_WARN, "STAT parser: invalid module index in command '" + last + "'");
        return PARSE_IGNORED;
    }

    out.moduleIndex = idx;

    // ---------------------------------------------------------
    // 2) Validate frame
    // ---------------------------------------------------------
    if (!py_uart.isFrameValid()) {
        Log(LOG_WARN, "STAT parser: skipping invalid frame");
        return PARSE_FAIL;
    }

    Log(LOG_INFO, "STAT parser: raw frame received for module " + String(idx));

    // ---------------------------------------------------------
    // 3) Extract @ ... $$ section
    // ---------------------------------------------------------
    int at  = raw.indexOf('@');
    int end = raw.indexOf("$$");
    if (at < 0 || end < 0 || end <= at) {
        Log(LOG_WARN, "STAT parser: no valid @ ... $$ frame found");
        return PARSE_FAIL;
    }

    // ---------------------------------------------------------
    // 4) Parse lines robustly
    // ---------------------------------------------------------
    int pos = at + 1;
    int safetyCounter = 0;

    while (pos < end && safetyCounter < 200) {

        safetyCounter++;

        int nl = raw.indexOf('\n', pos);
        String line;

        if (nl < 0 || nl >= end) {
            line = trimWS(raw.substring(pos, end));
            pos = end;
        } else {
            line = trimWS(raw.substring(pos, nl));
            pos = nl + 1;
        }

        if (line.length() == 0)
            continue;

        // End of output
        if (line.startsWith("Command completed"))
            break;

        // Key : Value
        int colon = line.indexOf(':');
        String key, value;

        if (colon >= 0) {
            key   = trimWS(line.substring(0, colon));
            value = trimWS(line.substring(colon + 1));
        }
        else {
            // Fallback: last token is value
            int split = line.lastIndexOf(' ');
            if (split > 0) {
                key   = trimWS(line.substring(0, split));
                value = trimWS(line.substring(split));
            }
            else {
                continue;
            }
        }

        if (key.length() == 0)
            continue;

        StatField f;
        f.name = std::move(key);
        f.raw  = std::move(value);
        out.fields.push_back(std::move(f));
    }

    if (safetyCounter >= 200) {
        Log(LOG_ERROR, "STAT parser: safety break triggered (malformed frame)");
        return PARSE_FAIL;
    }

    // ---------------------------------------------------------
    // 5) Store global result (for Web UI)
    // ---------------------------------------------------------
    fillStatSummary(idx, out.fields);

    StatBuffer* target = statUseA ? &statB : &statA;

    // Move into the double buffer (caller does not read `out` afterward). Capture
    // the count first because the moved-from vector reports size 0.
    size_t nFields = out.fields.size();
    target->stat = std::move(out);

    statUseA = !statUseA;


    Log(LOG_INFO, "STAT parser: parsed " + String(nFields) +
                  " fields for module " + String(idx));

    return PARSE_OK;
}
