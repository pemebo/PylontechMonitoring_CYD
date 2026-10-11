#pragma once
#include "config.h"
#include <vector>

// One cell snapshot captured from a BAT frame (web "Cell voltages" /
// "Cell SOCs" tables). Lives in Task 2, fed from the same loop that
// feeds the display CELLS screen.
struct WebCell {
    int volt_mV = 0;   // 0 = no data
    int soc     = -1;  // -1 = no data
};

struct ModuleCellStore {
    bool have = false;
    int  moduleIndex = 0;
    std::vector<WebCell> cells;
};

// Per-module cell store; index = module number (1..MAX_MODULES)
extern std::vector<ModuleCellStore> cellStore;

// Capture one module's cell frame (voltages + SOC per cell).
void updateCellStore(int moduleIndex, const std::vector<BatData>& cells, const std::vector<String>& header);