const el = (id) => document.getElementById(id);

const state = {
  angle: 90,
  pulse: 1500,
};

const clampPulse = (val) => {
  const num = Number(val);
  if (Number.isNaN(num)) return 1500;
  return Math.max(300, Math.min(3000, Math.round(num)));
};

let pulseSendTimer;

function queuePulseSend(value) {
  const pulse = clampPulse(value);
  setPulseUi(pulse);
  clearTimeout(pulseSendTimer);
  pulseSendTimer = setTimeout(() => {
    fetch('/api/servo?pulse=' + pulse);
  }, 120);
}

function setIfNotFocused(id, value) {
  const node = el(id);
  if (document.activeElement === node) {
    return;
  }
  node.value = value;
}

function setPulseUi(pulse) {
  const value = clampPulse(pulse);
  state.pulse = value;
  el('pulseVal').textContent = value + ' us';
  setIfNotFocused('pulseRange', value);
  setIfNotFocused('pulseInput', value);
}

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

    setPulseUi(s.pulse);
    setIfNotFocused('servoMin', s.servoMinUs);
    setIfNotFocused('servoMax', s.servoMaxUs);
    setIfNotFocused('servoZero', s.servoZeroUs);
    el('servoRange').textContent = 'range: ' + s.servoMinUs + '-' + s.servoMaxUs;

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
el('pulseRange').addEventListener('input', (e) => queuePulseSend(e.target.value));
el('pulseInput').addEventListener('input', (e) => queuePulseSend(e.target.value));
el('setMinBtn').addEventListener('click', () => setIfNotFocused('servoMin', state.pulse));
el('setMaxBtn').addEventListener('click', () => setIfNotFocused('servoMax', state.pulse));
el('setZeroBtn').addEventListener('click', () => setIfNotFocused('servoZero', state.pulse));
el('zeroBtn').addEventListener('click', () => fetch('/api/servo?cmd=zero'));
el('saveServoBtn').addEventListener('click', () => {
  const min = clampPulse(el('servoMin').value);
  const max = clampPulse(el('servoMax').value);
  const zero = clampPulse(el('servoZero').value);
  fetch('/api/servo?cmd=save&min=' + min + '&max=' + max + '&zero=' + zero);
});
el('resetServoBtn').addEventListener('click', () => fetch('/api/servo?cmd=reset'));

setInterval(fetchStatus, 700);
fetchStatus();
