function sanitize(str){
    return str.replace(/[^A-Za-z0-9_]/g, "");
}

// Backend replies stay in English; map the known responses for DISPLAY only.
var STAT_MSG_KEYS = {
    "STAT settings saved": "stat.msg_saved",
    "Missing body": "stat.msg_missing_body",
    "Stale Statistics page: reload the UI (Ctrl+F5)": "stat.msg_stale"
};

function statMsg(raw) {
    var key = STAT_MSG_KEYS[raw];
    if (key) return t(key);
    if (raw.indexOf("Invalid JSON: ") === 0) {
        return t("stat.msg_invalid_json") + raw.substring("Invalid JSON: ".length);
    }
    return raw;
}

function autodetect(name, raw) {

    let n = name.toLowerCase();
    let r = (raw || "").toLowerCase();

    if (n.includes("percent") || r.includes("%"))
        return { factor: "1", unit: "%", mqtt: true };

    if (n.includes("coulomb"))
        return { factor: "0.001", unit: "Ah", mqtt: true };

    if (isNaN(parseFloat(r)))
        return { factor: "text", unit: "", mqtt: false };

    return { factor: "1", unit: "", mqtt: false };
}

function statLoad() {
    fetch("/api/stat/values")
        .then(r => r.json())
        .then(j => {

            // CONFIG top
            document.getElementById("enable_stat").checked = j.config.enableStat;
            document.getElementById("topic_stat").value = j.mqtt.topicStat;
            document.getElementById("interval_stat").value = j.config.intervalStat / 1000;

            // Table bottom
            let container = document.getElementById("stat_groups");
            container.innerHTML = "";

            // Create a single table
            let table = document.createElement("table");
            table.className = "conn-table";

            table.innerHTML = `
<thead>
                    <tr>
                         <th>${t("stat.field_name")}</th>
                         <th>${t("stat.display_name")}</th>
                         <th>${t("stat.raw_data")}</th>
                         <th>${t("stat.factor")}</th>
                         <th>${t("stat.unit")}</th>
                         <th>${t("stat.mqtt_col")}</th>
                     </tr>
                </thead>
                <tbody></tbody>
            `;

            let tbody = table.querySelector("tbody");

            // saved fields
            let saved = {};
            j.fields.forEach(f => saved[f.name] = f);

            // ALL FIELDS TOGETHER
            for (let i = 0; i < j.headers.length; i++) {

                let name = j.headers[i];
                let raw  = j.values[i];

                let display = name;
                let factor  = "1";
                let unit    = "";
                let mqtt    = false;

                if (saved[name]) {
                    display = saved[name].display;
                    factor  = saved[name].factor;
                    unit    = saved[name].unit;
                    mqtt    = saved[name].sendMQTT;
                }
                else {
                    let auto = autodetect(name, raw);
                    factor = auto.factor;
                    unit   = auto.unit;
                    mqtt   = auto.mqtt;
                }

                let row = document.createElement("tr");
                row.innerHTML = `
                    <td>${name}</td>
                    <td><input value="${display}"></td>
                    <td>${raw}</td>
                    <td>
                        <select>
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
                        <select>
                            <option value=""></option>
                            <option value="V">V</option>
                            <option value="A">A</option>
                            <option value="Ah">Ah</option>
                            <option value="°C">°C</option>
                            <option value="%">%</option>
                            <option value="timestamp">timestamp</option>
                        </select>
                    </td>
<td><input type="checkbox" class="mqtt"></td>
                `;

                // Set values
                row.cells[3].querySelector("select").value = factor;
                row.cells[4].querySelector("select").value = unit;
                row.cells[5].querySelector("input").checked = mqtt;

                tbody.appendChild(row);
            }

            container.appendChild(table);
        });
}

statLoad();
updateMqttIndicator();

function saveStatSettings() {

    // The field table is sent as one packed record per line
    // ("name|display|factor|unit|mqtt") AFTER a single-line JSON header, so the
    // firmware never has to build a multi-KB JSON document for the 51 STAT fields.

    let lines = [];

    document.querySelectorAll('#stat_groups table tbody tr').forEach(row => {

        let name = row.cells[0].innerText;

        let dispInput = row.cells[1].querySelector("input");
        let dispRaw   = dispInput.value || name;
        let dispClean = sanitize(dispRaw);
        dispInput.value = dispClean;

        let factor = row.cells[3].querySelector("select").value;
        let unit   = row.cells[4].querySelector("select").value;
        let mqtt   = row.cells[5].querySelector("input").checked;

        lines.push([name, dispClean, factor, unit, mqtt ? 1 : 0].join("|"));
    });

    let payload =
        JSON.stringify({
            config: {
                enableStat: document.getElementById("enable_stat").checked,
                intervalStat: parseInt(document.getElementById("interval_stat").value) * 1000
            },
            mqtt: {
                topicStat: document.getElementById("topic_stat").value
            }
        }) + "\n" + lines.join("\n");

    fetch('/api/stat/set', {
        method: 'POST',
        headers: {'Content-Type':'application/json'},
        body: payload
    })
    .then(r => r.text().then(txt => ({ ok: r.ok, txt: txt })))
    .then(({ ok, txt }) => {
        const m = document.getElementById("stat_msg");
        m.style.color = ok ? "#00ff00" : "#ff4444";
        m.textContent = statMsg(txt);
    });
}
