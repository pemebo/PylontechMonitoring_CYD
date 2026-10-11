#include "py_parser_bat.h"
#include "py_log.h"
#include "py_uart.h"
#include <utility>   // std::move — avoid deep-copy heap churn (bad_alloc fix)

extern PyUart py_uart;

// Global BAT data (for Web UI)
std::vector<BatData> lastParsedBatCells;

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
// An empty, sign-only or non-numeric value is treated as "absent", never
// out-of-range. A lone "-" (placeholder) must be absent, not the number 0.
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
// Split columns by 2+ spaces
// ---------------------------------------------------------
static std::vector<String> splitColumns(const String& line) {
    std::vector<String> out;
    int len = line.length();
    int start = 0;

    while (start < len) {
        while (start < len && line[start] == ' ') start++;
        if (start >= len) break;

        int end = start;
        int spaceCount = 0;

        while (end < len) {
            char c = line[end];
            if (c == ' ') {
                spaceCount++;
                if (spaceCount >= 2) break;
            } else {
                spaceCount = 0;
            }
            end++;
        }

        String col = line.substring(start, end);
        col.trim();
        if (col.length() > 0) out.push_back(col);

        start = end;
    }

    return out;
}

// ---------------------------------------------------------
// Main BAT parser
// ---------------------------------------------------------
ParseResult parseBatFrame(int /*moduleIndex*/,
                          const String& raw)
{
    lastParsedBatCells.clear();

    // ---------------------------------------------------------
    // 1) Check if this frame belongs to BAT
    // ---------------------------------------------------------
    String last = py_uart.getLastCommand();
    String lastLower = last;
    lastLower.trim();
    lastLower.toLowerCase();

    if (!lastLower.startsWith("bat")) {
        Log(LOG_DEBUG, "BAT parser: ignoring frame (last command was '" + last + "')");
        return PARSE_IGNORED;
    }

    // Extract module index from command (bat1, bat2, ...)
    String num = lastLower.substring(3);
    num.trim();
    int moduleIdx = num.toInt();

    if (moduleIdx < 1 || moduleIdx > 16) {
        Log(LOG_WARN, "BAT parser: invalid module index '" + last + "'");
        return PARSE_IGNORED;
    }

    // ---------------------------------------------------------
    // 2) Validate frame
    // ---------------------------------------------------------
    if (!py_uart.isFrameValid()) {
        Log(LOG_WARN, "BAT parser: skipping invalid frame");
        return PARSE_FAIL;
    }

    Log(LOG_INFO, "BAT parser: raw frame received for module " + String(moduleIdx));

    // ---------------------------------------------------------
    // 3) Locate the @ ... $$ section, then split it into lines
    // ---------------------------------------------------------
    // Work directly on `raw` by index. The previous code built a ~2.3 KB
    // `frame = raw.substring(...)` plus another copy per replace("\r\n",...).
    // At this point the largest usable 8-bit block is ~2.1 KB, so that copy
    // either failed outright (want=2308) or consumed the only large hole and
    // starved the following cell.values.reserve(11). Indexing allocates only
    // the per-line trimmed Strings, exactly as before.
    //
    // `\r` needs no normalising here: trimWS() removes it, so a CRLF-terminated
    // line yields the same trimmed tokens as the old replace()+trim() sequence.
    int at  = raw.indexOf('@');
    int end = raw.indexOf("$$");
    if (at < 0 || end < 0 || end <= at) {
        Log(LOG_WARN, "BAT parser: no valid @ ... $$ frame found");
        return PARSE_FAIL;
    }

    // ---------------------------------------------------------
    // 4) Split into lines
    // ---------------------------------------------------------
    std::vector<String> lines;
    {
        int pos = at + 1;
        while (pos < end) {
            int nl = raw.indexOf('\n', pos);
            if (nl < 0 || nl >= end) {
                String lastLine = trimWS(raw.substring(pos, end));
                if (lastLine.length() > 0) lines.push_back(std::move(lastLine));
                break;
            }
            String line = trimWS(raw.substring(pos, nl));
            if (line.length() > 0) lines.push_back(std::move(line));
            pos = nl + 1;
        }
    }

    if (lines.size() < 2) {
        Log(LOG_WARN, "BAT parser: too few lines");
        return PARSE_FAIL;
    }

    // ---------------------------------------------------------
    // 5) Header
    // ---------------------------------------------------------
    std::vector<String> header = splitColumns(lines[0]);
    if (header.empty()) {
        Log(LOG_WARN, "BAT parser: empty header");
        return PARSE_FAIL;
    }

    // ---------------------------------------------------------
    // 6) Parse cell rows
    // ---------------------------------------------------------
    lastParsedBatCells.reserve(lines.size());
    for (int row = 1; row < (int)lines.size(); row++) {
        const String& line = lines[row];

        // Stop at non-numeric first token
        String firstToken = line;
        firstToken.trim();
        int spacePos = firstToken.indexOf(' ');
        if (spacePos > 0)
            firstToken = firstToken.substring(0, spacePos);

        if (firstToken.length() == 0)
            continue;

        if (!isDigit(firstToken[0])) {
            break; // end of data
        }

        std::vector<String> cols = splitColumns(line);
        if (cols.empty()) continue;

        size_t count = min(header.size(), cols.size());

        BatData cell;
        cell.cellIndex = row - 1;
        cell.moduleIndex = moduleIdx;
        cell.values.reserve(count);
        for (size_t c = 0; c < count; c++) {
            cell.values.push_back(std::move(cols[c]));
        }

        lastParsedBatCells.push_back(std::move(cell));
    }

    // ---------------------------------------------------------
    // 6b) Frame-level cell-voltage gate
    // ---------------------------------------------------------
    // A single cell outside the plausible LFP window means the frame is
    // misparsed/corrupt, so the whole frame is discarded. The matching header
    // field is "Volt" (exact match; never "Volt. State"). A missing or
    // non-numeric value is treated as absent, not as out-of-range.
    int badCell   = -1;
    int badCellMv = 0;

    int voltIdx = batColumnIndex(header, "Volt");
    for (const auto &cell : lastParsedBatCells) {
        if (voltIdx < 0 || voltIdx >= (int)cell.values.size()) continue;
        const String &volt = cell.values[voltIdx];
        if (!isIntegerToken(volt)) continue;

        int mv = volt.toInt();
        if (mv < BAT_CELL_V_MIN_MV || mv > BAT_CELL_V_MAX_MV) {
            badCell   = cell.cellIndex + 1;   // 1-based cell number
            badCellMv = mv;
            break;
        }
    }

    if (badCell > 0) {
        Log(LOG_DEBUG, "BAT parser: rejecting frame for module " + String(moduleIdx) +
                       ", cell " + String(badCell) + " voltage " + String(badCellMv) +
                       " mV outside " + String(BAT_CELL_V_MIN_MV) + "-" +
                       String(BAT_CELL_V_MAX_MV) + " mV");
        lastParsedBatCells.clear();
        return PARSE_RANGE_INVALID;
    }

    // ---------------------------------------------------------
    // 7) Publish to the BAT double buffer
    // ---------------------------------------------------------
    BatBuffer* target = batUseA ? &batB : &batA;
    target->header = std::move(header);
    // Publish the module index with the frame, before the toggle: consumers then
    // read a single, coherent (module, cells) pair instead of a shared index that
    // can still point to the previous module.
    target->moduleIndex = moduleIdx;

    // Move, not deep-copy: copying vector<BatData> (each holding a vector<String>)
    // needs ~200 small heap allocations at once. When the other task
    // transiently grabs the large contiguous heap block, one of those throws
    // std::bad_alloc and the whole frame is lost (empty cell data). Moving transfers
    // the already-built buffer with zero allocation. Capture the count first because
    // the moved-from vector reports size 0.
    size_t nCells = lastParsedBatCells.size();
    target->cells = std::move(lastParsedBatCells);

    batUseA = !batUseA;
    batFrameSeq++;


    Log(LOG_INFO, "BAT parser: parsed " + String(nCells) +
                  " cells for module " + String(moduleIdx));

    return PARSE_OK;
}
