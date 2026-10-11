// ---------------------------------------------------------
// Auto-Reload Control
// ---------------------------------------------------------
// var (not let): the SPA re-executes this script on every navigation and a
// top-level let would throw "Identifier already declared" on the second run.
// It must stay a global property too: the page uses
// oninput="healthAutoReload = false" in inline markup.
var healthAutoReload = true;

// Writing through the browser's native Basic-auth dialog:
// the first protected call returns 401 + WWW-Authenticate, the browser
// shows a single Username/Password dialog, caches the credentials for the
// session and auto-retries. Cancel → the request fails → no action taken.

// ---------------------------------------------------------
// Today-Column Formatting
// ---------------------------------------------------------
function fmtToday(value, cell, time) {
    if (value === null || value === undefined || value < 0) return "--";
    let s = value.toFixed(3) + "V";
    if (time) s += " (c" + (cell || "?") + " " + time + ")";
    return s;
}

function fmtDeltaToday(value, time) {
    if (value === null || value === undefined || value < 0) return "--";
    let s = value.toFixed(3) + "V";
    if (time) s += " (" + time + ")";
    return s;
}


// ---------------------------------------------------------
// Main function: Load Health
// ---------------------------------------------------------
function loadHealth() {
    fetch("/api/health")
        .then(r => r.json())
        .then(data => {

            // Header
            let h = document.getElementById("health_header");
            h.innerText = t("health.prefix") + statusLabel(data.strongest);
            h.style.background =
                data.color === "green" ? "#00cc00" :
                data.color === "yellow" ? "#ffcc00" :
                "#ff4444";

            // Module
            let tbody = document.querySelector("#health_table tbody");
            tbody.innerHTML = "";

            data.modules.forEach(m => {
                let row = document.createElement("tr");
                row.innerHTML = `
                    <td>${m.index}</td>
                    <td>${statusLabel(m.status)}</td>
                    <td>${m.tempMax.toFixed(2)}°C</td>
                    <td>${m.cellMin.toFixed(3)}V</td>
                    <td>${m.cellMax.toFixed(3)}V</td>
                    <td>${m.cellDiff.toFixed(3)}V</td>
                    <td>${fmtToday(m.todayMin, m.todayMinCell, m.todayMinTime)}</td>
                    <td>${fmtToday(m.todayMax, m.todayMaxCell, m.todayMaxTime)}</td>
                    <td>${fmtDeltaToday(m.deltaToday, m.deltaTodayTime)}</td>
                `;
                tbody.appendChild(row);
            });

            // History events (Date | Module | Event)
            let etbody = document.querySelector("#event_table tbody");
            etbody.innerHTML = "";

            (data.events || []).forEach(e => {
                let row = document.createElement("tr");
                row.innerHTML = `
                    <td>${e.date}</td>
                    <td>${e.module}</td>
                    <td>${eventLabel(e.event)}</td>
                `;
                etbody.appendChild(row);
            });

            // Display threshold values
            if (data.config) {
                document.getElementById("cellDiffWarn").value  =
                    data.config.cellDiffWarn.toFixed(3);

                document.getElementById("cellDiffError").value =
                    data.config.cellDiffError.toFixed(3);
            }
        });
}


// ---------------------------------------------------------
// Reset Health History
// ---------------------------------------------------------
function healthReset() {
    fetch("/api/health/reset")
        .then(r => {
            if (!r.ok) {
                alert(t("health.err_reset_auth"));
                return null;
            }
            return r.text();
        })
        .then(t => { if (t !== null) loadHealth(); })
        .catch(() => alert(t("health.err_reset_auth")));
}


// ---------------------------------------------------------
// Save Thresholds
// ---------------------------------------------------------
function saveHealthConfig() {

    // Re-enable auto-reload
    healthAutoReload = true;

    let warn  = parseFloat(document.getElementById("cellDiffWarn").value);
    let error = parseFloat(document.getElementById("cellDiffError").value);

    fetch("/api/health", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
            cellDiffWarn: warn,
            cellDiffError: error
        })
    })
    .then(r => r.text().then(txt => ({ ok: r.ok, txt: txt })))
    .then(({ ok, txt }) => {
        const m = document.getElementById("health_msg");
        m.style.color = ok ? "#00ff00" : "#ff4444";
        m.textContent = ok ? txt : (txt === "Unauthorized" ? t("health.err_save_auth") : txt);
        if (ok) loadHealth();
    })
    .catch(() => {
        const m = document.getElementById("health_msg");
        m.style.color = "#ff4444";
        m.textContent = t("health.err_save_auth");
    });
}


// ---------------------------------------------------------
// Auto-Reload every 2 seconds (if allowed)
// ---------------------------------------------------------
window._healthTimer = setInterval(function () {
    if (healthAutoReload) loadHealth();
}, 2000);

// SPA navigation hook: stop the poll timer when leaving the page.
window.pageCleanup = function () {
    clearInterval(window._healthTimer);
    window._healthTimer = null;
};


// ---------------------------------------------------------
// Initial Load
// ---------------------------------------------------------
loadHealth();
