// Backend replies stay in English; map the known responses for DISPLAY only.
var LOG_MSG_KEYS = {
    'Log level updated': 'log.msg_saved',
    'Missing body': 'log.msg_missing_body',
    'Invalid JSON': 'log.msg_invalid_json'
};

function logMsg(raw) {
    var key = LOG_MSG_KEYS[raw];
    return key ? t(key) : raw;
}

function loadLog() {
    fetch('/api/log')
        .then(r => r.text())
        .then(t => {
            const box = document.getElementById('logbox');
            const nearBottom =
                box.scrollHeight - box.scrollTop - box.clientHeight < 40;
            box.textContent = t;
            if (nearBottom) box.scrollTop = box.scrollHeight;
        })
        .catch(() => {
            document.getElementById('logbox').textContent = t('log.err_loading');
        });
}

// Auto-refresh (3s) only while the log page is visible.
function logAutoRefresh(enable) {
    if (window._logAutoTimer) {
        clearInterval(window._logAutoTimer);
        window._logAutoTimer = null;
    }
    if (enable && !document.hidden) {
        window._logAutoTimer = setInterval(loadLog, 3000);
    }
}

if (!window._logVisBound) {
    window._logVisBound = true;
    document.addEventListener("visibilitychange",
        () => logAutoRefresh(!document.hidden));
}

async function loadLogLevel() {
    let r = await fetch('/api/log/level');
    let j = await r.json();

    document.getElementById('log_info').checked = j.info;
    document.getElementById('log_warn').checked = j.warn;
    document.getElementById('log_error').checked = j.error;
    document.getElementById('log_debug').checked = j.debug;
}

async function saveLogLevel() {
    let data = {
        info: document.getElementById('log_info').checked,
        warn: document.getElementById('log_warn').checked,
        error: document.getElementById('log_error').checked,
        debug: document.getElementById('log_debug').checked
    };

    let r = await fetch('/api/log/level', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(data)
    });

    const m = document.getElementById('logmsg');
    m.style.color = r.ok ? '#00ff00' : '#ff4444';
    m.innerText = logMsg(await r.text());
}

loadLog();
loadLogLevel();
logAutoRefresh(true);
// Load log and log level on startup and refresh every 3s
