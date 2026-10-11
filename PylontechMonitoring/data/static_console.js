function sendCmd() {
    const cmd = document.getElementById('cmdline').value;
    if (cmd.length === 0) return;

    fetch('/req?code=' + encodeURIComponent(cmd))
        .then(() => loadFrame());

    document.getElementById('cmdline').value = '';
}

function quickCmd(c) {
    fetch('/req?code=' + encodeURIComponent(c))
        .then(() => loadFrame());
}

function loadFrame() {
    fetch('/api/lastframe')
        .then(r => r.text())
        .then(t => {
            const box = document.getElementById('rawout');
            box.value = t;
            box.scrollTop = box.scrollHeight;
        });
}

document.getElementById('cmdline').addEventListener('keydown', e => {
    if (e.key === 'Enter') {
        e.preventDefault();
        sendCmd();
    }
});
