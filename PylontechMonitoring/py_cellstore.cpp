#include "py_cellstore.h"
#include "py_parser_bat.h"

// Per-module cell store; index = module number (1..MAX_MODULES)
std::vector<ModuleCellStore> cellStore;

// Exact field lookup by column name; returns -1 when the field is absent.
static int cellFieldInt(const std::vector<String>& values, const std::vector<String>& header, const char* name) {
    int i = batColumnIndex(header, name);
    return (i >= 0 && i < (int)values.size()) ? values[i].toInt() : -1;
}

void updateCellStore(int moduleIndex, const std::vector<BatData>& cells, const std::vector<String>& header) {
    if (moduleIndex < 1 || moduleIndex > MAX_MODULES) return;
    if (cellStore.size() <= (size_t)MAX_MODULES) cellStore.resize(MAX_MODULES + 1);

    ModuleCellStore &st = cellStore[moduleIndex];
    st.have = true;
    st.moduleIndex = moduleIndex;
    st.cells.clear();
    st.cells.reserve(cells.size());

    for (const auto &c : cells) {
        WebCell wc;

        // Voltage: exact V / Volt / Voltage columns, then the same
        // "V*-prefixed plausible mV" fallback used by the display feed.
        int v = cellFieldInt(c.values, header, "V");
        if (v <= 0) v = cellFieldInt(c.values, header, "Volt");
        if (v <= 0) v = cellFieldInt(c.values, header, "Voltage");
        if (v <= 0) {
            for (size_t k = 0; k < header.size() && k < c.values.size(); k++) {
                if (header[k].startsWith("V") || header[k].startsWith("v")) {
                    int x = c.values[k].toInt();
                    if (x > 0 && x < 5000) { v = x; break; }
                }
            }
        }
        wc.volt_mV = (v > 0) ? v : 0;

        // SOC: exact column only (0 % is a valid value).
        int s = cellFieldInt(c.values, header, "SOC");
        wc.soc = (s >= 0) ? ((s > 100) ? 100 : s) : -1;

        st.cells.push_back(wc);
    }
}