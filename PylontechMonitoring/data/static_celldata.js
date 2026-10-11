function sanitize(str){
    return str.replace(/[^A-Za-z0-9_]/g, "");
}

// Backend replies stay in English; map the known responses for DISPLAY only.
// Programmatic checks (the /warning/i test below) must use the raw response.
var CELL_MSG_KEYS = {
    "BAT settings saved": "cell.msg_saved",
    "Missing body": "cell.msg_missing_body",
    "Invalid JSON": "cell.msg_invalid_json",
    "Cannot disable the BAT parser while a BAT export is running": "cell.msg_export_running",
    "BAT query interval must be at least 10 seconds": "cell.msg_interval_min"
};

function cellMsg(raw) {
    var key = CELL_MSG_KEYS[raw];
    if (key) return t(key);

    // "BAT settings saved. Warning: with N module(s) the BAT interval should be
    // at least S s (5 s per module); a shorter interval can fragment the heap
    // during an SD export."
    var m = raw.match(/^BAT settings saved\. Warning: with (\d+) module\(s\) the BAT interval should be at least (\d+) s \(5 s per module\); a shorter interval can fragment the heap during an SD export\.$/);
    if (m) {
        return t("cell.msg_saved_warning").replace("{n}", m[1]).replace("{sec}", m[2]);
    }
    return raw;
}

function batLoad() {
    fetch("/api/bat/cells")
        .then(r => r.json())
        .then(j => {

            // Config
            document.getElementById("enable_bat").checked = j.config.enableBat;
            document.getElementById("topic_bat").value   = j.mqtt.topicBat;
            document.getElementById("cell_prefix").value = j.mqtt.cellPrefix;
            document.getElementById("interval_bat").value = j.config.intervalBat / 1000;

            // Recommended interval: 5 s per present module (mirrors the server).
            const hint = document.getElementById("interval_bat_hint");
            if (hint) {
                const n = j.config.presentModules || 0;
                const minS = Math.round((j.config.minIntervalBatMs || 0) / 1000);
                if (n > 0) {
                    hint.textContent = n === 1
                        ? t("cell.interval_hint_one").replace("{sec}", minS)
                        : t("cell.interval_hint_many")
                            .replace("{sec}", minS)
                            .replace("{n}", n);
                } else {
                    hint.textContent = "";
                }
            }

            // Table
            let table = document.getElementById("bat_table");
            table.innerHTML = "";

            // saved fields
            let saved = {};
            j.fields.forEach(f => saved[f.name] = f);

            for (let i = 0; i < j.headers.length; i++) {

                let name = j.headers[i];
                let raw  = j.values[i] || "";

                // Default
                let display = name;
                let factor  = "1";
                let unit    = "";
                let mqtt    = false;

                // saved values override
                if (saved[name]) {
                    display = saved[name].display;
                    factor  = saved[name].factor;
                    unit    = saved[name].unit;
                    mqtt    = saved[name].sendMQTT;
                }
                else {
                    // Autodetect
                    let n = name.toLowerCase();
                    let r = raw.toLowerCase();

                    if (r.includes("%")) {
                        factor = "1"; unit = "%";
                    }
                    else if (r.includes("mah")) {
                        factor = "0.001"; unit = "Ah";
                    }
                    else if (isNaN(parseFloat(r))) {
                        factor = "text"; unit = "";
                    }
                    else if (n.includes("volt")) {
                        factor = "0.001"; unit = "V";
                    }
                    else if (n.includes("curr")) {
                        factor = "0.001"; unit = "A";
                    }
                    else if (n.includes("temp") || n.includes("tempr")) {
                        factor = "0.001"; unit = "°C";
                    }
                }

                // Render table
                let row = document.createElement("tr");
                row.innerHTML = `
                    <td>${name}</td>
                    <td><input id="disp_${name}" value="${display}"></td>
                    <td>${raw}</td>
                    <td>
                        <select id="fac_${name}">
                            <option value="0.0001">0.0001</option>
                            <option value="0.001">0.001</option>
                            <option value="0.01">0.01</option>
                            <option value="0.1">0.1</option>
                            <option value="1">1</option>
                            <option value="10">10</option>
                            <option value="text">text</option>
                            <option value="date">date</option>
                        </select>
                    </td>
                    <td>
                        <select id="unit_${name}">
                            <option value=""></option>
                            <option value="V">V</option>
                            <option value="A">A</option>
                            <option value="°C">°C</option>
                            <option value="%">%</option>
                            <option value="Ah">Ah</option>
                            <option value="timestamp">timestamp</option>
                        </select>
                    </td>
                    <td><input type="checkbox" id="mqtt_${name}"></td>
                `;

                table.appendChild(row);

                // Set values
                document.getElementById("fac_" + name).value = factor;
                document.getElementById("unit_" + name).value = unit;
                document.getElementById("mqtt_" + name).checked = mqtt;
            }
        });
}

function batSave() {

    let data = {
        config: {
            intervalBat: parseInt(document.getElementById("interval_bat").value) * 1000,
            enableBat: document.getElementById("enable_bat").checked
        },
        mqtt: {
            topicBat:   document.getElementById("topic_bat").value,
            cellPrefix: document.getElementById("cell_prefix").value
        },
        fields: []
    };

    document.querySelectorAll("#bat_table tr").forEach(row => {
        let name = row.cells[0].innerText;
        let disp = sanitize(document.getElementById("disp_" + name).value);

        data.fields.push({
            name: name,
            display: disp,
            factor: document.getElementById("fac_" + name).value,
            unit:   document.getElementById("unit_" + name).value,
            sendMQTT: document.getElementById("mqtt_" + name).checked
        });
    });

    fetch("/api/bat/set", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify(data)
    })
    .then(r => r.text().then(txt => ({ ok: r.ok, txt: txt })))
    .then(({ ok, txt }) => {
        const m = document.getElementById("bat_msg");
        const warn = ok && /warning/i.test(txt);
        m.style.color = !ok ? "#ff4444" : (warn ? "#ffaa00" : "#00ff00");
        m.textContent = cellMsg(txt);
    });
}

batLoad();
updateMqttIndicator();
