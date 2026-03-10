const statusPill = document.getElementById("statusPill");
const telTickerEl = null;
const legendAudioEl = document.getElementById("legendAudio");

const debugOverlay = document.getElementById("debugOverlay");
const debugPerfEl = document.getElementById("debugPerf");
const debugSafetyEl = document.getElementById("debugSafety");

const state = {
  audio: null,
  debugEnabled: false,
};

const AUDIO_TYPE = {
  PLAY_TRACK: 0,
  STOP: 1,
  PAUSE: 2,
  RESUME: 3,
  SET_VOLUME: 4,
  LOOP_TRACK: 5,
};

const SOUND_TRACK_LABELS = {
  2: "grumbly01",
  3: "okay okay",
  4: "okay follow me",
  5: "grumbly02",
  6: "yes i would",
  8: "grumpy03",
  12: "now",
  14: "what groan",
  15: "wah3",
  16: "tada",
  17: "chatty",
  20: "extended grumble",
  21: "grumbly1",
  24: "uh oh",
  32: "swr stinger",
  33: "purr3",
  254: "mandolorian",
  255: "imperial carol bells",
};

const AXIS_MAX = 512;
const STICK_MAX_DEFLECTION = 12;

// ── Controller slot definitions ──────────────────────────────────────

const CONTROLLER_SLOTS = [
  { id: 0, role: "drive",     type: "left",  corner: "bl", stickCX: 41, stickCY: 65 },
  { id: 1, role: "dome",      type: "right", corner: "br", stickCX: 59, stickCY: 120 },
  { id: 2, role: "animation", type: "left",  corner: "tl", stickCX: 41, stickCY: 65 },
  { id: 3, role: "camera",    type: "right", corner: "tr", stickCX: 59, stickCY: 120 },
];

// Per-controller state
const controllers = CONTROLLER_SLOTS.map(() => ({
  connected: false,
  buttonMask: 0,
  miscMask: 0,
  axes: [0, 0, 0, 0],
  playerLeds: null,
  role: null,
}));

// ── SVG Templates ────────────────────────────────────────────────────

function leftJoyConSVG(slotId) {
  const pfx = `jc${slotId}`;
  return `<svg class="jc-svg" viewBox="0 0 100 200" overflow="visible" xmlns="http://www.w3.org/2000/svg">
  <!-- Body -->
  <path class="jc-body" id="${pfx}-body" d="M 78,5 C 45,5 5,5 5,50 L 5,150 C 5,195 45,195 78,195 Z"/>
  <!-- Rail -->
  <rect class="jc-rail" x="76" y="24" width="8" height="152" rx="2"/>
  <!-- ZL trigger (thin strip above body outline, same offset as L below) -->
  <path class="jc-btn jc-trigger" data-side="left" data-bit="7"
        d="M 76,-6 C 54,-6 28,-6 15,6 L 19,11 C 34,1 58,1 76,1 Z"/>
  <text class="jc-label jc-label-xs" x="46" y="-1">ZL</text>
  <!-- L button (inset inside body top curve) -->
  <path class="jc-btn" data-side="left" data-bit="6"
        d="M 76,5 C 46,5 36,7 22,14 L 25,21 C 38,15 54,13 76,13 Z"/>
  <text class="jc-label jc-label-xs" x="48" y="11">L</text>
  <!-- Minus -->
  <rect class="jc-btn" data-side="left" data-misc-bit="1" x="56" y="33" width="12" height="4" rx="1.5"/>
  <!-- Joystick -->
  <circle class="jc-stick-well" cx="41" cy="65" r="18"/>
  <circle class="jc-stick-dot" id="${pfx}-stick" cx="41" cy="65" r="7"/>
  <!-- Stick press -->
  <circle class="jc-btn-hidden" data-side="left" data-bit="8" cx="41" cy="65" r="18"/>
  <!-- D-pad -->
  <circle class="jc-btn-face" data-side="left" data-bit="3" cx="41" cy="105" r="6.5"/>
  <polygon class="jc-label" points="41,101 44.5,107 37.5,107"/>
  <circle class="jc-btn-face" data-side="left" data-bit="0" cx="41" cy="131" r="6.5"/>
  <polygon class="jc-label" points="41,135 44.5,129 37.5,129"/>
  <circle class="jc-btn-face" data-side="left" data-bit="2" cx="28" cy="118" r="6.5"/>
  <polygon class="jc-label" points="24,118 30,114.5 30,121.5"/>
  <circle class="jc-btn-face" data-side="left" data-bit="1" cx="54" cy="118" r="6.5"/>
  <polygon class="jc-label" points="58,118 52,114.5 52,121.5"/>
  <!-- Capture -->
  <rect class="jc-btn" data-side="left" data-misc-bit="2" x="42" y="162" width="12" height="12" rx="2"/>
  <circle class="jc-label" cx="48" cy="168" r="3.5" fill="none" stroke-width="1"/>
  <!-- SL / SR on rail side -->
  <rect class="jc-btn" data-side="left" data-bit="4" x="81" y="47" width="6" height="14" rx="2"/>
  <text class="jc-label jc-label-xs" x="84" y="56">SL</text>
  <rect class="jc-btn" data-side="left" data-bit="5" x="81" y="139" width="6" height="14" rx="2"/>
  <text class="jc-label jc-label-xs" x="84" y="148">SR</text>
  <!-- Player LEDs -->
  <rect class="jc-player-led" id="${pfx}-pled-0" x="76" y="89" width="5" height="5" rx="1"/>
  <rect class="jc-player-led" id="${pfx}-pled-1" x="76" y="96" width="5" height="5" rx="1"/>
  <rect class="jc-player-led" id="${pfx}-pled-2" x="76" y="103" width="5" height="5" rx="1"/>
  <rect class="jc-player-led" id="${pfx}-pled-3" x="76" y="110" width="5" height="5" rx="1"/>
  <!-- System -->
  <circle class="jc-btn-hidden" data-side="left" data-misc-bit="0" cx="20" cy="35" r="5"/>
</svg>`;
}

function rightJoyConSVG(slotId) {
  const pfx = `jc${slotId}`;
  return `<svg class="jc-svg" viewBox="0 0 100 200" overflow="visible" xmlns="http://www.w3.org/2000/svg">
  <!-- Body -->
  <path class="jc-body" id="${pfx}-body" d="M 22,5 C 55,5 95,5 95,50 L 95,150 C 95,195 55,195 22,195 Z"/>
  <!-- Rail -->
  <rect class="jc-rail" x="16" y="24" width="8" height="152" rx="2"/>
  <!-- ZR trigger (thin strip above body outline, same offset as R below) -->
  <path class="jc-btn jc-trigger" data-side="right" data-bit="7"
        d="M 24,-6 C 46,-6 72,-6 85,6 L 81,11 C 66,1 42,1 24,1 Z"/>
  <text class="jc-label jc-label-xs" x="54" y="-1">ZR</text>
  <!-- R button (inset inside body top curve) -->
  <path class="jc-btn" data-side="right" data-bit="6"
        d="M 24,5 C 54,5 64,7 78,14 L 75,21 C 62,15 46,13 24,13 Z"/>
  <text class="jc-label jc-label-xs" x="52" y="11">R</text>
  <!-- Plus -->
  <g class="jc-btn" data-side="right" data-misc-bit="2">
    <rect x="32" y="35" width="12" height="4" rx="1.5"/>
    <rect x="36" y="31" width="4" height="12" rx="1.5"/>
  </g>
  <!-- ABXY face buttons -->
  <circle class="jc-btn-face" data-side="right" data-bit="3" cx="59" cy="52" r="6.5"/>
  <text class="jc-label jc-label-sm" x="59" y="55">X</text>
  <circle class="jc-btn-face" data-side="right" data-bit="0" cx="59" cy="78" r="6.5"/>
  <text class="jc-label jc-label-sm" x="59" y="81">B</text>
  <circle class="jc-btn-face" data-side="right" data-bit="2" cx="46" cy="65" r="6.5"/>
  <text class="jc-label jc-label-sm" x="46" y="68">Y</text>
  <circle class="jc-btn-face" data-side="right" data-bit="1" cx="72" cy="65" r="6.5"/>
  <text class="jc-label jc-label-sm" x="72" y="68">A</text>
  <!-- Joystick -->
  <circle class="jc-stick-well" cx="59" cy="120" r="18"/>
  <circle class="jc-stick-dot" id="${pfx}-stick" cx="59" cy="120" r="7"/>
  <!-- Stick press -->
  <circle class="jc-btn-hidden" data-side="right" data-bit="8" cx="59" cy="120" r="18"/>
  <!-- Home -->
  <circle class="jc-btn" data-side="right" data-misc-bit="1" cx="52" cy="162" r="6"/>
  <g class="jc-label" pointer-events="none">
    <polygon points="52,159 49,162 55,162"/>
    <rect x="49.5" y="162" width="5" height="3.5" rx="0.5"/>
  </g>
  <!-- SL / SR on rail side -->
  <rect class="jc-btn" data-side="right" data-bit="4" x="13" y="47" width="6" height="14" rx="2"/>
  <text class="jc-label jc-label-xs" x="16" y="56">SL</text>
  <rect class="jc-btn" data-side="right" data-bit="5" x="13" y="139" width="6" height="14" rx="2"/>
  <text class="jc-label jc-label-xs" x="16" y="148">SR</text>
  <!-- Player LEDs -->
  <rect class="jc-player-led" id="${pfx}-pled-0" x="19" y="89" width="5" height="5" rx="1"/>
  <rect class="jc-player-led" id="${pfx}-pled-1" x="19" y="96" width="5" height="5" rx="1"/>
  <rect class="jc-player-led" id="${pfx}-pled-2" x="19" y="103" width="5" height="5" rx="1"/>
  <rect class="jc-player-led" id="${pfx}-pled-3" x="19" y="110" width="5" height="5" rx="1"/>
  <!-- System -->
  <circle class="jc-btn-hidden" data-side="right" data-misc-bit="0" cx="80" cy="35" r="5"/>
</svg>`;
}

// ── Initialize controller overlays ───────────────────────────────────

function initControllerSlots() {
  for (const slot of CONTROLLER_SLOTS) {
    const container = document.getElementById(`jc-slot-${slot.id}`);
    if (!container) continue;

    const svgWrap = document.createElement("div");
    svgWrap.innerHTML = slot.type === "left" ? leftJoyConSVG(slot.id) : rightJoyConSVG(slot.id);
    container.appendChild(svgWrap);

    const maskEl = document.createElement("div");
    maskEl.className = "jc-mask";
    maskEl.id = `jc${slot.id}-mask`;
    maskEl.innerHTML = "mask: -<br>misc: -";
    container.appendChild(maskEl);
  }
}

initControllerSlots();

// ── Utility functions ────────────────────────────────────────────────

const statusLabel = document.getElementById("statusLabel");
const statusDetail = document.getElementById("statusDetail");

function updateStatus(msg) {
  const connected = !!msg.connected;
  const mode = msg.mode || "unknown";
  const text = msg.message || (connected ? "Connected" : "Disconnected");

  statusPill.classList.toggle("online", connected);
  statusPill.classList.toggle("offline", !connected);

  if (connected) {
    statusLabel.textContent = "Receiving";
  } else {
    statusLabel.textContent = "No telemetry";
  }

  let detail = "";
  if (mode === "simulate") {
    detail = "Simulation";
  } else if (mode === "serial" && msg.serial_port) {
    detail = `Serial: ${msg.serial_port}`;
  } else if (mode === "wifi" && msg.ip_address) {
    detail = `WiFi: ${msg.ip_address}`;
  } else if (text) {
    detail = text;
  }
  statusDetail.textContent = detail;
}

function hexMask(mask) {
  if (mask === null || mask === undefined) {
    return "-";
  }
  return `0x${Number(mask).toString(16).padStart(4, "0")}`;
}

function applyMask(slotId, side, buttonMask, miscMask) {
  const container = document.getElementById(`jc-slot-${slotId}`);
  if (!container) return;
  container.querySelectorAll(".jc-btn[data-side], .jc-btn-face[data-side], .jc-btn-hidden[data-side]").forEach((el) => {
    if (el.dataset.side !== side) return;
    let active = false;
    if (el.dataset.bit !== undefined) {
      const bit = Number(el.dataset.bit);
      active = Number.isFinite(buttonMask) && ((buttonMask >> bit) & 1) === 1;
    } else if (el.dataset.miscBit !== undefined) {
      const miscBit = Number(el.dataset.miscBit);
      active = Number.isFinite(miscMask) && ((miscMask >> miscBit) & 1) === 1;
    }
    el.classList.toggle("active", active);
  });
}

function setControllerConnected(slotId, connected, role) {
  const body = document.getElementById(`jc${slotId}-body`);
  if (!body) return;
  const on = !!connected;
  body.classList.toggle("connected", on);
  body.classList.toggle("disconnected", !on);
}

function setPlayerLeds(slotId, connected, ledMask) {
  for (let i = 0; i < 4; i++) {
    const led = document.getElementById(`jc${slotId}-pled-${i}`);
    if (!led) continue;
    let on;
    if (ledMask !== null && ledMask !== undefined && Number.isFinite(ledMask)) {
      on = ((ledMask >> i) & 1) === 1;
    } else {
      on = connected;
    }
    led.classList.toggle("on", on);
  }
}

function applyStickPosition(slotId, axes, centerX, centerY) {
  const stickEl = document.getElementById(`jc${slotId}-stick`);
  if (!stickEl || !axes || axes.length < 2) return;
  const ax = Number(axes[0]) || 0;
  const ay = Number(axes[1]) || 0;
  const nx = Math.max(-1, Math.min(1, ax / AXIS_MAX));
  const ny = Math.max(-1, Math.min(1, ay / AXIS_MAX));
  stickEl.setAttribute("cx", centerX + nx * STICK_MAX_DEFLECTION);
  stickEl.setAttribute("cy", centerY + ny * STICK_MAX_DEFLECTION);
}

function updateMaskDisplay(slotId, buttonMask, miscMask) {
  const el = document.getElementById(`jc${slotId}-mask`);
  if (!el) return;
  el.innerHTML = `mask: ${hexMask(buttonMask)}<br>misc: ${hexMask(miscMask)}`;
}

// ── Panel toggle nav ─────────────────────────────────────────────────

document.querySelectorAll(".nav-toggle[data-panel]").forEach((btn) => {
  btn.addEventListener("click", () => {
    const panel = document.getElementById(btn.dataset.panel);
    if (!panel) return;
    const isVisible = !panel.classList.contains("hidden");
    panel.classList.toggle("hidden", isVisible);
    btn.classList.toggle("active", !isVisible);
  });
});

// ── Toolbar collapse chevron ─────────────────────────────────────────

const toolbarChevron = document.getElementById("toolbarChevron");
const viewportToolbar = document.getElementById("viewportToolbar");

toolbarChevron.addEventListener("click", () => {
  const collapsed = viewportToolbar.classList.toggle("collapsed");
  toolbarChevron.innerHTML = collapsed ? "&#9650;" : "&#9660;";
  toolbarChevron.title = collapsed ? "Show toolbar" : "Hide toolbar";
});

// ── Debug menu tree ─────────────────────────────────────────────────

const labelLayerEl = document.getElementById("labelLayer");
const debugMenuBtn = document.getElementById("toggleDebugMenu");
const debugMenuEl = document.getElementById("debugMenu");
const debugMotorsEl = document.getElementById("debugMotors");
const debugServosEl = document.getElementById("debugServos");
const debugLedsEl = document.getElementById("debugLeds");
const debugSoundEl = document.getElementById("debugSound");

// Track which sections are enabled (shared with render3d.js via window)
const debugSections = {};
debugMenuEl.querySelectorAll("input[data-debug]").forEach((cb) => {
  debugSections[cb.dataset.debug] = cb.checked;
});
window.debugSections = debugSections;

// Toggle menu open/close
debugMenuBtn.addEventListener("click", (e) => {
  e.stopPropagation();
  debugMenuEl.classList.toggle("hidden");
});

// Close menu on outside click
document.addEventListener("click", (e) => {
  if (!debugMenuEl.contains(e.target) && e.target !== debugMenuBtn) {
    debugMenuEl.classList.add("hidden");
  }
});

// "All" toggle
const debugToggleAll = document.getElementById("debugToggleAll");
debugToggleAll.addEventListener("change", () => {
  const on = debugToggleAll.checked;
  debugMenuEl.querySelectorAll("input[data-debug]").forEach((cb) => {
    cb.checked = on;
    debugSections[cb.dataset.debug] = on;
  });
  debugToggleAll.indeterminate = false;
  updateDebugVisibility();
});

// Handle individual checkbox toggles
debugMenuEl.addEventListener("change", (e) => {
  const cb = e.target;
  if (!cb.dataset.debug) return;
  debugSections[cb.dataset.debug] = cb.checked;
  syncAllToggle();
  updateDebugVisibility();
});

function syncAllToggle() {
  const cbs = [...debugMenuEl.querySelectorAll("input[data-debug]")];
  const allChecked = cbs.every((cb) => cb.checked);
  const noneChecked = cbs.every((cb) => !cb.checked);
  debugToggleAll.checked = allChecked;
  debugToggleAll.indeterminate = !allChecked && !noneChecked;
}

function updateDebugVisibility() {
  const anyEnabled = Object.values(debugSections).some(Boolean);
  state.debugEnabled = anyEnabled;
  debugMenuBtn.classList.toggle("active", anyEnabled);
  debugOverlay.classList.toggle("hidden", !anyEnabled);
  if (labelLayerEl) labelLayerEl.classList.toggle("hidden", !anyEnabled);

  // Toggle individual sections
  debugOverlay.querySelectorAll("[data-debug-section]").forEach((el) => {
    const key = el.dataset.debugSection;
    el.classList.toggle("hidden", !debugSections[key]);
  });
}

// Initial visibility
syncAllToggle();
updateDebugVisibility();

function renderDebugOverlay(msg) {
  if (!state.debugEnabled) return;

  if (debugSections.perf && msg.loop_count !== undefined) {
    debugPerfEl.textContent =
      `loops: ${msg.loop_count ?? "-"}  max: ${msg.max_loop_time_us ?? "-"}µs  avg: ${msg.avg_loop_time_us ?? "-"}µs\nnodes: ${msg.active_nodes ?? "-"}/${msg.total_nodes ?? "-"}`;
  }

  if (debugSections.safety) {
    const mode = msg.degradation_mode ?? "-";
    const eExec = msg.executor_estop ? "YES" : "no";
    const eSafe = msg.safety_estop ? "YES" : "no";
    debugSafetyEl.textContent = `mode: ${mode}  e-stop exec: ${eExec}  safety: ${eSafe}`;
  }

  if (debugSections.motors && msg.motors) {
    const lines = msg.motors.map((m) => `m${m.id}: ${m.value !== null && m.value !== undefined ? Number(m.value).toFixed(3) : "--"}`);
    debugMotorsEl.textContent = lines.length ? lines.join("  ") : "--";
  }

  if (debugSections.servos && msg.servos) {
    const lines = msg.servos.map((s) => {
      const g = s.group ? `${s.group}:` : "";
      return `${g}${s.id}: ${s.value !== null && s.value !== undefined ? Number(s.value).toFixed(0) : "--"}`;
    });
    debugServosEl.textContent = lines.length ? lines.join("  ") : "--";
  }

  if (debugSections.leds && msg.leds) {
    const lines = msg.leds.map((l) => {
      const c = l.color;
      const r = c?.r ?? c?.red ?? 0;
      const g = c?.g ?? c?.green ?? 0;
      const b = c?.b ?? c?.blue ?? 0;
      return `${l.id}:(${r},${g},${b})`;
    });
    debugLedsEl.textContent = lines.length ? lines.join("  ") : "--";
  }

  if (debugSections.sound && state.audio) {
    const a = state.audio;
    const status = a.status || "idle";
    const track = formatTrackLabel(a.track);
    const vol = Number.isFinite(a.volume) ? a.volume : "-";
    const typeName = Object.entries(AUDIO_TYPE).find(([, v]) => v === a.type)?.[0] || "-";
    debugSoundEl.textContent = `status: ${status}  type: ${typeName}  track: ${track || "-"}  vol: ${vol}`;
  }
}

// ── Audio helpers ─────────────────────────────────────────────────────

function toFiniteNumber(value) {
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

function toOptionalBool(value) {
  if (value === true || value === false) return value;
  if (value === null || value === undefined) return null;
  if (typeof value === "number") return value !== 0;
  return null;
}

function resolveAudioStatus(audioType, audioValid, previousStatus) {
  if (audioValid === false) return "idle";
  if (audioType === AUDIO_TYPE.PLAY_TRACK || audioType === AUDIO_TYPE.RESUME || audioType === AUDIO_TYPE.LOOP_TRACK) return "playing";
  if (audioType === AUDIO_TYPE.STOP || audioType === AUDIO_TYPE.PAUSE) return "finished";
  return previousStatus || "idle";
}

function formatTrackLabel(trackNumber) {
  if (!Number.isFinite(trackNumber) || trackNumber <= 0) return "";
  const friendly = SOUND_TRACK_LABELS[trackNumber];
  return friendly ? `${friendly} (#${trackNumber})` : `#${trackNumber}`;
}

function renderAudio() {
  if (!legendAudioEl) return;
  if (!state.audio) {
    legendAudioEl.innerHTML = "";
    return;
  }
  const a = state.audio;
  const status = a.status || "idle";
  const pillClass = status === "playing" ? " playing" : " idle";
  const trackLabel = formatTrackLabel(a.track);
  const detail = trackLabel ? ` ${trackLabel}` : "";
  legendAudioEl.innerHTML = `<span class="legend-audio-pill${pillClass}">${status}${detail}</span>`;
}

// ── Controller update from telemetry ─────────────────────────────────

function updateController(slotId, data) {
  if (!data) return;
  const slot = CONTROLLER_SLOTS[slotId];
  if (!slot) return;

  const side = slot.type;
  const connected = !!data.connected;
  const buttonMask = data.buttons ?? 0;
  const miscMask = data.misc ?? 0;
  const axes = data.axes || [0, 0, 0, 0];
  const playerLeds = data.player_leds;
  const role = data.role || slot.role;

  setControllerConnected(slotId, connected, role);
  applyMask(slotId, side, buttonMask, miscMask);
  applyStickPosition(slotId, axes, slot.stickCX, slot.stickCY);
  setPlayerLeds(slotId, connected, playerLeds);
  updateMaskDisplay(slotId, buttonMask, miscMask);
}

// ── Telemetry handler ────────────────────────────────────────────────

function handleTelemetry(msg) {
  // Mark status as connected on any telemetry reception
  if (!statusPill.classList.contains("online")) {
    updateStatus({ connected: true, mode: msg.format || "unknown" });
  }

  // Handle new 4-controller format
  if (msg.controllers && Array.isArray(msg.controllers)) {
    for (let i = 0; i < msg.controllers.length && i < 4; i++) {
      updateController(i, msg.controllers[i]);
    }
  }
  // Backward compat: old joycon format -> map to slots 0 and 1
  else if (msg.joycon) {
    const joy = msg.joycon;
    updateController(0, {
      connected: joy.left_connected,
      buttons: joy.left_mask,
      misc: joy.left_misc,
      axes: joy.left_axes,
      player_leds: joy.left_player_leds,
      role: joy.left_role,
    });
    updateController(1, {
      connected: joy.right_connected,
      buttons: joy.right_mask,
      misc: joy.right_misc,
      axes: joy.right_axes,
      player_leds: joy.right_player_leds,
      role: joy.right_role,
    });
  }

  if (msg.audio && typeof msg.audio === "object") {
    const previous = state.audio || {};
    const audioValid = toOptionalBool(msg.audio.valid);
    const nextType = toFiniteNumber(msg.audio.type);
    const incomingTrack = toFiniteNumber(msg.audio.track);
    const resolvedTrack = (audioValid === false)
      ? null
      : ((incomingTrack && incomingTrack > 0)
      ? incomingTrack
      : (Number.isFinite(previous.track) ? previous.track : null));
    state.audio = {
      ...previous,
      ...msg.audio,
      valid: audioValid,
      type: nextType,
      track: resolvedTrack,
      volume: toFiniteNumber(msg.audio.volume),
      status: resolveAudioStatus(nextType, audioValid, previous.status),
      updatedAt: Date.now(),
    };
  }

  renderAudio();
  renderDebugOverlay(msg);
  window.dispatchEvent(new CustomEvent("telemetry:update", { detail: msg }));
}

// ── WebSocket connection ─────────────────────────────────────────────

function connect() {
  const protocol = window.location.protocol === "https:" ? "wss" : "ws";
  const ws = new WebSocket(`${protocol}://${window.location.host}/ws`);

  updateStatus({ connected: false, message: "Connecting to server..." });

  ws.onopen = () => {};

  ws.onclose = () => {
    updateStatus({ connected: false, message: "Server disconnected, retrying..." });
    setTimeout(connect, 1000);
  };

  ws.onerror = () => {
    updateStatus({ connected: false, message: "Connection error" });
  };

  ws.onmessage = (event) => {
    try {
      const msg = JSON.parse(event.data);
      if (msg.kind === "status") {
        updateStatus(msg);
        return;
      }
      if (msg.kind === "telemetry") {
        handleTelemetry(msg);
        return;
      }
      if (msg.kind === "parse_error") {
        console.warn("[parse error]", msg.raw || "");
      }
    } catch (err) {
      console.warn("[ws parse error]", err);
    }
  };
}

connect();
