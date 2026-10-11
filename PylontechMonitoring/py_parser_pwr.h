#pragma once
#include <Arduino.h>
#include <vector>
#include <map>
#include "config.h"

// ---------------------------------------------------------
// PWR Parser Header
// ---------------------------------------------------------
// - Stack mode: behaves exactly as before
// - Hub mode: supports Hub + Masterhub (Address/Stack/Module)
// - Returns one "overall" BatteryStack via stackOut
// - Exposes detailed hub/stack data via globals
// ---------------------------------------------------------

// Main PWR parser function (entry point)
ParseResult parsePwrFrame(const String& raw,
                          BatteryStack& stackOut,
                          std::vector<BatteryModule>& modulesOut);

// Re-evaluate the health summary from the last PWR frame (no events).
void refreshHealthFromSnapshot();

// Global parser results for Web UI (as before)
extern BatteryStack lastParsedStack;
// Latest parsed PWR modules (front of the double buffer). Replaces lastParsedModules.
inline const std::vector<BatteryModule>& currentPwrModules() {
    return pwrUseA ? pwrA.modules : pwrB.modules;
}
extern std::vector<String> lastParserHeader;
extern std::vector<String> lastParserValues;

// New: detailed hub/stack result
// hubs[hubID][stackID] = BatteryStack
struct ParsedHubData {
    std::map<int, std::map<int, BatteryStack>> hubs;
};

extern ParsedHubData lastParsedHub;
