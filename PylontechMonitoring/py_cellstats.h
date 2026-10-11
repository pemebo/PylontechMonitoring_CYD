#pragma once
#include "config.h"

// Update a module's instant + daily cell extremes from one frame.
// frameMin_mV / frameMax_mV: this frame's min/max cell voltage.
// minCell / maxCell: 1-based cell numbers; 0 when unknown (PWR mode).
// withCells=false -> PWR mode: only values are captured, no cell number
// and no capture time.
void updateCellStats(int moduleIndex, int frameMin_mV, int frameMax_mV,
                     int minCell, int maxCell, bool withCells);