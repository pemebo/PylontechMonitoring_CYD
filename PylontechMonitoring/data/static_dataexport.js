/* ---------------------------------------------------------
   Data Export Page — JavaScript

   Selection model (driven by /api/sdcard/export/status):
   - The two checkboxes (Module data / Cells data) decide WHAT a data export
     writes, not whether exporting is enabled. The Export button is the
     activation. On the wire the two kinds are still the tokens pwr / bat.
   - One data export session can cover Module data, Cells data or both at
     once; the server reports the covered kinds in the "kinds" array.
   - Only one export can run at a time. The button of the running export kind
     becomes "Stop export"; every other button and every option is disabled.
   - The whole UI is re-derived from the server status on (re-)entry, so
     navigating away and back restores the active/locked state.
   --------------------------------------------------------- */

// Top-level declarations use var on purpose: the SPA re-injects and re-executes
// this script on every navigation, and a top-level let/const would throw
// "Identifier already declared" on the second run (the global lexical binding
// is never cleared). var redeclaration is legal and keeps the code re-runnable.
var EXPORT_KINDS = ["data", "history"];
var EXPORT_ENDPOINTS = {
    data: "/api/sdcard/export/data",
    history: "/api/sdcard/export/history"
};

var CONFIG_IDS = [
    "sd_records_per_file",
    "sd_export_timeout"
];

// i18n key of the user-facing name of every wire token reported in the
// "kinds" array. Resolved with t() at display time, not at load time.
var KIND_DISPLAY_KEYS = {
    pwr: "exp.module_data",
    bat: "exp.cells_data",
    history: "exp.history_btn"
};

// i18n key of a button while nothing of that kind is running.
var EXPORT_LABEL_KEYS = {
    data: "exp.export_btn",
    history: "exp.history_btn"
};

var exportPollInterval = null;
var lastExportStatus = null;
var lastSdConfig = null;
var wasRunning = false;          // for one-shot result alerts
var pageInitialized = false;

function el(id) {
    return document.getElementById(id);
}

/* Which button owns a running session: a data session (PWR and/or BAT) is
   driven by the common Export button, a datalist run by its own button. */
function activeButtonKind(j) {
    if (!j || !j.kinds || !j.kinds.length) return null;
    if (j.kinds.indexOf("history") >= 0) return "history";
    if (j.kinds.indexOf("pwr") >= 0 || j.kinds.indexOf("bat") >= 0) return "data";
    return null;
}

function kindLabel(kinds) {
    if (!kinds || !kinds.length) return "—";
    // The tokens stay pwr/bat/history (firmware + filename contract);
    // only the label uses the vocabulary the checkboxes show.
    return kinds.map(k => t(KIND_DISPLAY_KEYS[k] || k)).join(" + ");
}

/* ---------------------------------------------------------
   Configuration
   --------------------------------------------------------- */
function loadSdConfig() {
    fetch("/api/sdcard/config")
        .then(r => r.json())
        .then(j => {
            lastSdConfig = j;
            // Selection = what a data export will write.
            el("sel_pwr").checked = !!j.selectPwr;
            el("sel_bat").checked = !!j.selectBat;
            el("sd_records_per_file").value = j.recordsPerFile;
            // The field is in seconds, the API stays in milliseconds.
            el("sd_export_timeout").value = Math.round(j.exportTimeoutMs / 1000);
            updateQueryIntervalInfo(j.queryIntervalPwr, j.queryIntervalBat, j.enableBat);
            if (j.sdReady !== undefined) applyStorageState(j.sdReady);
            applyLockState();
        })
        .catch(err => {
            console.error("Failed to load SD config:", err);
        });
}

function updateQueryIntervalInfo(pwrInterval, batInterval, enableBat) {
    el("query_interval_info").textContent =
        t("exp.query_interval_note")
            .replace("{pwr}", pwrInterval)
            .replace("{bat}", batInterval);

    // Only a problem when BAT is actually selected.
    const batSelected = el("sel_bat").checked;
    el("bat_export_warning").style.display = (batSelected && !enableBat) ? "block" : "none";
}

/* ---------------------------------------------------------
   Storage availability
   --------------------------------------------------------- */
function applyStorageState(sdReady) {
    el("no_storage_banner").style.display = sdReady ? "none" : "block";
}

/* ---------------------------------------------------------
   Locking: options + non-active buttons are disabled while exporting
   --------------------------------------------------------- */
function isLocked() {
    return !!(lastExportStatus && lastExportStatus.optionsLocked);
}

function applyLockState() {
    const locked = isLocked();
    const activeKind = locked ? activeButtonKind(lastExportStatus) : null;
    const sdReady = lastExportStatus ? lastExportStatus.sdReady !== false : true;
    const batParserReady = lastSdConfig ? lastSdConfig.enableBat !== false : true;
    const batSelected = el("sel_bat").checked;

    // Every option input of the page
    document.querySelectorAll("[data-export-option]").forEach(input => {
        input.disabled = locked;
    });
    CONFIG_IDS.forEach(id => {
        if (el(id)) el(id).disabled = locked;
    });

    // One button per export kind
    EXPORT_KINDS.forEach(kind => {
        const btn = document.querySelector("[data-export-kind='" + kind + "']");
        if (!btn) return;

        const isActive = locked && activeKind === kind;
        const stopText = (lastExportStatus.buttonText && lastExportStatus.buttonText !== "Stop export")
            ? lastExportStatus.buttonText
            : t("exp.stop_export");
        btn.textContent = isActive ? stopText : t(EXPORT_LABEL_KEYS[kind]);
        btn.style.background = isActive ? "#cc0000" : "";
        btn.disabled = !sdReady || (locked && !isActive) ||
            (kind === "data" && batSelected && !batParserReady);
    });

    el("active_export_hint").style.display = locked ? "block" : "none";
    if (locked) el("active_export_kind").textContent = kindLabel(lastExportStatus.kinds);
}

/* ---------------------------------------------------------
   Start / stop
   --------------------------------------------------------- */
function toggleExport(kind) {
    if (!EXPORT_KINDS.includes(kind)) return;
    if (isLocked() && activeButtonKind(lastExportStatus) === kind) {
        stopExport();
        return;
    }
    startExport(kind);
}

function parseModulesInput(inputId) {
    const val = el(inputId).value.trim();
    if (!val) return [];
    return val.split(",").map(s => parseInt(s.trim())).filter(n => !isNaN(n) && n > 0);
}

function getTimeValue(inputId) {
    const val = el(inputId).value;
    if (!val) return 0;
    // Convert datetime-local to Unix timestamp (seconds)
    const dt = new Date(val);
    return Math.floor(dt.getTime() / 1000);
}

function getModuleNumber() {
    return parseInt(el("module_number").value) || 0;
}

/* The Save Config button was removed, so starting an export is the
   persistence point of the two settings of this page: every start request
   carries them and the firmware saves them (clamped) before the export runs.
   A field the user emptied falls back to the last value read from
   /api/sdcard/config instead of sending NaN. */
function exportSettings() {
    const fallbackRecords = lastSdConfig ? lastSdConfig.recordsPerFile : 1000;
    const fallbackTimeoutMs = lastSdConfig ? lastSdConfig.exportTimeoutMs : 600000;

    const records = parseInt(el("sd_records_per_file").value, 10);
    const timeoutSec = parseInt(el("sd_export_timeout").value, 10);

    return {
        recordsPerFile: isNaN(records) ? fallbackRecords : records,
        // The field is in seconds, the wire and the firmware stay in ms.
        exportTimeoutMs: isNaN(timeoutSec) ? fallbackTimeoutMs : timeoutSec * 1000
    };
}

function startExport(kind) {
    let payload;
    if (kind === "data") {
        // The checkboxes travel with the request: the server persists them as
        // the selection and derives the kinds of the session from them.
        payload = {
            pwr: el("sel_pwr").checked,
            bat: el("sel_bat").checked,
            timeFrom: getTimeValue("data_time_from"),
            timeTo: getTimeValue("data_time_to"),
            modules: parseModulesInput("data_modules")
        };
        if (!payload.pwr && !payload.bat) {
            alert(t("exp.alert_select_kinds"));
            return;
        }
    } else {
        const module = getModuleNumber();
        if (module <= 0) {
            alert(t("exp.alert_module_required"));
            return;
        }
        payload = { module: module };
    }

    // Persisted by the firmware on export start, split by export family:
    // data exports carry the record rotation, BMS exports the timeout.
    const settings = exportSettings();
    if (kind === "data") {
        payload.recordsPerFile = settings.recordsPerFile;
    } else {
        payload.exportTimeoutMs = settings.exportTimeoutMs;
    }

    doExport(EXPORT_ENDPOINTS[kind], payload);
}

function doExport(url, data) {
    fetch(url, {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify(data)
    })
        .then(r => {
            if (!r.ok) {
                return r.text().then(t => { throw new Error(t.trim() || ("HTTP " + r.status)); });
            }
            // Arm the result alert before polling: a datalist export is one-shot
            // and can already be finished by the time the first poll lands.
            wasRunning = true;
            el("progress_section").style.display = "block";
            // This start request is what persisted the two settings, so re-read
            // them: the firmware clamps, and the progress bar sizes itself from
            // lastSdConfig. Without this the page keeps showing the pre-edit
            // values (and a clamped value is never echoed back).
            loadSdConfig();
            pollExportStatus();
            startStatusPoll();
        })
        .catch(err => {
            wasRunning = false;
            alert(t("exp.alert_export_failed") + err.message);
        });
}

function stopExport() {
    fetch("/api/sdcard/export/stop", { method: "POST" })
        .then(r => {
            if (!r.ok) return r.text().then(t => { throw new Error(t.trim()); });
            pollExportStatus();
        })
        .catch(err => {
            alert(t("exp.alert_stop_failed") + err.message);
        });
}

/* ---------------------------------------------------------
   Status polling
   --------------------------------------------------------- */
function startStatusPoll() {
    stopStatusPoll();
    exportPollInterval = setInterval(pollExportStatus, 1000);
}

function stopStatusPoll() {
    if (exportPollInterval) {
        clearInterval(exportPollInterval);
        exportPollInterval = null;
    }
}

function pollExportStatus() {
    return fetch("/api/sdcard/export/status")
        .then(r => r.json())
        .then(j => {
            lastExportStatus = j;
            applyStorageState(j.sdReady !== false);
            applyLockState();
            updateProgressUI(j);
            handleStateChange(j);
        })
        .catch(err => {
            console.error("Status poll error:", err);
        });
}

function handleStateChange(j) {
    // "stopped" is the firmware's terminal state after a user-requested stop
    // (finishExport() is not followed by another transition), so only
    // "running" (writing rows) and "scheduled" (waiting for its start time)
    // mean the session still owns the export.
    if (j.state === "running" || j.state === "scheduled") {
        wasRunning = true;
        el("progress_section").style.display = "block";
        return;
    }

    // Terminal state: only report a transition that was observed, so a page
    // reload does not replay the result of an earlier export.
    if (wasRunning) {
        if (j.state === "completed") {
            alert(t("exp.alert_completed")
                .replace("{kinds}", kindLabel(j.kinds))
                .replace("{n}", j.filesCreated));
        } else if (j.state === "stopped") {
            alert(t("exp.alert_stopped"));
        } else if (j.state === "error") {
            alert(t("exp.alert_export_failed") + (j.error || t("exp.see_device_log")));
        } else if (j.state === "timeout") {
            alert(t("exp.alert_timeout")
                .replace("{n}", Math.round((lastSdConfig ? lastSdConfig.exportTimeoutMs : 600000) / 1000)));
        }
    }

    wasRunning = false;
    stopStatusPoll();
    // Keep the section visible with the final result instead of hiding it.
    if (j.state !== "idle") el("progress_section").style.display = "block";
}

function updateProgressUI(j) {
    if (j.state === "scheduled") {
        let when = "";
        if (j.scheduledFrom) {
            when = ": " + new Date(j.scheduledFrom * 1000).toLocaleString();
        }
        el("progress_state").textContent = t("exp.state_scheduled").replace("{when}", when);
    } else {
        el("progress_state").textContent = j.state;
    }
    el("progress_kinds").textContent = kindLabel(j.kinds);
    el("progress_file_count").textContent = j.filesCreated;
    el("progress_record_count").textContent = j.recordsInCurrentFile;
    el("progress_filename").textContent = j.currentFile || "—";

    const errorEl = el("progress_error");
    if (j.error) {
        errorEl.style.display = "block";
        el("progress_error_text").textContent = j.error;
    } else {
        errorEl.style.display = "none";
    }
}

/* ---------------------------------------------------------
   Page lifecycle
   --------------------------------------------------------- */
function initDataExportPage() {
    if (pageInitialized) return;
    if (!el("btn_data_export")) return;      // markup not in the DOM yet
    pageInitialized = true;

    loadSdConfig();

    // Restore the active/locked state right away when re-entering the page.
    // pageCleanup() stops the interval on SPA navigation, so resume it when
    // the first status poll lands on an active (running or scheduled) export.
    pollExportStatus().then(() => {
        if (lastExportStatus && (lastExportStatus.state === "running" || lastExportStatus.state === "scheduled")) {
            startStatusPoll();
        }
    });

    // The BAT warning only matters while BAT is selected.
    el("sel_bat").addEventListener("change", () => {
        updateQueryIntervalInfo(
            lastSdConfig ? lastSdConfig.queryIntervalPwr : 0,
            lastSdConfig ? lastSdConfig.queryIntervalBat : 0,
            lastSdConfig ? lastSdConfig.enableBat : true);
        applyLockState();
    });
}

// The script is injected by layout.html *after* the markup is in the DOM, so
// DOMContentLoaded has already fired in that case and the init runs directly.
if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initDataExportPage);
} else {
    initDataExportPage();
}

window.pageCleanup = function () {
    stopStatusPoll();
    wasRunning = false;
    pageInitialized = false;
};
