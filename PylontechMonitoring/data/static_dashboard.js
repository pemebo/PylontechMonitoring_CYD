// Top-level declarations use var on purpose: the SPA re-injects and re-executes
// this script on every navigation, and a top-level let/const would throw
// "Identifier already declared" on the second run. var redeclaration is legal
// and keeps the script re-runnable; window.pageCleanup() stops the timer.
var MDI_BATT_CASE = "M16.67,4H15V2H9V4H7.33A1.33,1.33 0 0,0 6,5.33V20.67C6,21.4 6.6,22 7.33,22H16.67A1.33,1.33 0 0,0 18,20.67V5.33C18,4.6 17.4,4 16.67,4Z";
var MDI_BATT_FLASH = "M7,2V13H10V22L17,10H13L17,2H7Z";
var MDI_BATT_ALERT = "M13 14H11V8H13M13 18H11V16H13M16.7 4H15V2H9V4H7.3C6.6 4 6 4.6 6 5.3V20.6C6 21.4 6.6 22 7.3 22H16.6C17.3 22 17.9 21.4 17.9 20.7V5.3C18 4.6 17.4 4 16.7 4Z";

var BATT_COL_OK   = "#43a047";
var BATT_COL_RED  = "#e53935";
var BATT_COL_AMBER= "#ff8f00";
var BATT_COL_RING = "#78909c";
var BATT_COL_GREY = "#bdbdbd";

function setCpuBar(id, load) {
    const el = document.getElementById(id);
    if (!el) return;
    if (load === null || load === undefined || isNaN(load)) {
        el.style.width = "0%";
        return;
    }
    el.style.width = Math.min(100, Math.max(0, load)) + "%";
}

function wifiQuality(rssi) {
    if (rssi === undefined || rssi === null || isNaN(rssi)) return "\u2014";
    if (rssi >= -50) return t("dash.qual_high");
    if (rssi >= -60) return t("dash.qual_medium");
    if (rssi >= -70) return t("dash.qual_low");
    return t("dash.qual_very_low");
}

function formatBytes(bytes) {
    if (bytes === undefined || bytes === null || isNaN(bytes)) return "\u2014";
    if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(1) + " GB";
    if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MB";
    if (bytes >= 1024) return (bytes / 1024).toFixed(1) + " KB";
    return bytes + " B";
}

function setMemBar(id, pct) {
    const el = document.getElementById(id);
    if (!el) return;
    const p = Math.min(100, Math.max(0, pct));
    el.style.width = p + "%";
    el.style.background = p > 90 ? "#e53935" : p > 75 ? "#ff8f00" : "#1e88e5";
}

function batterySvg(soc, status) {
    const hasData = !(soc === null || soc === undefined || isNaN(soc));
    const cold = (status || "").toLowerCase();
    const disconnected = !hasData || cold.includes("disconnected");
    const alarm = cold.includes("alarm");
    const charging = cold === "charging";

    if (disconnected) return '<path d="' + MDI_BATT_ALERT + '" fill="' + BATT_COL_GREY + '"/>';
    if (alarm)        return '<path d="' + MDI_BATT_ALERT + '" fill="' + BATT_COL_RED + '"/>';

    const s = Math.min(100, Math.max(0, soc));
    // Interior of the 24x24 MDI case spans y=4..22 (18 units tall); fill it fully at 100%.
    const fillH = 18.0 * s / 100;
    const fillY = 22 - fillH;
    const fillCol = s < 10 ? BATT_COL_RED : BATT_COL_OK;

    let inner =
        '<defs><clipPath id="battClip"><path d="' + MDI_BATT_CASE + '"/></clipPath></defs>' +
        '<rect x="4" y="' + fillY.toFixed(2) + '" width="16" height="' + fillH.toFixed(2) + '" rx="1" fill="' + fillCol + '" clip-path="url(#battClip)"/>';
    if (charging) inner += '<path d="' + MDI_BATT_FLASH + '" fill="' + BATT_COL_AMBER + '" transform="translate(4.8,4.8) scale(0.6)"/>';

    return '<path d="' + MDI_BATT_CASE + '" fill="none" stroke="' + BATT_COL_RING + '" stroke-width="1.6"/>' + inner;
}

function renderBatteryIcon(soc, status) {
    const svgEl = document.getElementById("batt_svg");
    if (svgEl) {
        svgEl.innerHTML = '<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">' + batterySvg(soc, status) + '</svg>';
    }

    const hasData = !(soc === null || soc === undefined || isNaN(soc));
    const cold = (status || "").toLowerCase();
    const disconnected = !hasData || cold.includes("disconnected");
    const alarm = cold.includes("alarm");
    const s = hasData ? Math.min(100, Math.max(0, soc)) : 0;

    const socEl = document.getElementById("batt_soc");
    if (socEl) {
        socEl.textContent = hasData && !disconnected ? s + " %" : "-- %";
        socEl.style.color = alarm || (hasData && s < 10) ? BATT_COL_RED : BATT_COL_OK;
    }
    const stEl = document.getElementById("batt_status");
    if (stEl) {
        // Display the translated status, but keep the raw English token for the
        // colour rules (alarm/disconnected) so they work in both languages.
        stEl.textContent = statusLabel(status) || "\u2014";
        stEl.style.color = alarm ? BATT_COL_RED : disconnected ? "#9e9e9e" : BATT_COL_RING;
    }
}

function loadDashboard() {
    fetch("/api/dashboard")
        .then(r => r.json())
        .then(d => {
            if (!document.getElementById("batt_svg")) { clearInterval(window.dashTimer); return; }
            wifi_mode.textContent = d.wifi.mode;
            wifi_ssid.textContent = d.wifi.ssid;
            wifi_ip.textContent = d.wifi.ip;
            wifi_rssi.textContent = d.wifi.rssi;
            wifi_quality.textContent = wifiQuality(d.wifi.rssi);
            mqtt_status.textContent =
                !d.mqtt.enabled ? t("layout.mqtt_disabled") :
                d.mqtt.connected ? t("layout.mqtt_active") : t("layout.mqtt_disconnected");
            mqtt_status.style.color =
                d.mqtt.enabled && !d.mqtt.connected ? "#e53935" : "";
            mqtt_server.textContent = d.mqtt.server + ":" + d.mqtt.port;
            mqtt_last.textContent = d.mqtt.last_contact;
            bat_modules.textContent = d.battery.modules;
            bat_last.textContent = d.battery.last_update;

            // Battery Overview
            bat_soc.textContent = d.battery.overview.soc + " %";
            bat_status.textContent = statusLabel(d.battery.overview.status) || "—";
            bat_status.style.color =
                d.battery.overview.status === "Alarm!" ? "#ff4444" : "";
            bat_voltage.textContent = (d.battery.overview.voltage_mV / 1000).toFixed(2) + " V";
            bat_temp.textContent = (d.battery.overview.temperature / 1000).toFixed(1) + " °C";
            bat_current.textContent = (d.battery.overview.current_mA / 1000).toFixed(2) + " A";
            bat_power.textContent = d.battery.overview.powerW + " W";

            const ov = d.battery && d.battery.overview;
            renderBatteryIcon(ov ? ov.soc : null, ov ? ov.status : "Disconnected");

            // Health
            hl_status.textContent = statusLabel(d.health.status);
            hl_min.textContent = d.health.cellMin.toFixed(3) + " V";
            hl_max.textContent = d.health.cellMax.toFixed(3) + " V";
            var maxD = d.health.maxCellDiff;
            hl_delta.textContent = (typeof maxD === "number" ? maxD : d.health.cellDiff).toFixed(3) + " V";
            hl_soh.textContent = d.health.soh !== null ? d.health.soh + " %" : "—";
            hl_events.textContent = d.health.events !== undefined && d.health.events !== null ? d.health.events : 0;

            sys_time.textContent = d.system.time;
            sys_uptime.textContent = d.system.uptime;
            sys_fw.textContent = d.system.version;

            // CPU & Memory
            const cpu = d.system.cpu || {};
            const mem = d.system.mem || {};
            const load0 = typeof cpu.load0 === "number" ? cpu.load0 : null;
            const load1 = typeof cpu.load1 === "number" ? cpu.load1 : null;
            cpu_load0.textContent = load0 !== null ? load0 + " %" : "\u2014";
            cpu_load1.textContent = load1 !== null ? load1 + " %" : "\u2014";
            setCpuBar("cpu_bar0", load0);
            setCpuBar("cpu_bar1", load1);

            if (mem.heap_total !== undefined && mem.heap_total > 0) {
                const heapPct = Math.round(mem.heap_used / mem.heap_total * 100);
                mem_heap_pct.textContent = heapPct + " %";
                setMemBar("mem_heap_bar", heapPct);
            }
            const psramEl = document.getElementById("psram_section");
            if (psramEl && mem.psram_total !== undefined && mem.psram_total > 0) {
                const psramPct = Math.round(mem.psram_used / mem.psram_total * 100);
                psramEl.style.display = "";
                mem_psram_pct.textContent = psramPct + " %";
                setMemBar("mem_psram_bar", psramPct);
            }

            // SD card free space ("No card" when not mounted)
            const sdFreeEl = document.getElementById("mem_sd_free");
            if (sdFreeEl) {
                const sd = d.sdCard || {};
                if (sd.status === "ok") {
                    const freeBytes = (sd.totalBytes || 0) - (sd.usedBytes || 0);
                    sdFreeEl.textContent =
                        formatBytes(freeBytes) + " " + t("dash.free") + " (" + (sd.freeSpacePercent || 0) + " %)";
                } else {
                    sdFreeEl.textContent = t("dash.no_card");
                }
            }
        });
}
loadDashboard();
var dashTimer = setInterval(loadDashboard, 3000);

// SPA navigation hook: stop the dashboard timer when leaving the page.
window.pageCleanup = function () {
    clearInterval(window.dashTimer);
    window.dashTimer = null;
};