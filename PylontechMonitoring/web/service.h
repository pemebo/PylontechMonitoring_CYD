#pragma once
#include <Arduino.h>

const char SERVICE_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>ESP32 Firmware Update</title>
<style>
body { font-family: sans-serif; padding: 20px; }
button { padding: 8px 16px; margin-top: 10px; }
#status { margin-top: 15px; font-weight: bold; }
progress { width: 300px; display:none; }
</style>
</head>
<body>

<h2>Firmware update</h2>
<p>Current firmware version: <b id="fwver">...</b></p>

<p>Please select a <b>.bin</b> firmware file:</p>

<input type="file" id="fw">
<br><br>
<button onclick="upload()">Flash</button>

<br><br>
<progress id="prog" value="0" max="100"></progress>
<div id="status"></div>

<h3>System Actions</h3>

<button onclick="restartESP()">Restart ESP</button>
<br><br>

<button onclick="wifiReset()">WiFi Reset</button>
<br><br>

<button onclick="factoryReset()">Factory Reset</button>
<br><br>

<h3>Storage Information</h3>
<table>
<tr>
  <td>SPIFFS Total:</td>
  <td>
    <span id="spiffs_total">...</span><br>
    <small id="spiffs_total_pct"></small>
  </td>
</tr>
<tr>
  <td>SPIFFS Used:</td>
  <td>
    <span id="spiffs_used">...</span><br>
    <small id="spiffs_used_pct"></small>
  </td>
</tr>
<tr>
  <td>SPIFFS Free:</td>
  <td>
    <span id="spiffs_free">...</span><br>
    <small id="spiffs_free_pct"></small>
  </td>
</tr>

<tr>
  <td>NVS Total:</td>
  <td>
    <span id="nvs_total">...</span><br>
    <small id="nvs_total_pct"></small>
  </td>
</tr>
<tr>
  <td>NVS Used:</td>
  <td>
    <span id="nvs_used">...</span><br>
    <small id="nvs_used_pct"></small>
  </td>
</tr>
<tr>
  <td>NVS Free:</td>
  <td>
    <span id="nvs_free">...</span><br>
    <small id="nvs_free_pct"></small>
  </td>
</tr>
</table>



<script>
// ------------------------------------------------------------
// Load firmware version from ESP
// ------------------------------------------------------------
fetch("/api/version")
    .then(r => r.text())
    .then(v => {
        document.getElementById("fwver").innerText = v;
    });

// ------------------------------------------------------------
// OTA Upload (multipart/form-data!)
// ------------------------------------------------------------
function upload() {
    let file = document.getElementById("fw").files[0];
    if (!file) {
        alert("Please select a .bin file");
        return;
    }

    let prog = document.getElementById("prog");
    let status = document.getElementById("status");

    prog.style.display = "block";
    prog.value = 0;
    status.innerText = "Uploading...";

    // Create multipart FormData
    let formData = new FormData();
    formData.append("fw", file);

    let xhr = new XMLHttpRequest();
    xhr.open("POST", "/api/ota");

    xhr.upload.onprogress = function(e) {
        if (e.lengthComputable) {
            let p = Math.round((e.loaded / e.total) * 100);
            prog.value = p;
            status.innerText = "Upload: " + p + "%";
        }
    };

    xhr.onload = function() {
        if (xhr.status == 200) {
            status.innerText = "Update successful! ESP is restarting...";
            prog.value = 100;

            setTimeout(() => {
                location.reload();
            }, 5000);
        } else {
            status.innerText = "Error: " + xhr.responseText;
        }
    };

    xhr.send(formData);
}
// ---------------------------------------------
// ESP Restart
// ---------------------------------------------
function restartESP() {
    if (!confirm("The ESP will be restarted.")) return;

    fetch("/api/restart", { method: "POST" })
        .then(() => {
            alert("Restarting");
        });
}

// ---------------------------------------------
// WiFi Reset
// ---------------------------------------------
function wifiReset() {
    if (!confirm("WiFi settings will be deleted. Continue?")) return;

    fetch("/api/wifireset", { method: "POST" })
        .then(() => {
            alert("WiFi reset done. ESP is restarting.");
        });
}

// ---------------------------------------------
// Factory Reset
// ---------------------------------------------
function factoryReset() {
    if (!confirm("ALL user data will be deleted!\nPerform factory reset?")) return;

    fetch("/api/factoryreset", { method: "POST" })
        .then(() => {
            alert("Factory reset done. ESP is restarting.");
        });
}
// -------------------------------------------------------------
// Load storage info
// -------------------------------------------------------------
fetch("/api/storageinfo")
  .then(r => r.json())
  .then(info => {

    // setText: tolerate a missing element so one absent id cannot abort the
    // whole storage render chain.
    const setText = (id, text) => {
      const el = document.getElementById(id);
      if (el) el.innerText = text;
    };

    // --- SPIFFS ---
    setText("spiffs_total", info.spiffs_total + " Bytes");
    setText("spiffs_used",  info.spiffs_used  + " Bytes");
    setText("spiffs_free",  info.spiffs_free  + " Bytes");

    let spiffs_total = info.spiffs_total;
    let spiffs_used  = info.spiffs_used;
    let spiffs_free  = info.spiffs_free;

    setText("spiffs_total_pct",
        Math.round((spiffs_total / spiffs_total) * 100) + "% of 100%");
    setText("spiffs_used_pct",
        Math.round((spiffs_used / spiffs_total) * 100) + "% used");
    setText("spiffs_free_pct",
        Math.round((spiffs_free / spiffs_total) * 100) + "% free");

    // --- NVS ---
    setText("nvs_total", info.nvs_total + " entries");
    setText("nvs_used",  info.nvs_used  + " entries");
    setText("nvs_free",  info.nvs_free  + " entries");

    let nvs_total = info.nvs_total;
    let nvs_used  = info.nvs_used;
    let nvs_free  = info.nvs_free;

    setText("nvs_total_pct",
        Math.round((nvs_total / nvs_total) * 100) + "% of 100%");
    setText("nvs_used_pct",
        Math.round((nvs_used / nvs_total) * 100) + "% used");
    setText("nvs_free_pct",
        Math.round((nvs_free / nvs_total) * 100) + "% free");
  });


</script>


</body>
</html>
)rawliteral";