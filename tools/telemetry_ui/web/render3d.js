const viewportEl = document.getElementById("robotViewport");
const modeEl = document.getElementById("robotMode");
const labelLayer = document.getElementById("labelLayer");

if (!viewportEl || !modeEl) {
  throw new Error("3D viewport container not found");
}

// ── Default mapping (used when joint_mapping.json is unavailable) ─────

const DEFAULT_SERVO_JOINT_MAP = {
  "body:0": "neck_leg_a_joint",
  "body:1": "neck_leg_b_joint",
  "body:2": "neck_leg_c_joint",
  "body:3": "body_utility_arm_joint",
  "body:4": "body_door_right_joint",
  "body:5": "body_door_left_joint",
  "dome:0": "periscope_lift_joint",
  "dome:1": "periscope_spin_joint",
  "dome:2": "dome_door_right_joint",
  "dome:3": "dome_arm_right_lift_joint",
  "dome:4": "dome_arm_right_extend_joint",
  "dome:5": "dome_arm_right_rotate_joint",
  "dome:6": "dome_door_left_joint",
  "dome:7": "dome_arm_left_lift_joint",
  "dome:8": "dome_arm_left_extend_joint",
  "dome:9": "dome_arm_left_rotate_joint",
};

const DEFAULT_MOTOR_JOINT_MAP = {
  0: "left_wheel_joint",
  1: "right_wheel_joint",
  2: "dome_spin_joint",
};

const DEFAULT_MOTOR_RAD_PER_SEC = { 0: 8.0, 1: 8.0, 2: 3.0 };

// ── Human-readable labels ─────────────────────────────────────────────

const MOTOR_NAMES = { 0: "L drive", 1: "R drive", 2: "Dome" };

const SERVO_NAMES = {
  "body:0": "Neck A",
  "body:1": "Neck B",
  "body:2": "Neck C",
  "body:3": "Util arm",
  "body:4": "Door R",
  "body:5": "Door L",
  "dome:0": "Periscope",
  "dome:1": "Peri spin",
  "dome:2": "Dome dr R",
  "dome:3": "Arm R lift",
  "dome:4": "Arm R ext",
  "dome:5": "Arm R rot",
  "dome:6": "Dome dr L",
  "dome:7": "Arm L lift",
  "dome:8": "Arm L ext",
  "dome:9": "Arm L rot",
};

const LED_NAMES = { 0: "Front LED", 1: "Right Eye", 2: "Centre Eye", 3: "Left Eye" };

// ── Runtime mapping (populated from config or defaults) ───────────────

let SERVO_JOINT_MAP = {};   // servoKey -> [jointName, ...]
let SERVO_CALIBRATION = {}; // servoKey -> { min, max, neutral }
let MOTOR_JOINT_MAP = {};   // motorId  -> jointName
let MOTOR_RAD_PER_SEC = {};
let LED_LINK_MAP = {};      // ledId -> linkName
let URDF_UP_AXIS = "Z";    // "Z" for hand-written, "Y" for Fusion

// ── State ──────────────────────────────────────────────────────────────

const SERVO_SMOOTH_RATE = 8.0; // exponential decay rate (higher = faster)
const SERVO_TYPE = {
  SET_POSITION: 0,
  SET_SPEED: 1,
  DISABLE: 2,
  ENABLE: 3,
};

const SERVO_VISUAL_OVERRIDES = {
  "dome:2": { openAt: "max", closedAt: "min", openNorm: -1, closedNorm: 0 },
  "dome:6": { openAt: "neutral", closedAt: "max", openNorm: 0, closedNorm: 1 },
};

const state = {
  servoValues: new Map(),
  servoTelemetry: new Map(),
  servoCurrentNorm: new Map(), // smoothed normalized servo values
  motorValues: new Map(),
  motorAngles: new Map(),
  ledData: new Map(),       // ledId -> { on, r, g, b, brightness }
  urdfRobot: null,
  fallbackParts: null,
  lastFrameTime: 0,
  groundHeading: 0,
  groundX: 0,
  groundZ: 0,
  pivotOffset: null, // world-space midpoint between drive wheels
};

// ── Pin labels ─────────────────────────────────────────────────────────

const pinLabels = []; // { anchor: Object3D, el: HTMLElement, key: string }

function createPinLabel(key, name) {
  const el = document.createElement("div");
  el.className = "pin-label";
  el.innerHTML =
    `<span class="pin-label-name">${name}</span>` +
    `<span class="pin-label-value" data-key="${key}">--</span>`;
  el.style.display = "none";
  labelLayer.appendChild(el);
  return el;
}

// ── Load mapping config ────────────────────────────────────────────────

async function loadMappingConfig() {
  try {
    const resp = await fetch("/static/joint_mapping.json");
    if (!resp.ok) {
      throw new Error(`HTTP ${resp.status}`);
    }
    const cfg = await resp.json();

    const servoMap = {};
    for (const section of [cfg.servo_joints, cfg.dome_servo_joints]) {
      if (!section) continue;
      for (const [key, value] of Object.entries(section)) {
        if (key.startsWith("_")) continue;
        servoMap[key] = Array.isArray(value) ? value : (value ? [value] : []);
      }
    }
    SERVO_JOINT_MAP = servoMap;

    const motorMap = {};
    if (cfg.motor_joints) {
      for (const [key, value] of Object.entries(cfg.motor_joints)) {
        if (key.startsWith("_")) continue;
        motorMap[Number(key)] = value;
      }
    }
    MOTOR_JOINT_MAP = motorMap;

    if (cfg.motor_rad_per_sec) {
      const speeds = {};
      for (const [key, value] of Object.entries(cfg.motor_rad_per_sec)) {
        if (key.startsWith("_")) continue;
        speeds[Number(key)] = Number(value);
      }
      MOTOR_RAD_PER_SEC = speeds;
    }

    if (cfg.led_links) {
      for (const [key, value] of Object.entries(cfg.led_links)) {
        if (key.startsWith("_")) continue;
        LED_LINK_MAP[key] = value;
      }
    }

    if (cfg.servo_calibration) {
      const cal = {};
      for (const [key, value] of Object.entries(cfg.servo_calibration)) {
        if (key.startsWith("_")) continue;
        cal[key] = { min: Number(value.min), max: Number(value.max), neutral: Number(value.neutral) };
      }
      SERVO_CALIBRATION = cal;
    }

    URDF_UP_AXIS = cfg.urdf_up_axis || "Z";

    console.log("Joint mapping loaded:", Object.keys(servoMap).length, "servos,", Object.keys(motorMap).length, "motors,", Object.keys(LED_LINK_MAP).length, "leds,", Object.keys(SERVO_CALIBRATION).length, "calibrations");
    return cfg;
  } catch (err) {
    console.warn("joint_mapping.json not available, using defaults:", err.message);
    SERVO_JOINT_MAP = {};
    for (const [k, v] of Object.entries(DEFAULT_SERVO_JOINT_MAP)) {
      SERVO_JOINT_MAP[k] = [v];
    }
    MOTOR_JOINT_MAP = { ...DEFAULT_MOTOR_JOINT_MAP };
    MOTOR_RAD_PER_SEC = { ...DEFAULT_MOTOR_RAD_PER_SEC };
    URDF_UP_AXIS = "Z";
    return null;
  }
}

// ── Utilities ──────────────────────────────────────────────────────────

function setMode(text) {
  modeEl.textContent = text;
}

function clamp(value, lo, hi) {
  return Math.max(lo, Math.min(hi, value));
}

/**
 * Normalize a raw servo value to [-1, 1] using per-servo calibration.
 * neutral → 0, min → -1, max → +1.
 * Falls back to generic 500-2500 range if no calibration is available.
 */
function toServoNorm(rawValue, servoKey) {
  const v = Number(rawValue);
  if (!Number.isFinite(v)) {
    return 0; // no data → neutral (mesh default pose)
  }
  if (Math.abs(v) <= 1.25) {
    return clamp(v, -1, 1);
  }

  const cal = servoKey ? SERVO_CALIBRATION[servoKey] : null;
  if (cal) {
    // Map: min → -1, neutral → 0, max → +1
    if (v <= cal.neutral) {
      const range = cal.neutral - cal.min;
      return range > 0 ? clamp((v - cal.neutral) / range, -1, 0) : 0;
    } else {
      const range = cal.max - cal.neutral;
      return range > 0 ? clamp((v - cal.neutral) / range, 0, 1) : 0;
    }
  }

  // Fallback: generic PWM range
  if (v >= 500 && v <= 2500) {
    return clamp((v - 1500) / 500, -1, 1);
  }
  if (v >= 0 && v <= 255) {
    return clamp((v - 127.5) / 127.5, -1, 1);
  }
  return clamp(v / 1000, -1, 1);
}

function calibrationPoint(cal, point) {
  if (!cal) return null;
  if (point === "min") return cal.min;
  if (point === "max") return cal.max;
  if (point === "neutral") return cal.neutral;
  return null;
}

function normalizedBetween(value, from, to) {
  const v = Number(value);
  if (!Number.isFinite(v) || !Number.isFinite(from) || !Number.isFinite(to) || from === to) {
    return 0;
  }
  return clamp((v - from) / (to - from), 0, 1);
}

function servoNormTarget(group, id) {
  const key = `${group}:${id}`;
  const override = SERVO_VISUAL_OVERRIDES[key];
  if (!override) {
    return toServoNorm(state.servoValues.get(key), key);
  }

  const cal = SERVO_CALIBRATION[key];
  const closedValue = calibrationPoint(cal, override.closedAt);
  const openValue = calibrationPoint(cal, override.openAt);
  const amountOpen = normalizedBetween(state.servoValues.get(key), closedValue, openValue);
  return override.closedNorm + (override.openNorm - override.closedNorm) * amountOpen;
}

/**
 * Advance smoothed servo values toward their targets by dt seconds.
 */
function updateServoSmoothing(dt) {
  const alpha = 1 - Math.exp(-SERVO_SMOOTH_RATE * dt);
  for (const [servoKey] of Object.entries(SERVO_JOINT_MAP)) {
    const [group, idStr] = servoKey.split(":");
    const target = servoNormTarget(group, Number(idStr));
    const current = state.servoCurrentNorm.get(servoKey) ?? target;
    state.servoCurrentNorm.set(servoKey, current + (target - current) * alpha);
  }
}

function servoNorm(group, id) {
  const key = `${group}:${id}`;
  return state.servoCurrentNorm.get(key) ?? servoNormTarget(group, id);
}

function servoOpenAmount(group, id) {
  const key = `${group}:${id}`;
  const override = SERVO_VISUAL_OVERRIDES[key];
  if (!override) {
    return clamp(Math.abs(servoNorm(group, id)), 0, 1);
  }

  return normalizedBetween(servoNorm(group, id), override.closedNorm, override.openNorm);
}

function servoValueFromNorm(key, norm) {
  const cal = SERVO_CALIBRATION[key];
  if (!cal) {
    return state.servoValues.get(key);
  }

  const override = SERVO_VISUAL_OVERRIDES[key];
  if (override) {
    const amountOpen = normalizedBetween(norm, override.closedNorm, override.openNorm);
    const closedValue = calibrationPoint(cal, override.closedAt);
    const openValue = calibrationPoint(cal, override.openAt);
    return closedValue + ((openValue - closedValue) * amountOpen);
  }

  if (norm <= 0) {
    return cal.neutral + (norm * (cal.neutral - cal.min));
  }
  return cal.neutral + (norm * (cal.max - cal.neutral));
}

function mapToJointRange(norm, joint) {
  if (!joint) {
    return 0;
  }
  const lo = joint.limit?.lower ?? -Math.PI;
  const hi = joint.limit?.upper ?? Math.PI;
  // Map norm=0 → joint value 0 (URDF default pose), not midpoint.
  // norm=-1 → lower limit, norm=+1 → upper limit, piecewise linear.
  let value;
  if (norm <= 0) {
    value = -norm * lo;   // -1→lo, 0→0
  } else {
    value = norm * hi;    // 0→0, +1→hi
  }
  return clamp(value, lo, hi);
}

// ── Telemetry recording ────────────────────────────────────────────────

function recordServoTelemetry(msg) {
  for (const servo of msg.servos || []) {
    const group = servo.group || "other";
    const key = `${group}:${servo.id}`;
    state.servoTelemetry.set(key, { ...servo, group });
    if (!isServoPositionCommand(servo)) {
      continue;
    }
    state.servoValues.set(key, servo.value);
  }
}

function servoCommandName(servo) {
  if (servo?.command) return servo.command;
  const type = Number(servo?.type);
  if (type === SERVO_TYPE.SET_SPEED) return "speed";
  if (type === SERVO_TYPE.DISABLE) return "disable";
  if (type === SERVO_TYPE.ENABLE) return "enable";
  return "position";
}

function isServoPositionCommand(servo) {
  return servoCommandName(servo) === "position";
}

function recordMotorTelemetry(msg) {
  for (const motor of msg.motors || []) {
    state.motorValues.set(motor.id, motor.value ?? 0);
  }
}

function recordLedTelemetry(msg) {
  for (const led of msg.leds || []) {
    state.ledData.set(led.id, {
      on: led.state !== "off" && led.state !== false,
      r: led.color?.r ?? led.color?.red ?? 0,
      g: led.color?.g ?? led.color?.green ?? 0,
      b: led.color?.b ?? led.color?.blue ?? 0,
      brightness: led.brightness ?? 255,
    });
  }
}

// ── Pose application ───────────────────────────────────────────────────

function applyUrdfPose(robot) {
  if (!robot || !robot.joints) {
    return;
  }

  for (const [servoKey, jointNames] of Object.entries(SERVO_JOINT_MAP)) {
    const [group, idStr] = servoKey.split(":");
    const norm = servoNorm(group, Number(idStr));

    for (const jointName of jointNames) {
      const joint = robot.joints[jointName];
      if (!joint || typeof joint.setJointValue !== "function") {
        continue;
      }
      joint.setJointValue(mapToJointRange(norm, joint));
    }
  }

  for (const [motorId, jointName] of Object.entries(MOTOR_JOINT_MAP)) {
    const joint = robot.joints[jointName];
    if (!joint || typeof joint.setJointValue !== "function") {
      continue;
    }
    const angle = state.motorAngles.get(Number(motorId)) || 0;
    joint.setJointValue(angle);
  }
}

function applyFallbackPose(parts) {
  parts.dome.rotation.y = state.motorAngles.get(2) || 0;

  const neckAvg = (servoNorm("body", 0) + servoNorm("body", 1) + servoNorm("body", 2)) / 3;
  parts.neck.rotation.x = neckAvg * 0.6;

  parts.periscope.position.y = 1.25 + clamp(servoNorm("dome", 0), 0, 1) * 0.32;

  parts.domeDoorR.rotation.z = servoOpenAmount("dome", 2) * 1.0;
  parts.domeDoorL.rotation.z = -servoOpenAmount("dome", 6) * 1.0;
  parts.bodyDoorR.rotation.y = -clamp(servoNorm("body", 4), 0, 1) * 1.2;
  parts.bodyDoorL.rotation.y = clamp(servoNorm("body", 5), 0, 1) * 1.2;
}

// ── LED color application ─────────────────────────────────────────────

function applyLedColors(robot, THREE) {
  if (!robot) return;

  for (const [ledId, linkName] of Object.entries(LED_LINK_MAP)) {
    const data = state.ledData.get(ledId) || state.ledData.get(Number(ledId));
    if (!data) continue;

    let linkObj = robot.links?.[linkName] || null;
    if (!linkObj) {
      robot.traverse((child) => {
        if (child.name === linkName && !linkObj) linkObj = child;
      });
    }
    if (!linkObj) continue;

    linkObj.traverse((child) => {
      if (!child.isMesh) return;
      if (!child.material._ledOwned) {
        child.material = child.material.clone();
        child.material._ledOwned = true;
      }
      if (data.on) {
        const intensity = (data.brightness ?? 255) / 255;
        const color = new THREE.Color(data.r / 255, data.g / 255, data.b / 255);
        child.material.emissive = color;
        child.material.emissiveIntensity = 0.8 * intensity;
      } else {
        child.material.emissive = new THREE.Color(0, 0, 0);
        child.material.emissiveIntensity = 0;
      }
    });
  }
}

function applyFallbackLedColors(parts, THREE) {
  const fbLedTargets = { 0: parts.body, 1: parts.dome };
  for (const [ledId, mesh] of Object.entries(fbLedTargets)) {
    const data = state.ledData.get(ledId) || state.ledData.get(Number(ledId));
    if (!data || !mesh) continue;
    if (data.on) {
      const intensity = (data.brightness ?? 255) / 255;
      mesh.material.emissive.setRGB(data.r / 255, data.g / 255, data.b / 255);
      mesh.material.emissiveIntensity = 0.8 * intensity;
    } else {
      mesh.material.emissive.setRGB(0, 0, 0);
      mesh.material.emissiveIntensity = 0;
    }
  }
}

// ── Fallback procedural model ──────────────────────────────────────────

function buildFallbackRobot(THREE, scene) {
  const root = new THREE.Group();
  scene.add(root);

  const body = new THREE.Mesh(
    new THREE.CylinderGeometry(0.75, 0.85, 1.55, 24),
    new THREE.MeshStandardMaterial({ color: 0xdadfe4, metalness: 0.22, roughness: 0.58 })
  );
  body.position.y = 0.78;
  root.add(body);

  const neck = new THREE.Group();
  neck.position.y = 1.56;
  root.add(neck);

  const dome = new THREE.Mesh(
    new THREE.SphereGeometry(0.62, 24, 16, 0, Math.PI * 2, 0, Math.PI / 2),
    new THREE.MeshStandardMaterial({ color: 0xe8ecef, metalness: 0.18, roughness: 0.56 })
  );
  dome.rotation.x = Math.PI;
  neck.add(dome);

  const periscope = new THREE.Mesh(
    new THREE.CylinderGeometry(0.08, 0.1, 0.56, 16),
    new THREE.MeshStandardMaterial({ color: 0x33424f, metalness: 0.45, roughness: 0.42 })
  );
  periscope.position.set(0.18, 1.25, 0.04);
  root.add(periscope);

  const domeDoorR = new THREE.Mesh(
    new THREE.BoxGeometry(0.24, 0.04, 0.36),
    new THREE.MeshStandardMaterial({ color: 0xbec8d0 })
  );
  domeDoorR.position.set(0.45, 1.56, 0.26);
  neck.add(domeDoorR);

  const domeDoorL = new THREE.Mesh(
    new THREE.BoxGeometry(0.24, 0.04, 0.36),
    new THREE.MeshStandardMaterial({ color: 0xbec8d0 })
  );
  domeDoorL.position.set(-0.45, 1.56, 0.26);
  neck.add(domeDoorL);

  const bodyDoorR = new THREE.Mesh(
    new THREE.BoxGeometry(0.02, 0.6, 0.42),
    new THREE.MeshStandardMaterial({ color: 0xa5b1ba })
  );
  bodyDoorR.position.set(0.74, 0.86, 0.1);
  root.add(bodyDoorR);

  const bodyDoorL = new THREE.Mesh(
    new THREE.BoxGeometry(0.02, 0.6, 0.42),
    new THREE.MeshStandardMaterial({ color: 0xa5b1ba })
  );
  bodyDoorL.position.set(-0.74, 0.86, 0.1);
  root.add(bodyDoorL);

  return { root, body, neck, dome, periscope, domeDoorR, domeDoorL, bodyDoorR, bodyDoorL };
}

// ── Build pin labels for URDF model ────────────────────────────────────

function buildUrdfLabels(THREE, robot) {
  if (!robot || !labelLayer) return;

  // Motors
  for (const [motorIdStr, jointName] of Object.entries(MOTOR_JOINT_MAP)) {
    const motorId = Number(motorIdStr);
    const joint = robot.joints?.[jointName];
    if (!joint) continue;
    const name = MOTOR_NAMES[motorId] || `M${motorId}`;
    const el = createPinLabel(`motor:${motorId}`, name);
    pinLabels.push({ anchor: joint, el, key: `motor:${motorId}` });
  }

  // Servos
  for (const [servoKey, jointNames] of Object.entries(SERVO_JOINT_MAP)) {
    if (!jointNames || jointNames.length === 0) continue;
    const joint = robot.joints?.[jointNames[0]];
    if (!joint) continue;
    const name = SERVO_NAMES[servoKey] || servoKey;
    const el = createPinLabel(`servo:${servoKey}`, name);
    pinLabels.push({ anchor: joint, el, key: `servo:${servoKey}` });
  }

  // LEDs
  for (const [ledId, linkName] of Object.entries(LED_LINK_MAP)) {
    let linkObj = null;
    if (robot.links && robot.links[linkName]) {
      linkObj = robot.links[linkName];
    } else {
      robot.traverse((child) => {
        if (child.name === linkName && !linkObj) linkObj = child;
      });
    }
    if (!linkObj) continue;
    const name = LED_NAMES[ledId] || `LED ${ledId}`;
    const el = createPinLabel(`led:${ledId}`, name);
    // Add swatch span
    const swatch = document.createElement("span");
    swatch.className = "pin-label-swatch";
    swatch.dataset.ledId = ledId;
    el.appendChild(swatch);
    pinLabels.push({ anchor: linkObj, el, key: `led:${ledId}` });
  }
}

// ── Build pin labels for fallback model ────────────────────────────────

function buildFallbackLabels(THREE, parts) {
  if (!labelLayer) return;

  // Fallback anchor helpers: create invisible objects at meaningful positions
  const anchorMap = {
    "motor:0": (() => { const o = new THREE.Object3D(); o.position.set(-0.6, 0.15, 0); parts.root.add(o); return o; })(),
    "motor:1": (() => { const o = new THREE.Object3D(); o.position.set(0.6, 0.15, 0); parts.root.add(o); return o; })(),
    "motor:2": parts.dome,
    "servo:body:0": parts.neck,
    "servo:body:3": (() => { const o = new THREE.Object3D(); o.position.set(0.5, 0.6, 0.4); parts.root.add(o); return o; })(),
    "servo:body:4": parts.bodyDoorR,
    "servo:body:5": parts.bodyDoorL,
    "servo:dome:0": parts.periscope,
    "servo:dome:2": parts.domeDoorR,
    "servo:dome:6": parts.domeDoorL,
  };

  for (const [key, anchor] of Object.entries(anchorMap)) {
    const [type, ...rest] = key.split(":");
    let name;
    if (type === "motor") {
      name = MOTOR_NAMES[Number(rest[0])] || `M${rest[0]}`;
    } else {
      const servoKey = rest.join(":");
      name = SERVO_NAMES[servoKey] || servoKey;
    }
    const el = createPinLabel(key, name);
    pinLabels.push({ anchor, el, key });
  }

  // LEDs on fallback
  const ledAnchors = {
    0: parts.body,
    1: parts.dome,
  };
  for (const [ledId, anchor] of Object.entries(ledAnchors)) {
    const name = LED_NAMES[ledId] || `LED ${ledId}`;
    const el = createPinLabel(`led:${ledId}`, name);
    const swatch = document.createElement("span");
    swatch.className = "pin-label-swatch";
    swatch.dataset.ledId = ledId;
    el.appendChild(swatch);
    pinLabels.push({ anchor, el, key: `led:${ledId}` });
  }
}

// ── Project labels to screen each frame ────────────────────────────────

// Map pin-label key prefix to debug section name
function labelCategory(key) {
  if (key.startsWith("motor:")) return "motors";
  if (key.startsWith("servo:")) return "servos";
  if (key.startsWith("led:")) return "leds";
  return null;
}

function updateLabelPositions(camera, viewportW, viewportH, tempVec) {
  const sections = window.debugSections || {};
  for (const label of pinLabels) {
    // Hide if this label's debug category is toggled off
    const cat = labelCategory(label.key);
    if (cat && !sections[cat]) {
      label.el.style.display = "none";
      continue;
    }

    label.anchor.getWorldPosition(tempVec);
    tempVec.project(camera);

    // Behind camera or outside frustum
    if (tempVec.z > 1 || tempVec.x < -1.1 || tempVec.x > 1.1 || tempVec.y < -1.1 || tempVec.y > 1.1) {
      label.el.style.display = "none";
      continue;
    }

    const x = (tempVec.x * 0.5 + 0.5) * viewportW;
    const y = (-tempVec.y * 0.5 + 0.5) * viewportH;

    label.el.style.display = "";
    label.el.style.left = `${x}px`;
    label.el.style.top = `${y}px`;
  }
}

// ── Update label content from telemetry ────────────────────────────────

function updateLabelContent(msg) {
  // Motors
  for (const motor of msg.motors || []) {
    const el = labelLayer.querySelector(`[data-key="motor:${motor.id}"]`);
    if (!el) continue;
    const val = motor.value === null || motor.value === undefined ? "--" : Number(motor.value).toFixed(3);
    el.textContent = val;
  }

  // LEDs
  for (const led of msg.leds || []) {
    const swatch = labelLayer.querySelector(`.pin-label-swatch[data-led-id="${led.id}"]`);
    if (!swatch) continue;
    const color = led.color;
    const r = Number(color?.r ?? color?.red ?? 0);
    const g = Number(color?.g ?? color?.green ?? 0);
    const b = Number(color?.b ?? color?.blue ?? 0);
    swatch.style.background = `rgb(${clamp(r,0,255)},${clamp(g,0,255)},${clamp(b,0,255)})`;

    const valEl = swatch.parentElement?.querySelector(".pin-label-value");
    if (valEl) {
      const onOff = (led.state === "off" || led.state === false) ? "off" : "on";
      valEl.textContent = onOff;
    }
  }
}

function updateServoLabelContent() {
  for (const [servoKey] of Object.entries(SERVO_JOINT_MAP)) {
    if (!state.servoValues.has(servoKey)) {
      continue;
    }
    const [group, idStr] = servoKey.split(":");
    const id = Number(idStr);
    const el = labelLayer.querySelector(`[data-key="servo:${group}:${id}"]`);
    if (!el) continue;
    const value = servoValueFromNorm(servoKey, servoNorm(group, id));
    el.textContent = Number.isFinite(value) ? Number(value).toFixed(3) : "--";
  }
}

function updateServoDebugPanel() {
  const sections = window.debugSections || {};
  if (!sections.servos) {
    return;
  }

  const el = document.getElementById("debugServos");
  if (!el) {
    return;
  }

  const lines = [];
  for (const [servoKey, servo] of state.servoTelemetry) {
    const command = servoCommandName(servo);
    if (command !== "position") {
      const value = Number(servo.value);
      lines.push(`${servoKey} ${command}=${Number.isFinite(value) ? value.toFixed(0) : "--"}`);
      continue;
    }

    const [group, idStr] = servoKey.split(":");
    const id = Number(idStr);
    const value = servoValueFromNorm(servoKey, servoNorm(group, id));
    lines.push(`${servoKey} position=${Number.isFinite(value) ? Number(value).toFixed(0) : "--"}`);
  }
  el.textContent = lines.length ? lines.join("  ") : "--";
}

// ── Bootstrap ──────────────────────────────────────────────────────────

async function bootstrap() {
  setMode("loading config & 3D modules");

  const [mappingCfg, THREE, orbitMod] = await Promise.all([
    loadMappingConfig(),
    import("three"),
    import("three/examples/jsm/controls/OrbitControls.js"),
  ]);
  const { OrbitControls } = orbitMod;

  let URDFLoader = null;
  try {
    const mod = await import(
      "https://esm.sh/urdf-loader@0.12.3?external=three"
    );
    URDFLoader = mod.default || mod.URDFLoader;
  } catch (_) {
    // Optional dependency; fallback model used instead.
  }

  // ── Scene setup ────────────────────────────────────────────────────

  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0xe6edf4);

  const camera = new THREE.PerspectiveCamera(40, 1, 0.005, 100);
  camera.position.set(3.2, 2.0, 3.2);
  camera.lookAt(0, 0.3, 0);

  const renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  viewportEl.appendChild(renderer.domElement);

  const controls = new OrbitControls(camera, renderer.domElement);
  controls.target.set(0, 0.3, 0);
  controls.enableDamping = true;
  controls.dampingFactor = 0.08;
  controls.enablePan = true;
  controls.enableZoom = true;
  controls.enableRotate = true;
  controls.screenSpacePanning = true;
  controls.minDistance = 0.1;
  controls.maxDistance = 20;
  controls.mouseButtons = {
    LEFT: THREE.MOUSE.ROTATE,
    MIDDLE: THREE.MOUSE.DOLLY,
    RIGHT: THREE.MOUSE.PAN,
  };
  controls.touches = {
    ONE: THREE.TOUCH.ROTATE,
    TWO: THREE.TOUCH.DOLLY_PAN,
  };

  scene.add(new THREE.HemisphereLight(0xf5faff, 0x5f6772, 1.0));
  const key = new THREE.DirectionalLight(0xffffff, 0.95);
  key.position.set(2.4, 3.8, 2.2);
  scene.add(key);

  const GRID_SIZE = 8;
  const GRID_DIVS = 32;
  const GRID_CELL = GRID_SIZE / GRID_DIVS; // 0.25 — exact in float64
  const GROUND_SPEED = 4.0;
  const gridPivot = new THREE.Group();
  scene.add(gridPivot);
  const grid = new THREE.GridHelper(GRID_SIZE, GRID_DIVS, 0xc8d2dc, 0xc8d2dc);
  grid.position.y = 0.0;
  gridPivot.add(grid);

  // ── Models ─────────────────────────────────────────────────────────

  const fallback = buildFallbackRobot(THREE, scene);
  state.fallbackParts = fallback;
  setMode("fallback model (loading URDF...)");

  const params = new URLSearchParams(window.location.search);

  let urdfUrl;
  let pkgMapping;

  if (mappingCfg && mappingCfg.urdf_file) {
    const urdfDir = "/description/fusion2urdf";
    urdfUrl = params.get("urdf") || `${urdfDir}/${mappingCfg.urdf_file}`;
    pkgMapping = {};
    pkgMapping[mappingCfg.urdf_package || "fusion2urdf_description"] = urdfDir;
  } else {
    urdfUrl = window.CHOPPER_URDF_URL || params.get("urdf") || "/description/chopper.urdf";
    pkgMapping = params.get("pkg") || "";
  }

  if (URDFLoader) {
    try {
      const loader = new URDFLoader();
      loader.packages = pkgMapping;

      await new Promise((resolve, reject) => {
        loader.load(
          urdfUrl,
          (robot) => {
            const wrapper = new THREE.Group();

            if (URDF_UP_AXIS === "Z") {
              wrapper.rotation.x = -Math.PI / 2;
            }

            wrapper.add(robot);
            scene.add(wrapper);

            wrapper.traverse((child) => {
              if (child.isMesh) {
                child.material = new THREE.MeshStandardMaterial({
                  color: 0xb0b8c0,
                  metalness: 0.3,
                  roughness: 0.6,
                });
              }
            });

            fallback.root.visible = false;
            state.urdfRobot = robot;
            setMode(mappingCfg ? "Fusion URDF (mesh)" : "URDF model");

            if (robot.joints) {
              console.log("URDF joints:", Object.keys(robot.joints).join(", "));
            }

            function allMeshesReady() {
              let total = 0;
              let ready = 0;
              wrapper.traverse((child) => {
                if (child.isMesh) {
                  total++;
                  const pos = child.geometry?.attributes?.position;
                  if (pos && pos.count > 0) {
                    ready++;
                  }
                }
              });
              return total > 0 && ready === total;
            }

            function groundAndFit() {
              if (!allMeshesReady()) {
                setTimeout(groundAndFit, 50);
                return;
              }

              wrapper.updateMatrixWorld(true);
              wrapper.traverse((child) => {
                if (child.isMesh && child.geometry) {
                  child.geometry.computeBoundingBox();
                }
              });

              const box = new THREE.Box3().setFromObject(wrapper);
              wrapper.position.y -= box.min.y;
              wrapper.updateMatrixWorld(true);

              const grounded = new THREE.Box3().setFromObject(wrapper);
              const center = grounded.getCenter(new THREE.Vector3());
              const gSize = grounded.getSize(new THREE.Vector3());
              const gMax = Math.max(gSize.x, gSize.y, gSize.z);

              const fovRad = camera.fov * Math.PI / 180;
              const dist = (gMax / 2) / Math.tan(fovRad / 2) * 2.2;

              camera.position.set(
                center.x + dist * 0.65,
                center.y + dist * 0.45,
                center.z + dist * 0.65
              );
              camera.lookAt(center);
              controls.target.copy(center);
              controls.update();

              console.log("URDF grounded bounds:", {
                meshes: (() => { let n = 0; wrapper.traverse(c => { if (c.isMesh) n++; }); return n; })(),
                min: grounded.min.toArray().map(v => v.toFixed(3)),
                max: grounded.max.toArray().map(v => v.toFixed(3)),
                size: gSize.toArray().map(v => v.toFixed(3)),
              });
              saveDefaultView();

              // Position grid pivot at midpoint between drive wheels
              const leftJoint = robot.joints[MOTOR_JOINT_MAP[0]];
              const rightJoint = robot.joints[MOTOR_JOINT_MAP[1]];
              if (leftJoint && rightJoint) {
                const lw = new THREE.Vector3();
                const rw = new THREE.Vector3();
                leftJoint.getWorldPosition(lw);
                rightJoint.getWorldPosition(rw);
                const mid = lw.add(rw).multiplyScalar(0.5);
                gridPivot.position.set(mid.x, 0, mid.z);
                state.pivotOffset = { x: mid.x, z: mid.z };
                console.log("Grid pivot at wheel midpoint:", mid.x.toFixed(3), mid.z.toFixed(3));
              }

              // Build labels after model is grounded
              buildUrdfLabels(THREE, robot);
            }
            groundAndFit();

            resolve();
          },
          undefined,
          reject
        );
      });
    } catch (err) {
      setMode("fallback model (URDF load failed)");
      console.warn("URDF load failed:", err);
    }
  } else {
    setMode("fallback model (URDF loader unavailable)");
  }

  // Build fallback labels if URDF didn't load
  if (!state.urdfRobot && fallback.root.visible) {
    buildFallbackLabels(THREE, fallback);
  }

  // ── Default camera state ───────────────────────────────────────────

  const defaultCamera = {
    position: camera.position.clone(),
    target: controls.target.clone(),
  };

  function saveDefaultView() {
    defaultCamera.position.copy(camera.position);
    defaultCamera.target.copy(controls.target);
  }

  function resetView() {
    camera.position.copy(defaultCamera.position);
    controls.target.copy(defaultCamera.target);
    camera.lookAt(defaultCamera.target);
    controls.update();
  }

  const resetBtn = document.getElementById("resetViewBtn");
  if (resetBtn) {
    resetBtn.addEventListener("click", resetView);
  }

  // ── Resize ─────────────────────────────────────────────────────────

  function resize() {
    const rect = viewportEl.getBoundingClientRect();
    const w = Math.floor(rect.width) || 640;
    const h = Math.floor(rect.height) || 380;
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    renderer.setSize(w, h, false);
  }
  resize();
  window.addEventListener("resize", resize);
  new ResizeObserver(resize).observe(viewportEl);

  // ── Telemetry listener ─────────────────────────────────────────────

  window.addEventListener("telemetry:update", (event) => {
    const msg = event.detail || {};
    recordServoTelemetry(msg);
    recordMotorTelemetry(msg);
    recordLedTelemetry(msg);
    updateLabelContent(msg);
  });

  // ── Render loop ────────────────────────────────────────────────────

  const tempVec = new THREE.Vector3();
  state.lastFrameTime = performance.now();

  const cameraBarEl = document.getElementById("cameraBar");
  let lastCameraBarUpdate = 0;

  function frame(now) {
    const dt = Math.min((now - state.lastFrameTime) / 1000, 0.1);
    state.lastFrameTime = now;

    for (const [motorId, speed] of state.motorValues) {
      const radPerSec = MOTOR_RAD_PER_SEC[motorId] || 6.0;
      const prev = state.motorAngles.get(motorId) || 0;
      state.motorAngles.set(motorId, prev + speed * radPerSec * dt);
    }

    updateServoSmoothing(dt);
    updateServoLabelContent();
    updateServoDebugPanel();

    const leftSpeed = state.motorValues.get(0) || 0;
    const rightSpeed = state.motorValues.get(1) || 0;
    // Telemetry carries logical drive commands before HAL motor inversion.
    // Positive logical drive means robot-forward, so the ground scrolls backward.
    const forward = -((leftSpeed + rightSpeed) * 0.5);
    const turnRate = (rightSpeed - leftSpeed) * 2.0;

    // groundX/groundZ track displacement in grid-local space so
    // wrapping always aligns with the grid lines.
    const dh = turnRate * dt;
    state.groundHeading += dh;

    // Re-project accumulated offset when heading changes so pure
    // rotation doesn't cause a visible scroll.
    if (dh !== 0) {
      const cd = Math.cos(dh);
      const sd = Math.sin(dh);
      const px = state.groundX;
      const pz = state.groundZ;
      state.groundX =  px * cd + pz * sd;
      state.groundZ = -px * sd + pz * cd;
    }

    // Forward (mesh +Z) in grid-local frame is (sin(heading), cos(heading))
    const fwd = forward * GROUND_SPEED * dt;
    state.groundX += fwd * Math.sin(state.groundHeading);
    state.groundZ += fwd * Math.cos(state.groundHeading);

    // Wrap to one cell — grid tiles seamlessly so the jump is invisible
    state.groundX = ((state.groundX % GRID_CELL) + GRID_CELL) % GRID_CELL;
    state.groundZ = ((state.groundZ % GRID_CELL) + GRID_CELL) % GRID_CELL;

    // Pivot rotates around world origin (mesh center)
    gridPivot.rotation.y = -state.groundHeading;
    grid.position.x = -state.groundX;
    grid.position.z = -state.groundZ;

    if (state.urdfRobot) {
      applyUrdfPose(state.urdfRobot);
      applyLedColors(state.urdfRobot, THREE);
    }
    if (state.fallbackParts && state.fallbackParts.root.visible) {
      applyFallbackPose(state.fallbackParts);
      applyFallbackLedColors(state.fallbackParts, THREE);
    }

    controls.update();
    renderer.render(scene, camera);

    // Update camera bar (~4 Hz)
    if (cameraBarEl && now - lastCameraBarUpdate > 250) {
      lastCameraBarUpdate = now;
      const p = camera.position;
      const t = controls.target;
      const dist = p.distanceTo(t);
      cameraBarEl.textContent =
        `x:${p.x.toFixed(1)} y:${p.y.toFixed(1)} z:${p.z.toFixed(1)}  d:${dist.toFixed(1)}`;
    }

    // Project pin labels to screen
    if (pinLabels.length > 0) {
      const rect = viewportEl.getBoundingClientRect();
      updateLabelPositions(camera, rect.width, rect.height, tempVec);
    }

    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
}

bootstrap().catch((err) => {
  setMode("3D unavailable");
  console.error("3D bootstrap failed:", err);
});
