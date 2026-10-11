// ---------------------------------------------------------
// Per-module view (mirrors the TFT MODULES screen)
// ---------------------------------------------------------
function fmtTemp(mC) {
    if (mC === null || mC === undefined || isNaN(mC)) return "--";
    return (mC / 1000).toFixed(1) + " °C";
}

function fmtNum(v, dec) {
    if (v === null || v === undefined || isNaN(v)) return "--";
    return v.toFixed(dec);
}

function fmtStat(v) {
    if (v === null || v === undefined || v === -1 || isNaN(v)) return "--";
    return v;
}

// ---------------------------------------------------------
// Page-owned refresh state
// ---------------------------------------------------------
if (typeof window.pageCleanup === "function") {
    window.pageCleanup();
}

window.__modulesState = {
    active: true,
    modulesTimer: null,
    statisticsTimer: null,
    cellsTimer: null,
    cellBatchTimer: null,
    statReloadTimer: null,
    cellsReloadTimer: null,
    cellBatchRequestInFlight: false,
    cellBatchAwaitingCompletion: false,
    autoRefreshEnabled: false,
    autoRefreshStarted: false,
    cellIntervalMs: 1000,
    visibilityHandler: null,
    cleanupHook: null
};

(function (state) {
    state.cleanupHook = function () {
        cleanupModulesPage(state);
    };
    window.pageCleanup = state.cleanupHook;
})(window.__modulesState);

function isModulesStateActive(state) {
    return !!(state && state.active && window.__modulesState === state);
}

function isModulesStateUsable(state) {
    return isModulesStateActive(state) && !document.hidden;
}

function clearModulesInterval(state, key) {
    if (state[key] !== null && state[key] !== undefined) {
        clearInterval(state[key]);
        state[key] = null;
    }
}

function clearModulesTimeout(state, key) {
    if (state[key] !== null && state[key] !== undefined) {
        clearTimeout(state[key]);
        state[key] = null;
    }
}

function stopModulesAutoRefresh(state) {
    state.autoRefreshEnabled = false;
    state.autoRefreshStarted = false;
    state.cellBatchAwaitingCompletion = false;
    clearModulesTimeout(state, "cellBatchTimer");
}

function stopModulesRenderTimers(state) {
    clearModulesInterval(state, "modulesTimer");
    clearModulesInterval(state, "statisticsTimer");
    clearModulesInterval(state, "cellsTimer");
}

function stopModulesPageTimers(state) {
    stopModulesRenderTimers(state);
    clearModulesTimeout(state, "cellBatchTimer");
    clearModulesTimeout(state, "statReloadTimer");
    clearModulesTimeout(state, "cellsReloadTimer");
}

function startModulesRenderTimers(state) {
    stopModulesRenderTimers(state);
    if (!isModulesStateUsable(state)) return;

    state.modulesTimer = setInterval(function () {
        if (isModulesStateUsable(state)) loadModules(state);
    }, 2000);
    state.statisticsTimer = setInterval(function () {
        if (isModulesStateUsable(state)) loadStatistics(state);
    }, 4000);
    state.cellsTimer = setInterval(function () {
        if (isModulesStateUsable(state)) loadCells(state);
    }, 4000);
}

function cleanupModulesPage(state) {
    if (!state) return;

    state.active = false;
    stopModulesPageTimers(state);
    state.cellBatchRequestInFlight = false;
    state.cellBatchAwaitingCompletion = false;

    if (state.visibilityHandler) {
        document.removeEventListener("visibilitychange", state.visibilityHandler);
        state.visibilityHandler = null;
    }

    if (window.__modulesState === state) {
        window.__modulesState = null;
    }
    if (window.pageCleanup === state.cleanupHook) {
        window.pageCleanup = null;
    }
}

function handleModulesVisibility(state) {
    if (!isModulesStateActive(state)) return;

    if (document.hidden) {
        stopModulesPageTimers(state);
        return;
    }

    startModulesRenderTimers(state);
    if (!state.autoRefreshStarted) {
        loadCells(state);
    } else if (state.autoRefreshEnabled) {
        scheduleCellBatch(state, 0);
    }
}

function normalizeBatInterval(value) {
    const interval = Number(value);
    if (!isFinite(interval) || interval <= 0) return 1000;
    return Math.max(1000, interval);
}

function scheduleCellBatch(state, delayMs) {
    clearModulesTimeout(state, "cellBatchTimer");
    if (!isModulesStateUsable(state) || !state.autoRefreshEnabled) return;

    const requested = Number(delayMs);
    const delay = Math.max(1000, isFinite(requested) ? requested : state.cellIntervalMs);
    state.cellBatchTimer = setTimeout(function () {
        state.cellBatchTimer = null;
        checkCellBatch(state);
    }, delay);
}

function requestCellBatch(state) {
    if (!isModulesStateUsable(state) || !state.autoRefreshEnabled ||
        state.cellBatchRequestInFlight) {
        return Promise.resolve(false);
    }

    state.cellBatchRequestInFlight = true;
    return fetch("/api/cells/load", { method: "POST" })
        .then(function (response) {
            if (response.status === 400) {
                return { accepted: false, stop: true, retryMs: 0 };
            }
            if (response.status === 409) {
                return response.text().then(function (text) {
                    return {
                        accepted: false,
                        stop: text.trim() === "BAT polling is managed by the scheduler",
                        retryMs: 1000
                    };
                });
            }
            if (!response.ok) {
                throw new Error(t("mod.err_bat_load") + response.status);
            }
            return response.text().then(function (text) {
                return {
                    accepted: text.trim() === "OK",
                    stop: false,
                    retryMs: 1000
                };
            });
        })
        .catch(function () {
            return { accepted: false, stop: false, retryMs: 1000 };
        })
        .then(function (result) {
            state.cellBatchRequestInFlight = false;
            if (!isModulesStateUsable(state)) return result.accepted;

            if (result.stop) {
                stopModulesAutoRefresh(state);
            } else if (result.accepted) {
                state.cellBatchAwaitingCompletion = true;
                clearModulesTimeout(state, "cellsReloadTimer");
                state.cellsReloadTimer = setTimeout(function () {
                    state.cellsReloadTimer = null;
                    if (isModulesStateUsable(state)) loadCells(state);
                }, 1500);
                scheduleCellBatch(state, 1000);
            } else {
                scheduleCellBatch(state, result.retryMs);
            }
            return result.accepted;
        });
}

function checkCellBatch(state) {
    if (!isModulesStateUsable(state) || !state.autoRefreshEnabled) return;
    if (state.cellBatchRequestInFlight) {
        scheduleCellBatch(state, 1000);
        return;
    }

    fetch("/api/cells")
        .then(function (response) {
            if (!response.ok) throw new Error(t("mod.err_cell_read") + response.status);
            return response.json();
        })
        .then(function (data) {
            if (!isModulesStateUsable(state)) return;
            if (data.enableBat !== false || !(data.modules || []).length) {
                stopModulesAutoRefresh(state);
                return;
            }

            state.autoRefreshEnabled = true;
            state.cellIntervalMs = normalizeBatInterval(data.intervalBat);
            if (state.cellBatchAwaitingCompletion) {
                if (data.batLoading) {
                    scheduleCellBatch(state, 1000);
                } else {
                    state.cellBatchAwaitingCompletion = false;
                    clearModulesTimeout(state, "cellsReloadTimer");
                    loadCells(state);
                    scheduleCellBatch(state, state.cellIntervalMs);
                }
            } else if (data.batLoading) {
                scheduleCellBatch(state, 1000);
            } else {
                requestCellBatch(state);
            }
        })
        .catch(function () {
            scheduleCellBatch(state, 1000);
        });
}

function updateAutoCellRefresh(state, data) {
    if (data.enableBat !== false || !(data.modules || []).length) {
        stopModulesAutoRefresh(state);
        return;
    }

    state.autoRefreshEnabled = true;
    state.cellIntervalMs = normalizeBatInterval(data.intervalBat);
    if (!state.autoRefreshStarted) {
        state.autoRefreshStarted = true;
        requestCellBatch(state);
    }
}

function loadStatistics(state) {
    if (!isModulesStateUsable(state)) return;

    fetch("/api/statistics")
        .then(function (response) {
            if (!response.ok) throw new Error(t("mod.err_statistics_read") + response.status);
            return response.json();
        })
        .then(function (d) {
            if (!isModulesStateUsable(state)) return;
            const header = document.getElementById("statistics_header");
            if (header) {
                header.innerHTML = "";
                if (!d.enableStat) {
                    header.innerHTML =
                        '<span style="color:#ff8f00;">' + t("mod.stat_disabled") + '</span> ' +
                        '<button onclick="loadStatNow()">' + t("mod.refresh") + '</button>';
                }
            }

            const tbody = document.querySelector("#stats_table tbody");
            if (!tbody) return;
            tbody.innerHTML = "";

            (d.modules || []).forEach(function (m) {
                const row = document.createElement("tr");
                row.innerHTML =
                    "<td>M" + m.index + "</td>" +
                    "<td>" + fmtStat(m.soh) + " %</td>" +
                    "<td>" + fmtStat(m.cycles) + "</td>" +
                    "<td>" + (m.capAh >= 0 ? m.capAh.toFixed(1) : "--") + " Ah</td>" +
                    "<td>" + fmtStat(m.ov) + "</td>" +
                    "<td>" + fmtStat(m.hv) + "</td>" +
                    "<td>" + fmtStat(m.lv) + "</td>" +
                    "<td>" + fmtStat(m.uv) + "</td>" +
                    "<td>" + fmtStat(m.lifeWarn) + "</td>" +
                    "<td>" + fmtStat(m.lifeAlarm) + "</td>";
                tbody.appendChild(row);
            });
        })
        .catch(function () {});
}

function loadStatNow(state) {
    state = state || window.__modulesState;
    if (!isModulesStateUsable(state)) return;

    fetch("/api/statistics/load", { method: "POST" })
        .then(function (response) {
            if (!response.ok) throw new Error(t("mod.err_stat_load") + response.status);
            return response.text();
        })
        .then(function (text) {
            if (text.trim() !== "OK" || !isModulesStateUsable(state)) return;
            clearModulesTimeout(state, "statReloadTimer");
            state.statReloadTimer = setTimeout(function () {
                state.statReloadTimer = null;
                if (isModulesStateUsable(state)) loadStatistics(state);
            }, 1500);
        })
        .catch(function () {});
}

function loadModules(state) {
    if (!isModulesStateUsable(state)) return;

    fetch("/api/modules")
        .then(function (response) {
            if (!response.ok) throw new Error(t("mod.err_modules_read") + response.status);
            return response.json();
        })
        .then(function (d) {
            if (!isModulesStateUsable(state)) return;
            const st = d.stack || {};

            const header = document.getElementById("modules_header");
            if (header) {
                const stStatus = statusLabel(st.status) || "—";
                header.innerHTML =
                    t("mod.hdr_batt_soc") + " <b>" + (isNaN(st.soc) ? "--" : st.soc) + " %</b>" +
                    " &nbsp; " + t("mod.hdr_status") + " <b>" + stStatus + "</b>" +
                    " &nbsp; " + t("mod.hdr_voltage") + " <b>" + fmtNum(st.voltage_mV / 1000, 2) + " V</b>" +
                    " &nbsp; " + t("mod.hdr_current") + " <b>" + fmtNum(st.current_mA / 1000, 2) + " A</b>" +
                    " &nbsp; " + t("mod.hdr_modules") + " <b>" + (isNaN(st.batteryCount) ? "--" : st.batteryCount) + "</b>" +
                    (st.last_update ? " &nbsp; " + t("mod.hdr_update") + " " + st.last_update : "");
            }

            const tbody = document.querySelector("#modules_table tbody");
            if (!tbody) return;
            tbody.innerHTML = "";

            (d.modules || []).forEach(function (m) {
                const baseSt = statusLabel(m.baseSt) || "--";
                const row = document.createElement("tr");
                row.innerHTML =
                    "<td>M" + m.index + "</td>" +
                    "<td>" + baseSt + "</td>" +
                    "<td>" + m.soc + " %</td>" +
                    "<td>" + (m.voltage_mV / 1000).toFixed(2) + " V</td>" +
                    "<td>" + (m.current_mA / 1000).toFixed(2) + " A</td>" +
                    "<td>" + fmtTemp(m.temperature_mC) + "</td>" +
                    "<td>" + (m.cellDiff >= 0 ? m.cellDiff.toFixed(3) + " V" : "--") + "</td>";
                tbody.appendChild(row);
            });
        })
        .catch(function () {});
}

// ---------------------------------------------------------
// Cell voltages + Cell SOCs (one column per cell C1..CN)
// ---------------------------------------------------------
function renderCellsTable(tblId, d, mode, state) {
    if (state && !isModulesStateUsable(state)) return;

    const tbl = document.getElementById(tblId);
    if (!tbl) return;
    const thead = tbl.querySelector("thead");
    const tbody = tbl.querySelector("tbody");
    if (!thead || !tbody) return;
    tbody.innerHTML = "";

    let maxCells = 0;
    (d.modules || []).forEach(function (m) {
        const n = Math.max((m.voltages || []).length, (m.socs || []).length);
        if (n > maxCells) maxCells = n;
    });

    let head = "<tr><th>" + t("mod.module") + "</th>";
    for (let i = 1; i <= maxCells; i++) head += "<th>C" + i + "</th>";
    head += "</tr>";
    thead.innerHTML = head;

    (d.modules || []).forEach(function (m) {
        const row = document.createElement("tr");
        let html = "<td>M" + m.index + "</td>";

        if (mode === "V") {
            // Same logic as the TFT CELLS screen: highlight the lowest cell
            // of the module in blue and the highest in red (TFT_RED / 0x001F).
            // Ties: first cell reaching the value wins; none when all equal.
            let fMax = -1, fMin = -1, maxIdx = -1, minIdx = -1;
            const volts = m.voltages || [];
            for (let i = 0; i < volts.length; i++) {
                const v = volts[i];
                if (v <= 0) continue;
                if (fMax === -1 || v > fMax) { fMax = v; maxIdx = i; }
                if (fMin === -1 || v < fMin) { fMin = v; minIdx = i; }
            }
            const allSame = (fMax >= 0 && fMax === fMin);

            for (let i = 0; i < maxCells; i++) {
                const v = volts[i];
                let style = "";
                if (v > 0 && !allSame) {
                    if (i === maxIdx)      style = ' style="color:#ff0000; font-weight:bold;"';
                    else if (i === minIdx) style = ' style="color:#0000ff; font-weight:bold;"';
                }
                html += "<td" + style + ">" + (v > 0 ? (v / 1000).toFixed(3) + " V" : "—") + "</td>";
            }
        } else {
            const socs = m.socs || [];
            for (let i = 0; i < maxCells; i++) {
                const s = socs[i];
                html += "<td>" + (s !== undefined && s >= 0 ? s + " %" : "—") + "</td>";
            }
        }

        row.innerHTML = html;
        tbody.appendChild(row);
    });
}

function loadCells(state) {
    if (!isModulesStateUsable(state)) return;

    fetch("/api/cells")
        .then(function (response) {
            if (!response.ok) throw new Error(t("mod.err_cells_read") + response.status);
            return response.json();
        })
        .then(function (d) {
            if (!isModulesStateUsable(state)) return;

            const header = document.getElementById("cell_header");
            if (header) {
                header.innerHTML = "";
                if (d.enableBat === false) {
                    header.innerHTML =
                        '<span style="color:#ff8f00;">' + t("mod.cells_interval_hint") + '</span>';
                }
            }
            renderCellsTable("cell_volt_table", d, "V", state);
            renderCellsTable("cell_soc_table", d, "SOC", state);
            updateAutoCellRefresh(state, d);
        })
        .catch(function () {});
}

window.loadModules = loadModules;
window.loadStatistics = loadStatistics;
window.loadStatNow = loadStatNow;
window.loadCells = loadCells;

(function (state) {
    state.visibilityHandler = function () {
        handleModulesVisibility(state);
    };
    document.addEventListener("visibilitychange", state.visibilityHandler);
    startModulesRenderTimers(state);
    if (isModulesStateUsable(state)) {
        loadModules(state);
        loadStatistics(state);
        loadCells(state);
    }
})(window.__modulesState);
