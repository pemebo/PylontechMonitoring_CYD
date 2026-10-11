// ---------------------------------------------------------
// Screen config page
// ---------------------------------------------------------

// The firmware replies with plain English text. We never translate the backend:
// we map the known responses to screen.* keys for DISPLAY only. Any programmatic
// comparison (e.g. the "saved" check below) must use the raw response.
var SCREEN_MSG_KEYS = {
    "Screen config saved": "screen.msg_saved",
    "Missing body": "screen.msg_missing_body",
    "Invalid JSON": "screen.msg_invalid_json",
    "Screen cannot be turned off while WiFi is unavailable": "screen.msg_wifi_unavailable"
};

function screenMsg(raw) {
    var key = SCREEN_MSG_KEYS[raw];
    return key ? t(key) : raw;
}

function pad2(n) {
    return String(n).padStart(2, "0");
}

// "22:30" (time input) -> [hour, minute]
function timeParts(id) {
    const v = document.getElementById(id).value || "";
    const p = v.split(":");
    if (!v || p.length !== 2) return [0, 0];
    return [parseInt(p[0], 10) || 0, parseInt(p[1], 10) || 0];
}

function formatTime(h, m) {
    return pad2(h) + ":" + pad2(m);
}

// ---------------------------------------------------------
// Schedule (form state only; persisted with Save)
// ---------------------------------------------------------
function setScheduleUI(on, timeValid) {
    // Schedule and Auto power-off are mutually exclusive: enabling schedule
    // clears auto power-off.
    if (on) document.getElementById("scr_autoOff").checked = false;
    document.getElementById("scr_schedule").checked = on;
    document.getElementById("scr_offTime").disabled = !on;
    document.getElementById("scr_onTime").disabled = !on;

    // Warn only when scheduling is active but there is no valid system time.
    document.getElementById("scr_time_warn").style.display =
        (on && !timeValid) ? "block" : "none";
}

// Auto power-off is mutually exclusive with Schedule: enabling it clears
// schedule (and its time fields).
function autoOffChange() {
    if (document.getElementById("scr_autoOff").checked)
        setScheduleUI(false, true);
}

// ---------------------------------------------------------
// Master switch: when the screen is disabled, every other
// option on this page is unavailable (including touch).
// ---------------------------------------------------------
function applyEnabledState() {
    const en = document.getElementById("scr_enabled").checked;
    document.getElementById("scr_disp_body").classList.toggle("scr-disabled", !en);
    document.getElementById("scr_power_body").classList.toggle("scr-disabled", !en);

    // Disable every control except the master switch itself and the Display box
    // Save button (that button must stay usable so the disabled state can be saved).
    const keep = ["scr_enabled", "scr_disp_save"];
    document.querySelectorAll(
        "#scr_disp_body input, #scr_disp_body button, " +
        "#scr_power_body input, #scr_power_body button"
    ).forEach(el => { if (!keep.includes(el.id)) el.disabled = !en; });
}

// ---------------------------------------------------------
// Load current config + runtime state
// ---------------------------------------------------------
function loadScreen() {
    fetch("/api/screen")
        .then(r => r.json())
        .then(j => {
            const cfg = j.config;
            const st  = j.state;

            // Master switch reflects the STORED config, not the runtime backlight
            // state — an auto power-off turning the screen off must not uncheck it.
            document.getElementById("scr_enabled").checked = cfg.enabled;

            document.getElementById("scr_touch").checked = cfg.touch;

            document.getElementById("scr_dark").checked = cfg.darkMode;

            document.getElementById("scr_autoOff").checked = cfg.autoOff;
            document.getElementById("scr_autoOffSec").value = cfg.autoOffSec;

            document.getElementById("scr_brightness").value = cfg.brightness;
            document.getElementById("scr_brightness_val").textContent = cfg.brightness;

            document.getElementById("scr_refresh").value = cfg.refreshSec;

            setScheduleUI(cfg.scheduled, st.timeValid);
            document.getElementById("scr_offTime").value = formatTime(cfg.offHour, cfg.offMin);
            document.getElementById("scr_onTime").value = formatTime(cfg.onHour, cfg.onMin);

            // The screen may only be turned off while WiFi is available.
            const en = document.getElementById("scr_enabled");
            en.disabled = !st.wifiOk;
            document.getElementById("scr_wifi_warn").style.display =
                st.wifiOk ? "none" : "block";

            applyEnabledState();
        });
}


// ---------------------------------------------------------
// Save
// ---------------------------------------------------------
function screenSave(msgId) {
    const off = timeParts("scr_offTime");
    const on  = timeParts("scr_onTime");

    const data = {
        enabled:    document.getElementById("scr_enabled").checked,
        touch:      document.getElementById("scr_touch").checked,
        darkMode:   document.getElementById("scr_dark").checked,
        autoOff:    document.getElementById("scr_autoOff").checked,
        autoOffSec: Number(document.getElementById("scr_autoOffSec").value),
        brightness: Number(document.getElementById("scr_brightness").value),
        scheduled:  document.getElementById("scr_schedule").checked,
        offHour:    off[0],
        offMin:     off[1],
        onHour:     on[0],
        onMin:      on[1],
        refreshSec: Number(document.getElementById("scr_refresh").value)
    };

    fetch("/api/screen", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(data)
    })
    .then(r => r.text())
    .then(txt => {
        const msg = document.getElementById(msgId);
        // Compare against the RAW server text (never the translated display).
        msg.style.color = txt === "Screen config saved" ? "#00ff00" : "#ff4444";
        msg.textContent = screenMsg(txt);
        loadScreen();
    });
}


// ---------------------------------------------------------
// Live (debounced) brightness while dragging the slider
// ---------------------------------------------------------
var brightTimer = null;
function onBrightnessInput(value) {
    document.getElementById("scr_brightness_val").textContent = value;
    clearTimeout(brightTimer);
    const v = Number(value);
    brightTimer = setTimeout(() => {
        fetch("/api/screen", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ brightness: v })
        });
    }, 350);
}


// ---------------------------------------------------------
// Init
// ---------------------------------------------------------
// loadScreen() is invoked by layout.html's loadPage() onload hook (like the
// connection page) so re-entering this view re-populates the form even if the
// script had already been executed in this SPA session.