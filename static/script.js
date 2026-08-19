/* =====================================================================
   SMART FARM — DASHBOARD LOGIC

   Real-time strategy: polling, not WebSockets.
   PythonAnywhere's standard web apps are WSGI-only, so a persistent
   WebSocket server isn't a reliable option on free/standard hosting.
   Polling /api/latest every POLL_INTERVAL_MS is indistinguishable from
   "real-time" to a human eye and needs zero special deployment steps.
   ===================================================================== */

const POLL_INTERVAL_MS = 2000;   // matches the ESP8266's own post cadence
const LOGS_INTERVAL_MS = 8000;   // history table refreshes less often
const STALE_AFTER_MS = 6000;     // no fresh data in 6s -> "stale" dot
const ERROR_AFTER_MS = 15000;    // no fresh data in 15s -> "error" dot

const RING_RADIUS = 86;
const RING_CIRCUMFERENCE = 2 * Math.PI * RING_RADIUS;

// ---- element refs ----
const el = {
  liveDot: document.getElementById("live-dot"),
  liveText: document.getElementById("live-text"),

  soilRing: document.getElementById("soil-ring"),
  soilPercent: document.getElementById("soil-percent"),
  soilStatus: document.getElementById("soil-status"),

  tempValue: document.getElementById("temp-value"),
  humidityValue: document.getElementById("humidity-value"),

  pumpDot: document.getElementById("pump-dot"),
  pumpStatusText: document.getElementById("pump-status-text"),
  pumpModeLabel: document.getElementById("pump-mode-label"),
  pumpOnBtn: document.getElementById("pump-on-btn"),
  pumpOffBtn: document.getElementById("pump-off-btn"),

  modeAutoBtn: document.getElementById("mode-auto-btn"),
  modeManualBtn: document.getElementById("mode-manual-btn"),
  modeHint: document.getElementById("mode-hint"),

  thresholdInput: document.getElementById("threshold-input"),
  thresholdDisplay: document.getElementById("threshold-display"),
  hysteresisInput: document.getElementById("hysteresis-input"),
  hysteresisDisplay: document.getElementById("hysteresis-display"),
  saveThresholdBtn: document.getElementById("save-threshold-btn"),
  saveConfirm: document.getElementById("save-confirm"),

  controlHint: document.getElementById("control-hint"),

  logsContainer: document.getElementById("logs-container"),
};

el.soilRing.style.strokeDasharray = `${RING_CIRCUMFERENCE}`;

// ---- local state ----
let currentMode = "AUTO";
let lastUpdateAt = null;      // Date object, from ESP8266's updated_at
let lastFetchOk = null;       // timestamp of last successful poll (local clock)
let userIsEditingSettings = false; // pause slider overwrite while user drags

let lastKnownPump = false;      // true/false, mirrors the server's real state
let pendingPumpTarget = null;   // the state we're waiting to see confirmed, or null
let pendingSince = null;        // Date.now() when the click was sent
const PENDING_TIMEOUT_MS = 10000; // auto-unlock safety net

// =========================================================
// Formatting helpers
// =========================================================
function setRing(percent) {
  const clamped = Math.max(0, Math.min(100, percent));
  const offset = RING_CIRCUMFERENCE * (1 - clamped / 100);
  el.soilRing.style.strokeDashoffset = offset;

  el.soilPercent.textContent = `${Math.round(clamped)}%`;

  if (clamped < 25) {
    el.soilRing.style.stroke = "var(--warn)";
    el.soilStatus.textContent = "Very dry";
  } else if (clamped < 45) {
    el.soilRing.style.stroke = "var(--earth)";
    el.soilStatus.textContent = "Dry";
  } else {
    el.soilRing.style.stroke = "var(--leaf)";
    el.soilStatus.textContent = "Moist";
  }
}

// ---- Pump status + controls: state always mirrors the server, never guessed ----
function setPumpUI(pumpOn) {
  lastKnownPump = pumpOn;
  el.pumpDot.classList.toggle("is-on", pumpOn);
  el.pumpDot.classList.toggle("is-off", !pumpOn);
  el.pumpStatusText.textContent = pumpOn ? "ON" : "OFF";

  // The change we were waiting on just arrived -- unlock the buttons.
  if (pendingPumpTarget !== null && pumpOn === pendingPumpTarget) {
    clearPending();
  }
}

function clearPending(message = "") {
  pendingPumpTarget = null;
  pendingSince = null;
  updatePumpLock();
  if (message) {
    el.controlHint.textContent = message;
    setTimeout(() => {
      el.controlHint.textContent = currentMode === "MANUAL"
        ? "Tap a button to switch the pump on or off."
        : "Switch to Manual mode to control the pump directly.";
    }, 4000);
  }
}

function updatePumpLock() {
  const manual = currentMode === "MANUAL";
  const locked = !manual || pendingPumpTarget !== null;
  el.pumpOnBtn.disabled = locked;
  el.pumpOffBtn.disabled = locked;
}

function checkPendingTimeout() {
  if (pendingPumpTarget !== null && pendingSince && (Date.now() - pendingSince > PENDING_TIMEOUT_MS)) {
    clearPending("Didn't update — check the device and try again.");
  }
}

function setModeUI(mode) {
  currentMode = mode;
  el.modeAutoBtn.classList.toggle("is-active", mode === "AUTO");
  el.modeManualBtn.classList.toggle("is-active", mode === "MANUAL");
  el.pumpModeLabel.textContent = mode;

  updatePumpLock();

  const manual = mode === "MANUAL";
  el.controlHint.textContent = manual
    ? "Tap a button to switch the pump on or off."
    : "Switch to Manual mode to control the pump directly.";

  el.modeHint.textContent = manual
    ? "Manual mode is active. The moisture threshold below is ignored until you switch back to Auto."
    : "The pump switches on and off by itself, based on your moisture threshold.";
}

function setLiveIndicator(state, label) {
  el.liveDot.classList.remove("is-live", "is-stale", "is-error");
  el.liveDot.classList.add(state);
  el.liveText.textContent = label;
}

function timeAgoLabel(date) {
  if (!date) return "no data yet";
  const seconds = Math.round((Date.now() - date.getTime()) / 1000);
  if (seconds < 3) return "updated just now";
  if (seconds < 60) return `updated ${seconds}s ago`;
  const minutes = Math.round(seconds / 60);
  return `updated ${minutes}m ago`;
}

function formatLogTime(isoString) {
  try {
    const d = new Date(isoString);
    return d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  } catch (e) {
    return isoString;
  }
}

// =========================================================
// Polling: latest reading (drives everything "live")
// =========================================================
async function pollLatest() {
  try {
    const res = await fetch("/api/latest", { cache: "no-store" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();

    setRing(data.soil_moisture);
    el.tempValue.textContent = `${data.temperature.toFixed(1)}°C`;
    el.humidityValue.textContent = `${Math.round(data.humidity)}%`;
    setPumpUI(data.pump);
    setModeUI(data.mode);

    // Don't yank the slider out from under the user mid-drag.
    if (!userIsEditingSettings) {
      el.thresholdInput.value = data.threshold;
      el.thresholdDisplay.textContent = Math.round(data.threshold);
      el.hysteresisInput.value = data.hysteresis;
      el.hysteresisDisplay.textContent = Math.round(data.hysteresis);
    }

    lastUpdateAt = data.updated_at ? new Date(data.updated_at) : new Date();
    lastFetchOk = Date.now();
  } catch (err) {
    // Server unreachable -- don't guess sensor values, just flag it.
    setLiveIndicator("is-error", "server unreachable");
    return;
  }

  refreshLiveIndicator();
}

function refreshLiveIndicator() {
  if (!lastFetchOk) {
    setLiveIndicator("is-error", "no connection yet");
    return;
  }
  const age = Date.now() - lastFetchOk;
  if (age <= STALE_AFTER_MS) {
    setLiveIndicator("is-live", timeAgoLabel(lastUpdateAt));
  } else if (age <= ERROR_AFTER_MS) {
    setLiveIndicator("is-stale", "waiting for device…");
  } else {
    setLiveIndicator("is-error", "device offline");
  }
}

// =========================================================
// Polling: sensor history
// =========================================================
async function pollLogs() {
  try {
    const res = await fetch("/api/logs?limit=8", { cache: "no-store" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const rows = await res.json();

    if (!rows.length) {
      el.logsContainer.innerHTML = `<p class="hint">No readings logged yet.</p>`;
      return;
    }

    el.logsContainer.innerHTML = rows.map(row => `
      <div class="log-card">
        <span class="log-card__time">${formatLogTime(row.timestamp)}</span>
        <span class="log-card__metric"><span>Soil</span><span>${Math.round(row.soil_moisture)}%</span></span>
        <span class="log-card__metric"><span>Temp</span><span>${row.temperature.toFixed(1)}°C</span></span>
        <span class="log-card__metric"><span>Humidity</span><span>${Math.round(row.humidity)}%</span></span>
      </div>
    `).join("");
  } catch (err) {
    el.logsContainer.innerHTML = `<p class="hint">Couldn't load history — retrying…</p>`;
  }
}

// =========================================================
// User actions
// =========================================================
async function setMode(mode) {
  setModeUI(mode); // optimistic UI update
  try {
    await fetch("/api/mode", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ mode }),
    });
  } catch (err) {
    // Next poll will resync the true state if this failed.
  }
}

async function setPumpManual(target) {
  if (currentMode !== "MANUAL" || pendingPumpTarget !== null) return;
  if (target === lastKnownPump) return; // already in that state, nothing to do

  pendingPumpTarget = target;
  pendingSince = Date.now();
  el.pumpOnBtn.disabled = true;
  el.pumpOffBtn.disabled = true;
  el.controlHint.textContent = target ? "Turning pump on…" : "Turning pump off…";

  try {
    const res = await fetch("/api/pump", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ pump: target }),
    });
    if (!res.ok) throw new Error("request failed");
    // Success: stays locked until /api/latest confirms the real state.
  } catch (err) {
    clearPending("Couldn't reach the server — try again.");
  }
}

async function saveThreshold() {
  const threshold = Number(el.thresholdInput.value);
  const hysteresis = Number(el.hysteresisInput.value);

  el.saveThresholdBtn.disabled = true;
  el.saveConfirm.textContent = "Saving…";
  try {
    const res = await fetch("/api/threshold", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ threshold, hysteresis }),
    });
    if (!res.ok) throw new Error("save failed");
    el.saveConfirm.textContent = "Saved — the device will pick this up on its next check-in.";
  } catch (err) {
    el.saveConfirm.textContent = "Couldn't save. Check your connection and try again.";
  } finally {
    el.saveThresholdBtn.disabled = false;
    userIsEditingSettings = false;
    setTimeout(() => { el.saveConfirm.textContent = ""; }, 4000);
  }
}

// =========================================================
// Wire up events
// =========================================================
el.modeAutoBtn.addEventListener("click", () => setMode("AUTO"));
el.modeManualBtn.addEventListener("click", () => setMode("MANUAL"));

el.pumpOnBtn.addEventListener("click", () => setPumpManual(true));
el.pumpOffBtn.addEventListener("click", () => setPumpManual(false));

el.thresholdInput.addEventListener("input", () => {
  userIsEditingSettings = true;
  el.thresholdDisplay.textContent = el.thresholdInput.value;
});
el.hysteresisInput.addEventListener("input", () => {
  userIsEditingSettings = true;
  el.hysteresisDisplay.textContent = el.hysteresisInput.value;
});
el.saveThresholdBtn.addEventListener("click", saveThreshold);

// =========================================================
// Start polling
// =========================================================
pollLatest();
pollLogs();
setInterval(pollLatest, POLL_INTERVAL_MS);
setInterval(pollLogs, LOGS_INTERVAL_MS);
setInterval(refreshLiveIndicator, 1000); // keeps "updated Ns ago" ticking between polls
setInterval(checkPendingTimeout, 1000);  // auto-unlocks the pump buttons if it never confirms