#include "py_cellstats.h"

void updateCellStats(int moduleIndex, int frameMin_mV, int frameMax_mV,
                     int minCell, int maxCell, bool withCells) {
    if (moduleIndex < 1 || moduleIndex > MAX_MODULES) return;
    if (frameMin_mV <= 0 || frameMax_mV <= 0) return;
    if (frameMax_mV < frameMin_mV) return;

    CellDayStats &st = cellDayStats[moduleIndex];

    // Midnight rollover: reset this module's "today" values when the
    // calendar day changes. Guarded by valid time (unsynced clock never
    // wipes against the 1970 epoch).
    int dayKey = config.getCurrentDayKey();
    if (dayKey >= 0 && st.dayKey != dayKey) {
        st = CellDayStats();
        st.dayKey = dayKey;
    }

    // Instantaneous (last frame)
    st.lastMin_mV = frameMin_mV;
    st.lastMax_mV = frameMax_mV;
    st.lastDelta_mV = frameMax_mV - frameMin_mV;
    st.lastMinCell = withCells ? minCell : 0;
    st.lastMaxCell = withCells ? maxCell : 0;

    if (dayKey < 0) return;  // no valid day -> only the instant values

    int nowMin = -1;
    if (withCells) nowMin = config.getCurrentMinutesOfDay();

    // Daily max
    if (frameMax_mV > st.todayMax_mV) {
        st.todayMax_mV = frameMax_mV;
        st.todayMaxCell = withCells ? maxCell : 0;
        if (withCells) st.todayMaxMin = nowMin;
    }

    // Daily min
    if (st.todayMin_mV == -1 || frameMin_mV < st.todayMin_mV) {
        st.todayMin_mV = frameMin_mV;
        st.todayMinCell = withCells ? minCell : 0;
        if (withCells) st.todayMinMin = nowMin;
    }

    // Daily max delta
    int delta = frameMax_mV - frameMin_mV;
    if (delta > st.todayDelta_mV) {
        st.todayDelta_mV = delta;
        if (withCells) st.todayDeltaMin = nowMin;
    }
}