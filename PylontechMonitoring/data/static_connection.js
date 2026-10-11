// -------------------------------------------------------------
// init
// -------------------------------------------------------------
function initConnectionPage() {
    wifiLoadStatus();
    mqttLoad();
    tzLoad();
    timeLoad();
    ipLoad();
    restartLoad();
    languageLoad();
}

// -------------------------------------------------------------
// Server response -> i18n key
// The firmware replies with plain English text. We never translate the
// backend itself: we map the known responses to conn.* keys for DISPLAY only
// (falling back to the raw server text). Any programmatic comparison must use
// the raw response, not the mapped text.
// -------------------------------------------------------------
var SERVER_MSG_KEYS = {
    "Connecting…": "conn.msg_connecting",
    "MQTT saved": "conn.msg_mqtt_saved",
    "Time saved": "conn.msg_time_saved",
    "Network saved": "conn.msg_network_saved",
    "Password saved": "conn.msg_password_saved",
    "Password unchanged": "conn.msg_password_unchanged",
    "Automatic restart saved": "conn.msg_restart_saved",
    "Missing body": "conn.msg_missing_body",
    "Invalid JSON": "conn.msg_invalid_json",
    "SSID missing": "conn.msg_ssid_missing",
    "Max 20 characters": "conn.msg_max_20",
    "Only alphanumeric characters (A-Z a-z 0-9)": "conn.msg_alnum",
    "Time must be in HH:MM format (00-23 / 00-59)": "conn.msg_time_format",
    "Days must be between 1 and 365": "conn.msg_days_range",
    "Unsupported language": "conn.msg_unsupported_language"
};

function serverMsg(raw) {
    var key = SERVER_MSG_KEYS[raw];
    return key ? t(key) : raw;
}

// -------------------------------------------------------------
// Language
// -------------------------------------------------------------
function languageLoad() {
    fetch("/api/language")
        .then(r => r.json())
        .then(j => {
            var sel = document.getElementById("lang_select");
            if (sel) sel.value = (j.language === "es") ? "es" : "en";
        });
}

function languageSave() {
    var sel = document.getElementById("lang_select");
    var lang = sel ? sel.value : "en";
    var m = document.getElementById("lang_msg");

    fetch("/api/language", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify({ language: lang })
    })
    .then(r => r.text().then(txt => ({ ok: r.ok, txt: txt })))
    .then(({ ok, txt }) => {
        if (!ok) {
            if (m) {
                m.style.color = "#ff4444";
                m.textContent = serverMsg(txt);
            }
            return;
        }

        // Apply immediately: cookie + <html lang> + full document translation.
        setLang(lang);

        if (m) {
            m.style.color = "#00ff00";
            m.textContent = t("conn.language_saved");
        }

        // Re-render the current page so its dynamic text (built in JS) uses
        // the new language too.
        initConnectionPage();
    })
    .catch(() => {
        if (m) {
            m.style.color = "#ff4444";
            m.textContent = serverMsg("Invalid JSON");
        }
    });
}

// -------------------------------------------------------------
// WiFi
// -------------------------------------------------------------
function wifiLoadStatus() {
    fetch("/api/wifi")
        .then(r => r.json())
        .then(j => {
            document.getElementById("wifi_status").textContent =
                `${t("conn.connected")}: ${j.connected}, SSID: ${j.ssid}, RSSI: ${j.rssi}, IP: ${j.ip}`;

            const btn = document.getElementById("wifi_toggle_btn");
            if (btn) {
                btn.dataset.enabled = j.enabled ? "true" : "false";
                btn.textContent = j.enabled ? t("conn.wifi_disable") : t("conn.wifi_enable");
            }
        });
}

function wifiToggle() {
    const btn = document.getElementById("wifi_toggle_btn");
    const on = btn.dataset.enabled !== "true";   // target state (flip)
    const m = document.getElementById("wifi_msg");

    if (!on) {
        m.style.color = "#ffaa00";
        m.textContent = t("conn.wifi_disabling");
    }

    fetch("/api/wifi/enabled", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify({ enabled: on })
    })
    .then(r => r.text().then(txt => ({ ok: r.ok, txt: txt })))
    .then(({ ok, txt }) => {
        if (!on) return;   // radio is going down; keep the "disabling" message
        m.style.color = ok ? "#00ff00" : "#ff4444";
        m.textContent = ok ? t("conn.wifi_enabled") : serverMsg(txt);
        wifiLoadStatus();
    })
    .catch(() => {
        if (!on) return;
        m.style.color = "#ff4444";
        m.textContent = t("conn.wifi_enable_failed");
    });
}

function wifiScan() {
    fetch("/api/wifi/scan")
        .then(r => r.json())
        .then(data => {

            let nets = data.nets || [];

            // Remove duplicates → keep strongest signal
            let best = {};
            nets.forEach(n => {
                if (!best[n.ssid] || n.rssi > best[n.ssid].rssi) {
                    best[n.ssid] = n;
                }
            });

            // Convert to array + sort
            let list = Object.values(best).sort((a, b) => b.rssi - a.rssi);

            // Generate HTML
            let html = "";

            list.forEach(n => {
                html += `
                    <div class="wifi-entry">

                        <div class="wifi-row">
                            <span class="wifi-ssid">${n.ssid}</span>
                            <span class="wifi-signal">${n.rssi} dBm</span>
                        </div>

                        <div class="wifi-row">
                            <input type="password" id="pass_${n.ssid}" placeholder="${t("conn.enter_password")}">
                            <button onclick="wifiConnectScan('${n.ssid}')">${t("conn.connect")}</button>
                        </div>

                    </div>
                `;
            });

            document.getElementById("wifi_scanlist").innerHTML = html;
        });
}


function wifiSelectAndConnect(ssid) {
    document.getElementById("wifi_ssid").value = ssid;
    document.getElementById("wifi_pass").value = "";
    document.getElementById("wifi_manual").checked = true;
    wifiToggleManual();
}


function wifiConnectScan(ssid) {
    let pass = document.getElementById("pass_" + ssid).value;

    let data = {
        ssid: ssid,
        pass: pass
    };

    fetch("/api/wifi", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify(data)
    })
    .then(r => r.text().then(txt => ({ ok: r.ok, txt: txt })))
    .then(({ ok, txt }) => {
        const m = document.getElementById("wifi_msg");
        m.style.color = ok ? "#00ff00" : "#ff4444";
        m.textContent = serverMsg(txt);
        wifiLoadStatus();
    });
}

function wifiToggleManual() {
    const box = document.getElementById("wifi_manual_box");
    box.style.display = document.getElementById("wifi_manual").checked ? "block" : "none";
}


// -------------------------------------------------------------
// MQTT
// -------------------------------------------------------------
function mqttLoad() {
    fetch("/api/mqtt")
        .then(r => r.json())
        .then(j => {
            document.getElementById("mqtt_enabled").checked = j.enabled;
            document.getElementById("mqtt_server").value = j.server;
            document.getElementById("mqtt_port").value = j.port;
            document.getElementById("mqtt_user").value = j.user;
            document.getElementById("mqtt_topic").value = j.topic;
        });
}

function mqttSave() {
    let data = {
        enabled: document.getElementById("mqtt_enabled").checked,
        server: document.getElementById("mqtt_server").value,
        port: Number(document.getElementById("mqtt_port").value),
        user: document.getElementById("mqtt_user").value,
        pass: document.getElementById("mqtt_pass").value,
        topic: document.getElementById("mqtt_topic").value
    };

    fetch("/api/mqtt", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify(data)
    })
    .then(r => r.text().then(txt => ({ ok: r.ok, txt: txt })))
    .then(({ ok, txt }) => {
        const m = document.getElementById("mqtt_msg");
        m.style.color = ok ? "#00ff00" : "#ff4444";
        m.textContent = serverMsg(txt);
    });
}

// -------------------------------------------------------------
// NTP / Time – MODI
// -------------------------------------------------------------
function ntpModeSelect(mode) {

    const g = document.getElementById("ntp_gateway");
    const n = document.getElementById("ntp_manual");
    const tm = document.getElementById("time_manual");

    if (mode === "gateway") {
        g.checked = true; n.checked = false; tm.checked = false;
    }
    if (mode === "manual") {
        g.checked = false; n.checked = true; tm.checked = false;
    }
    if (mode === "time") {
        g.checked = false; n.checked = false; tm.checked = true;
    }

    document.getElementById("ntp_manual_box").style.display = n.checked ? "block" : "none";
    document.getElementById("time_manual_box").style.display = tm.checked ? "block" : "none";

    updateCurrentNtpServer();
}



// -------------------------------------------------------------
// Current Server
// -------------------------------------------------------------
function updateCurrentNtpServer() {
    const manual = document.getElementById("time_manual").checked;

    if (manual) {
        document.getElementById("ntp_current").textContent = t("conn.manual_time_no_ntp");
    } else {
        // Always show the server from the API
        document.getElementById("ntp_current").textContent = CURRENT_NTP_SERVER;
    }
}



// -------------------------------------------------------------
// Timezones
// -------------------------------------------------------------
// Top-level declarations use var on purpose: the SPA re-injects and re-executes
// this script on every navigation, and a top-level let/const would throw
// "Identifier already declared" on the second run. var redeclaration is legal
// and keeps the script re-runnable.
var TZDATA = {};
var CURRENT_NTP_SERVER = "";
var SAVED_TIMEZONE = "";

function tzLoad() {
    fetch("/timezone.json")
        .then(r => r.json())
        .then(j => {
            TZDATA = j;

            let regionSel = document.getElementById("tz_region");
            regionSel.innerHTML = "";

            Object.keys(j).forEach(region => {
                let o = document.createElement("option");
                o.value = region;
                o.textContent = region;
                regionSel.appendChild(o);
            });

            tzRegionChanged();
        });
}

function tzRegionChanged() {
    let region = document.getElementById("tz_region").value;
    let citySel = document.getElementById("tz_city");
    citySel.innerHTML = "";

    if (TZDATA[region]) {
        TZDATA[region].forEach(e => {
            let o = document.createElement("option");
            o.value = e.tz;
            o.textContent = e.city;
            citySel.appendChild(o);
        });

        // Restore previously selected timezone if available in new region
        if (SAVED_TIMEZONE) {
            for (let i = 0; i < citySel.options.length; i++) {
                if (citySel.options[i].value === SAVED_TIMEZONE) {
                    citySel.value = SAVED_TIMEZONE;
                    SAVED_TIMEZONE = "";
                    break;
                }
            }
        }
    }

    tzCityChanged();
}

function tzCityChanged() {
    let tz = document.getElementById("tz_city").value;
    document.getElementById("tz_full").value = tz;
}

// -------------------------------------------------------------
// Load from API
// -------------------------------------------------------------
function timeLoad() {
    fetch("/api/time")
        .then(r => r.json())
        .then(j => {

            // Checkboxen korrekt setzen
            document.getElementById("ntp_gateway").checked = j.use_gateway_ntp;
            document.getElementById("ntp_manual").checked  = j.manual_ntp;
            document.getElementById("time_manual").checked = j.manual_mode;

   // Manual NTP input field: always pool.ntp.org
    document.getElementById("ntp_server").value = "pool.ntp.org";

            // Manual Time fields
            document.getElementById("time_date").value = j.manual_date;
            document.getElementById("time_time").value = j.manual_time;

            // Timezone - set region and city
            let tz = j.timezone || "Europe/Berlin";
            SAVED_TIMEZONE = tz;

            let regionSel = document.getElementById("tz_region");
            regionSel.value = tz.split("/")[0];

            // tzRegionChanged will repopulate cities and restore saved timezone
            tzRegionChanged();

            // Store and display API server
            CURRENT_NTP_SERVER = j.server;
            document.getElementById("ntp_current").textContent = CURRENT_NTP_SERVER;

            // Enable modes (UI only, must NOT overwrite ntp_current)
            ntpModeSelect(
                j.use_gateway_ntp ? "gateway" :
                j.manual_ntp      ? "manual"  :
                                    "time"
            );
        });
}

// -------------------------------------------------------------
// Save to API
// -------------------------------------------------------------
function timeSave() {
    let data = {
        manual_mode: document.getElementById("time_manual").checked,
        manual_date: document.getElementById("time_date").value,
        manual_time: document.getElementById("time_time").value,
        use_gateway_ntp: document.getElementById("ntp_gateway").checked,
        manual_ntp: document.getElementById("ntp_manual").checked,
        server: document.getElementById("ntp_server").value,
        timezone: document.getElementById("tz_full").value
    };

    fetch("/api/time", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify(data)
    })
    .then(r => r.text().then(txt => ({ ok: r.ok, txt: txt })))
    .then(({ ok, txt }) => {
        const m = document.getElementById("time_msg");
        m.style.color = ok ? "#00ff00" : "#ff4444";
        m.textContent = serverMsg(txt);
    });
}

// -------------------------------------------------------------
// IP Config
// -------------------------------------------------------------
function ipLoad() {
    fetch("/api/network")
        .then(r => r.json())
        .then(j => {

            // Set DHCP
            document.getElementById("ip_dhcp").checked = j.dhcp;

            // Set fields
            document.getElementById("ip_addr").value = j.ip;
            document.getElementById("ip_mask").value = j.mask;
            document.getElementById("ip_gw").value = j.gw;
            document.getElementById("ip_dns").value = j.dns;

            // Sichtbarkeit aktualisieren
            ipToggleDhcp();
        });
}

function ipToggleDhcp() {
    const dhcp = document.getElementById("ip_dhcp").checked;
    document.getElementById("ip_static_box").style.display = dhcp ? "none" : "block";
}

function ipSave() {
    let data = {
        dhcp: document.getElementById("ip_dhcp").checked,
        ip:   document.getElementById("ip_addr").value,
        mask: document.getElementById("ip_mask").value,
        gw:   document.getElementById("ip_gw").value,
        dns:  document.getElementById("ip_dns").value
    };

    fetch("/api/network", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify(data)
    })
    .then(r => r.text().then(txt => ({ ok: r.ok, txt: txt })))
    .then(({ ok, txt }) => {
        const m = document.getElementById("ip_msg");
        m.style.color = ok ? "#00ff00" : "#ff4444";
        m.textContent = serverMsg(txt);
    });
}


// -------------------------------------------------------------
// Config Password
// -------------------------------------------------------------
function passwordSave() {
    let pass = document.getElementById("cfg_password").value.trim();

    if (pass.length > 20 || (pass.length > 0 && !/^[A-Za-z0-9]+$/.test(pass))) {
        const m = document.getElementById("password_msg");
        m.style.color = "#ff4444";
        m.textContent = t("conn.err_password_charset");
        return;
    }

    fetch("/api/password", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify({ password: pass })
    })
    .then(r => r.text().then(txt => ({ ok: r.ok, txt: txt })))
    .then(({ ok, txt }) => {
        const m = document.getElementById("password_msg");
        m.style.color = ok ? "#00ff00" : "#ff4444";
        m.textContent = serverMsg(txt);
        document.getElementById("cfg_password").value = "";
    });
}

// -------------------------------------------------------------
// Automatic Restart
// -------------------------------------------------------------
function restartLoad() {
    fetch("/api/autoreboot")
        .then(r => r.json())
        .then(j => {
            document.getElementById("rbt_enabled").checked = j.enabled;
            document.getElementById("rbt_time").value = j.time || "03:00";
            document.getElementById("rbt_days").value = j.days;
        });
}

function restartSave() {
    const time = document.getElementById("rbt_time").value;
    const days = Number(document.getElementById("rbt_days").value);

    if (!/^([01]\d|2[0-3]):[0-5]\d$/.test(time)) {
        const m = document.getElementById("restart_msg");
        m.style.color = "#ff4444";
        m.textContent = t("conn.err_time_format");
        return;
    }

    if (!Number.isInteger(days) || days < 1 || days > 365) {
        const m = document.getElementById("restart_msg");
        m.style.color = "#ff4444";
        m.textContent = t("conn.err_days_range");
        return;
    }

    let data = {
        enabled: document.getElementById("rbt_enabled").checked,
        time: time,
        days: days
    };

    fetch("/api/autoreboot", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify(data)
    })
    .then(r => r.text().then(txt => ({ ok: r.ok, txt: txt })))
    .then(({ ok, txt }) => {
        const m = document.getElementById("restart_msg");
        m.style.color = ok ? "#00ff00" : "#ff4444";
        m.textContent = serverMsg(txt);
    });
}

// -------------------------------------------------------------
// Init
// -------------------------------------------------------------
window.addEventListener("load", () => {
    wifiLoadStatus();
    mqttLoad();
    tzLoad();
    timeLoad();
    ipLoad();
    restartLoad();
    languageLoad();

    document.getElementById("tz_region").addEventListener("change", tzRegionChanged);
    document.getElementById("tz_city").addEventListener("change", tzCityChanged);
});
