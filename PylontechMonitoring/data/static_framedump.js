// Top-level declarations use var on purpose: the SPA re-injects and re-executes
// this script on every navigation, and a top-level let/const would throw
// "Identifier already declared" on the second run. var redeclaration is legal
// and keeps the script re-runnable; window.pageCleanup() stops the timer.
var MAX_FRAMES = 50;

var running = false;
var lastFrame = null;

function startTimer() {
    if (!window._framedumpTimer) {
        window._framedumpTimer = setInterval(fetchFrame, 1000);
    }
}

function stopTimer() {
    if (window._framedumpTimer) {
        clearInterval(window._framedumpTimer);
        window._framedumpTimer = null;
    }
}

function toggle() {
    running = !running;
    const btn = document.getElementById("toggleBtn");
    btn.textContent = running ? t("fd.stop") : t("fd.start");

    if (running) {
        lastFrame = null;
        startTimer();
    } else {
        stopTimer();
    }
}

function fetchFrame() {
    if (!running) return;

    fetch("/api/framedump")
        .then(r => r.text())
        .then(t => {
            if (!running) return;
            if (t.trim().length === 0) return;
            if (t === lastFrame) return;

            lastFrame = t;
            addFrame(t);
        })
        .catch(e => console.log(e));
}

function addFrame(text) {
    const dump = document.getElementById("dump");

    const entry = document.createElement("div");
    entry.className = "frame";

    const ts = document.createElement("div");
    ts.className = "frame-ts";
    ts.textContent = t("fd.timestamp").replace("{t}", timeStamp());
    entry.appendChild(ts);

    const body = document.createElement("pre");
    body.className = "frame-body";
    body.textContent = text;
    entry.appendChild(body);

    dump.prepend(entry);

    while (dump.childElementCount > MAX_FRAMES) {
        dump.removeChild(dump.lastElementChild);
    }

    updateCount();
}

function clearDump() {
    document.getElementById("dump").innerHTML = "";
    lastFrame = null;
    updateCount();
}

function updateCount() {
    document.getElementById("count").textContent =
        document.getElementById("dump").childElementCount + "/" + MAX_FRAMES;
}

function timeStamp() {
    const d = new Date();
    const pad = n => String(n).padStart(2, "0");
    return pad(d.getHours()) + ":" + pad(d.getMinutes()) + ":" + pad(d.getSeconds());
}

stopTimer();
var btn = document.getElementById("toggleBtn");
if (btn) btn.textContent = t("fd.start");
document.getElementById("count").textContent = "0/" + MAX_FRAMES;

// SPA navigation hook: stop the frame timer and the polling state on leave.
window.pageCleanup = function () {
    stopTimer();
    running = false;
};