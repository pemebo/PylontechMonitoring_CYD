function sanitize(str){
    return str.replace(/[^A-Za-z0-9_]/g, "");
}

// Backend replies stay in English; map the known responses for DISPLAY only.
var BV_MSG_KEYS = {
    "PWR settings saved": "bv.msg_saved",
    "Missing body": "bv.msg_missing_body",
    "Invalid JSON": "bv.msg_invalid_json"
};

function bvMsg(raw) {
    var key = BV_MSG_KEYS[raw];
    return key ? t(key) : raw;
}

function pwrLoad() {
    fetch("/api/pwr/base")
        .then(r => r.json())
        .then(j => {

            // Section 1
            document.getElementById("use_fahrenheit").checked = j.config.useFahrenheit;

            // Section 2
            document.getElementById("topic_pwr").value   = j.mqtt.topicPwr;
            document.getElementById("topic_stack").value = j.mqtt.topicStack;

            // Section 3
            document.getElementById("interval_pwr").value = j.config.intervalPwr / 1000;

            // Table
            let table = document.getElementById("pwr_table");
            table.innerHTML = "";

            // Map for saved fields
            let saved = {};
            j.fields.forEach(f => saved[f.name] = f);

            for (let i = 0; i < j.headers.length; i++) {

                let name = j.headers[i];
                let raw  = j.values[i] || "";

                // Default values
                let display = name;
                let factor  = "1";
                let unit    = "";
                let mqtt    = false;

                // When saved → overwrite
                if (saved[name]) {
                    display = saved[name].display;
                    factor  = saved[name].factor;
                    unit    = saved[name].unit;
                    mqtt    = saved[name].sendMQTT;
                }
                else {
                    // AUTODETECT
                    let n = name.toLowerCase();
                    let r = raw.toLowerCase();

                    if (r !== "") {

                        if (r.endsWith('%') || n.endsWith('%')) {
                            factor = '1'; unit = '%'; mqtt = true;
                        }
                        else if (n.includes('time')) {
                            factor = 'date'; unit = 'timestamp';
                        }
                        else if (isNaN(parseFloat(r))) {
                            factor = 'text'; unit = '';
                        }
                        else if (n.includes('volt')) {
                            factor = '0.001'; unit = 'V'; mqtt = true;
                        }
                        else if (n.includes('curr')) {
                            factor = '0.001'; unit = 'A'; mqtt = true;
                        }
                        else if (n.endsWith('id')) {
                            factor = '1'; unit = '';
                        }
                        else if (n.includes('t')) {
                            factor = '0.001'; unit = '°C';
                        }
                        else if (n.includes('v')) {
                            factor = '0.001'; unit = 'V';
                        }
                        else {
                            factor = 'text'; unit = '';
                        }
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

function pwrSave() {

    let data = {
        config: {
            intervalPwr: parseInt(document.getElementById("interval_pwr").value) * 1000,
            useFahrenheit: document.getElementById("use_fahrenheit").checked
        },
        mqtt: {
            topicPwr:   document.getElementById("topic_pwr").value,
            topicStack: document.getElementById("topic_stack").value
        },
        fields: []
    };

    document.querySelectorAll("#pwr_table tr").forEach(row => {
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

    fetch("/api/pwr/set", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify(data)
    })
    .then(r => r.text().then(txt => ({ ok: r.ok, txt: txt })))
    .then(({ ok, txt }) => {
        const m = document.getElementById("pwr_msg");
        m.style.color = ok ? "#00ff00" : "#ff4444";
        m.textContent = bvMsg(txt);
    });
}

pwrLoad();
updateMqttIndicator();
