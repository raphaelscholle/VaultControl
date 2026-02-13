const el = (id) => document.getElementById(id);

const state = {
  angle: 90,
};

async function fetchStatus() {
  try {
    const res = await fetch('/api/status');
    const s = await res.json();

    state.angle = s.angle;
    el('angle').value = s.angle;
    el('angleVal').textContent = s.angle + ' deg';
    el('raw').textContent = 'raw: ' + s.raw;
    el('cal').textContent = 'cal: ' + s.cal.toFixed(1) + '%';
    el('min').textContent = s.min;
    el('max').textContent = s.max;
    el('range').textContent = 'min: ' + s.min + ' | max: ' + s.max;
    el('pulse').textContent = 'pulse: ' + s.pulse + ' us';
    el('wifi').textContent = 'WiFi: ' + (s.wifi ? 'ON' : 'OFF');
    el('ip').textContent = 'IP: ' + s.ip;
    el('clients').textContent = 'Clients: ' + s.clients;

    const fill = Math.max(0, Math.min(100, s.cal));
    el('calFill').style.width = fill + '%';

    if (s.calibrating) {
      el('calState').textContent = 'Calibrating';
      el('noticeText').textContent = 'Calibration running';
    } else {
      el('calState').textContent = 'Idle';
      el('noticeText').textContent = 'Calibration idle';
    }
  } catch (err) {
    el('wifi').textContent = 'WiFi: OFF';
  }
}

async function setAngle(angle) {
  state.angle = angle;
  el('angleVal').textContent = angle + ' deg';
  await fetch('/api/set?angle=' + angle);
}

el('angle').addEventListener('input', (e) => setAngle(e.target.value));
el('startBtn').addEventListener('click', () => fetch('/api/calibrate?cmd=start'));
el('stopBtn').addEventListener('click', () => fetch('/api/calibrate?cmd=stop'));
el('resetBtn').addEventListener('click', () => fetch('/api/calibrate?cmd=reset'));

setInterval(fetchStatus, 700);
fetchStatus();
