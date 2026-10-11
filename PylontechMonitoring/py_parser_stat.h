#pragma once
#include <Arduino.h>
#include <vector>
#include "config.h"   // Provides StatField, StatData, ParseResult

// Compact per-module STAT summary (replaces the full statTable copies).
extern StatSummary statSummary[];

// Latest parsed STAT frame (front of the double buffer). Replaces lastParsedStat.
inline const StatData& currentStatData() {
    return statUseA ? statA.stat : statB.stat;
}

// STAT parser function
ParseResult parseStatFrame(int moduleIndex,
                           const String& raw,
                           StatData& out);
