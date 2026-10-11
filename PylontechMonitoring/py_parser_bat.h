#pragma once
#include <Arduino.h>
#include <vector>
#include "config.h"

// Global BAT data for web UI (optional)
extern std::vector<BatData> lastParsedBatCells;

// Front BAT buffer (double-buffer front): header + cells.
inline const std::vector<String>& currentBatHeader() { return batUseA ? batA.header : batB.header; }
inline const std::vector<BatData>& currentBatCells()  { return batUseA ? batA.cells  : batB.cells; }

// Index of a column in a BAT header, or -1 when absent.
inline int batColumnIndex(const std::vector<String>& header, const char* name) {
    for (size_t i = 0; i < header.size(); i++) if (header[i] == name) return (int)i;
    return -1;
}

// BAT parser function.
// No BatData& out parameter on purpose: the caller never read the value, so
// filling it forced a second full deep copy of the first cell for nothing. The
// frame is published through the BAT double buffer (header + cells).
ParseResult parseBatFrame(int moduleIndex,
                          const String& raw);
