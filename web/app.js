"use strict";
const $ = (id) => document.getElementById(id);
const LANES = ["entry", "exit"];
const MIRRORED = {entry: false, exit: true};
let state = null, stream = null, pixels = null;
let uart = "", cursor = {}, sourceRuns = {};
let uartTruncated = false;
let editing = null;
let connectionShown = {kind: "", message: "Connecting…"};
const drawn = {};
const text = (id, value) => { $(id).textContent = value; };
// Manual equipment actions target the selected lane.
const currentLane = () => $("lane").value || "entry";

// Human wording (English keys, see i18n.js) for each profile choice, reused by
// the queue summaries.
const CHOICES = {
  entry_capture: {normal: "", retry_recovers: "Low confidence, then read",
    failed: "Plate never readable", no_reply: "Entry camera silent"},
  exit_capture: {normal: "", misread: "Different plate read at exit"},
  payment: {paid: "", delayed: "Pays slowly", cancelled: "Payment cancelled",
    malformed: "Broken terminal reply", no_reply: "Terminal silent"},
  quirk: {none: "", hesitate: "Hesitates", leave_during_payment: "Drives away while paying",
    linger_in_beam: "Stops under the beam", block_closing: "Blocks the closing barrier"},
};
const OUTCOME = {
  entered: "opened for entry", exited: "opened for exit", refused: "refused",
  ignored: "gave no assistance", abandoned: "let the car drive away",
  assisted: "requested assistance", opened_after_abandon: "opened after the car left",
};
const WHERE = {
  queued: "in the street", entering: "at the entry", parked: "parked",
  exit_queued: "waiting for the exit", exiting: "at the exit", stuck: "exit refused",
  gone: "left the lot", refused: "turned away",
};

function el(tag, className, content) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (content !== undefined) node.textContent = content;
  return node;
}

const sentence = value => value ? value[0].toUpperCase() + value.slice(1) + "." : "";

function plate(value) {
  const node = el("span", "plate");
  node.append(el("span", null, "BRASIL"), el("b", null, value));
  return node;
}

// Rebuild a list only when what it shows has changed.
function draw(id, key, build) {
  if (drawn[id] === key) return;
  drawn[id] = key;
  $(id).replaceChildren(...build());
}

function setConnection(kind, message) {
  connectionShown = {kind, message};
  text("connection", t(message));
  $("connection").classList.toggle("live", kind === "live");
  $("connection").classList.toggle("lost", kind === "lost");
}

async function request(path, body) {
  const response = await fetch(path, {
    method: body ? "POST" : "GET", credentials: "same-origin",
    headers: body ? {"Content-Type": "application/json"} : {},
    body: body ? JSON.stringify(body) : undefined
  });
  const bodyText = await response.text();
  let result = {};
  try { result = bodyText ? JSON.parse(bodyText) : {}; } catch (_) {
    if (response.ok) throw new Error(t("Invalid response from local bridge"));
  }
  if (!response.ok) throw new Error(result.error || t("Request failed: HTTP {status}", {status: response.status}));
  return result;
}

function renderLane(lane, source, next) {
  const p = source.physical, q = source.protocol;
  // The exit scene is mirrored: its traffic leaves the lot right to left.
  const mirror = MIRRORED[lane], along = 60 + (6000 - p.vehicle_mm) * 0.1;
  $(`${lane}-car`).setAttribute("transform", `translate(${mirror ? 900 - along : along},75)`);
  $(`${lane}-car`).classList.toggle("empty", p.vehicle_mm >= 6000);
  // Plan view: closed spans the lane; opening swings the arm along the kerb,
  // toward the approaching car.
  const angle = p.position_mdeg / 1000;
  $(`${lane}-barrier`).setAttribute("transform", `rotate(${mirror ? angle : -angle})`);
  $(`${lane}-red`).classList.toggle("on", q.red);
  $(`${lane}-green`).classList.toggle("on", q.green);
  $(`${lane}-beam`).classList.toggle("active", p.obstruction);
  $(`${lane}-buzzer`).classList.toggle("on", q.buzzer);
  text(`${lane}-angle`, `${(p.position_mdeg / 1000).toFixed(1)}°`);
  const motor = p.enable ? (p.direction ? "opening" : "closing") : "off";
  text(`${lane}-motor`, t(motor));
  $(`${lane}-motor`).classList.toggle("hot", p.enable);
  text(`${lane}-motor-arrow`, p.enable ? t(motor) : "");
  text(`${lane}-vehicle`, `${p.vehicle_mm} mm`);
  text(`${lane}-adc`, String(p.adc_code));
  text(`${lane}-beam-state`, t(p.obstruction ? "blocked" : "clear"));
  $(`${lane}-beam-state`).classList.toggle("hot", p.obstruction);
  const halted = next.queue && next.queue.halted[lane];
  text(`${lane}-badge`, halted ? t("Reset required: {why}", {why: tServer(halted)}) :
    p.jam ? t("Barrier jammed") : p.limit_fault ? t("Limit switch fault injected") : "");
  const visit = next.queue && next.queue.lanes[lane];
  const car = visit && next.queue.cars.find(c => c.id === visit.car);
  text(`${lane}-car-plate`, car ? car.plate : "");
}

function summary(car) {
  const notes = Object.keys(CHOICES).map(key => CHOICES[key][car.profile[key]]).filter(Boolean).map(note => t(note));
  const node = el("span", "what");
  if (notes.length) node.append(el("strong", null, notes.join(", ")), document.createElement("br"));
  node.append(t("Parks {s} s", {s: car.park_s}));
  return node;
}

function chip(car) {
  const button = el("button", "car-chip");
  button.type = "button";
  button.dataset.car = String(car.id);
  button.append(plate(car.plate), summary(car));
  const item = el("li");
  item.append(button);
  return item;
}

function renderQueue(next) {
  const queue = next.queue;
  if (!queue) {
    text("street-note", t("This scenario scripts its own vehicles. Load Car queue to drive cars yourself."));
    text("lot-count", t("Scripted scenario"));
    draw("queue", "scripted", () => []);
    draw("bays", "scripted", () => []);
    const results = next.results || [];
    text("verdict-score", t("{n} checkpoints passed", {n: results.length}));
    draw("verdicts", JSON.stringify(results), () => results.slice().reverse().map(r => {
      const item = el("li", "verdict pass");
      item.append(el("span", "mark", "✓"),
        el("span", null, t("Checkpoint at {s} s on the {lane} lane",
          {s: (r.tick / 100).toFixed(2), lane: t(r.lane)})));
      return item;
    }));
    return;
  }
  const waiting = queue.cars.filter(c => c.state === "queued");
  text("street-note", t(waiting.length ? "Cars wait here for the entry barrier. Select a car to change how it behaves."
    : "The street is empty. Add a car to send it to the entry."));
  draw("queue", JSON.stringify(waiting), () => waiting.map(chip));
  const parked = queue.cars.filter(c => c.spot !== null && c.spot !== undefined);
  // Occupancy as the lot checker counts it: sessions open and close at the
  // beam, slightly before a car's drawn manoeuvre ends.
  const taken = next.lot ? next.lot.sessions.length : queue.inside;
  text("lot-count", t("{n} of {total} spots taken", {n: taken, total: queue.capacity}));
  const bays = Array.from({length: queue.capacity}, (_, spot) => parked.find(c => c.spot === spot));
  draw("bays", JSON.stringify(bays.map(c => c && [c.id, c.plate, c.state, Math.ceil(c.remaining_s || 0)])),
    () => bays.map((car, spot) => {
      const item = el("li", car && car.state === "stuck" ? "bay stuck" : "bay");
      item.append(el("span", "num", t("Spot {n}", {n: spot + 1})));
      if (car) {
        const button = el("button");
        button.type = "button";
        button.dataset.car = String(car.id);
        const when = car.state === "parked" ? t("Leaves in {s} s", {s: Math.ceil(car.remaining_s)}) :
          t(car.state === "stuck" ? "Exit refused. Select to retry." : WHERE[car.state]);
        button.append(plate(car.plate), el("span", "when", when));
        item.append(button);
      } else {
        item.append(el("span", "free", t("Free")));
      }
      return item;
    }));
  const verdicts = queue.verdicts.filter(v => v.status !== "info");
  // Scores come from whole-run totals; the list shows only recent verdicts.
  const totals = queue.totals || {pass: verdicts.filter(v => v.status === "pass").length,
    fail: verdicts.filter(v => v.status === "fail").length};
  const served = totals.pass + totals.fail;
  text("verdict-score", served ? t("{pass} of {served} passed", {pass: totals.pass, served})
    : t("No cars served yet"));
  // Failures older than the recent window stay listed after it.
  const key = v => `${v.car}/${v.lane}/${v.tick}`;
  const recent = new Set(verdicts.map(key));
  const earlier = (queue.failures || []).filter(v => !recent.has(key(v)));
  const shown = [...verdicts.slice().reverse(), ...earlier.slice().reverse()];
  draw("verdicts", JSON.stringify(shown), () => shown.map(v => {
    const item = el("li", `verdict ${v.status}`);
    const outcome = code => OUTCOME[code] ? t(OUTCOME[code]) : code;
    const reason = sentence(tServer(v.reason));
    item.append(el("span", "mark", v.status === "pass" ? "✓" : "✗"),
      el("span", null, t("{plate} at the {lane}: {outcome}",
        {plate: v.plate, lane: t(v.lane), outcome: outcome(v.observed)})),
      el("span", "why", v.status === "pass" ? reason
        : t("Expected: {expected}. {reason}", {expected: v.expected.map(outcome).join(t(" or ")), reason})));
    return item;
  }));
  if (editing !== null) refreshEditor();
}

function controls(next) {
  const done = Boolean(next.failure) || next.finished;
  $("start").disabled = next.running || done;
  $("pause").disabled = !next.running;
  $("add-car").disabled = !next.queue;
  document.querySelectorAll("[data-signal]").forEach(button => { button.disabled = done; });
}

function render(next) {
  // Subscribe/snapshot races can enqueue an older complete frame. Revisions
  // are process-wide and monotonic across reset; never roll the view backward.
  if (state && next.revision <= state.revision) return;
  state = next;
  setConnection("live", "Connected");
  const lanes = next.lanes;
  if (!lanes) return;
  if ($("scenario").options.length === 0) {
    for (const name of next.scenarios) {
      const option = document.createElement("option");
      option.value = name;
      option.textContent = name === "lot" ? t("Car queue") : t("Scenario: {name}", {name});
      $("scenario").append(option);
    }
  }
  const phase = next.failure ? "failed" : next.finished ? "completed" : next.running ? "running" : "paused";
  text("runstate", t({failed: "Failed", completed: "Completed", running: "Running", paused: "Paused"}[phase]));
  for (const name of ["failed", "completed", "running", "paused"]) {
    $("runstate").classList.toggle(name, name === phase);
  }
  const tick = lanes.entry.physical.tick;
  text("clock", (tick / 100).toFixed(2));
  if (next.failure) {
    text("error", t(next.failure.kind === "firmware_failure"
      ? "The firmware broke a lab rule: {message}. Reset to run again."
      : "The simulator stopped: {message}. Reset, or restart the launcher.", {message: next.failure.message}));
  } else {
    const failed = (next.equipment || []).findLast(c => c.status === "failed");
    if (failed) text("error", t("An equipment action could not run at tick {tick}: the scene was not ready for it (for example, Next car needs the previous car past a closed barrier).", {tick: failed.event.tick}));
  }
  for (const lane of LANES) renderLane(lane, lanes[lane], next);
  renderQueue(next);
  controls(next);
  const manual = lanes[currentLane()].physical;
  $("obstruction").checked = manual.obstruction;
  $("jam").checked = manual.jam;
  $("limits").value = String(manual.limit_fault);
  if (next.pixels && next.pixels !== pixels) {
    pixels = next.pixels;
    const ctx = $("display").getContext("2d"), image = ctx.createImageData(240, 320);
    for (let i = 0; i < 240 * 320; i++) {
      const value = parseInt(pixels.slice(i * 4, i * 4 + 4), 16);
      image.data[i * 4] = ((value >> 11) & 31) * 255 / 31;
      image.data[i * 4 + 1] = ((value >> 5) & 63) * 255 / 63;
      image.data[i * 4 + 2] = (value & 31) * 255 / 31;
      image.data[i * 4 + 3] = 255;
    }
    ctx.putImageData(image, 0, 0);
  }
  const owners = Object.keys(next.events).sort();
  const lines = [];
  for (const owner of owners) {
    const [lane, kind] = owner.split(".");
    if (sourceRuns[owner] !== lanes[lane][kind].run) {
      sourceRuns[owner] = lanes[lane][kind].run;
      cursor[owner] = 0;
      uart = "";
      uartTruncated = false;
    }
  }
  for (const owner of owners) {
    for (const event of next.events[owner]) {
      if (event.seq <= cursor[owner]) continue;
      // A complete current-state snapshot need not include the UART prefix.
      // This also applies on first connection/reload, when the cursor is zero.
      if (event.seq > cursor[owner] + 1) {
        uartTruncated = true;
        lines.push({at: event.time_us, owner, seq: event.seq - 0.5,
          text: `\n[${t("Source window gap; state resynchronized. UART history incomplete.")}]\n`});
      }
      cursor[owner] = event.seq;
      if (owner.endsWith(".protocol") && ["rx_byte", "tx_byte"].includes(event.signal)) {
        lines.push({at: event.time_us, owner, seq: event.seq, text:
          `[${event.time_us}µs ${owner.split(".")[0]} ${event.device} ${event.signal}] ${event.value === 10 ? "\\n\n" : String.fromCharCode(event.value)}\n`});
      }
    }
  }
  // Both lanes share one virtual clock: merge their bytes in time order.
  lines.sort((a, b) => a.at - b.at || a.owner.localeCompare(b.owner) || a.seq - b.seq);
  for (const line of lines) uart += line.text;
  if (uart.length > 32768) uartTruncated = true;
  uart = uart.slice(-32768);
  text("uart", (uartTruncated
    ? `[${t("Recent UART suffix only; current state snapshot remains authoritative.")}]\n`
    : "") + uart);
  text("diagnostics", JSON.stringify({lanes, lot: next.lot, counts: next.counts,
    display: next.display}, null, 2));
  text("events", Object.entries(next.events).map(([owner, events]) =>
    `${owner}\n` + events.slice(-40).map(e => `run=${e.run} seq=${e.seq} ${e.time_us}µs tick=${e.tick} ${e.category}/${e.device}/${e.signal}=${e.value}`).join("\n")).join("\n\n"));
  text("results", JSON.stringify({commands: next.commands, equipment: next.equipment,
    checkpoints: next.results}, null, 2));
}

async function command(op, fields = {}, button = null) {
  if (!state) return;
  text("error", "");
  if (button) button.setAttribute("aria-busy", "true");
  try {
    const result = await request("/api/command", {v: 1, run: state.run,
      id: crypto.randomUUID().replaceAll("-", ""), op, ...fields});
    render(await request("/api/snapshot"));
    return result;
  } catch (error) {
    text("error", error.message);
  } finally {
    if (button) button.setAttribute("aria-busy", "false");
  }
}

// ----- car editor --------------------------------------------------------
const FIELDS = ["entry_capture", "exit_capture", "payment", "quirk"];
const EXIT_QUIRKS = ["none", "hesitate", "leave_during_payment"];

function editedCar() {
  return state && state.queue && state.queue.cars.find(c => c.id === editing);
}

function refreshEditor() {
  const car = editedCar();
  if (!car) { closeEditor(); return; }
  const queued = car.state === "queued";
  const inside = ["parked", "stuck"].includes(car.state);
  $("editor-plate").replaceChildren(el("span", null, "BRASIL"), el("b", null, car.plate));
  text("editor-state", queued ? t("Waiting in the street. Every setting can change.")
    : car.state === "stuck" ? t("Its exit was refused. Only its exit can change.")
    : inside ? t("Parked. Only its exit can change.")
    : t("This car is {where}. Its settings are fixed now.", {where: t(WHERE[car.state])}));
  $("editor-retry").hidden = car.state !== "stuck";
  for (const id of ["edit-plate", "edit-entry_capture"]) $(id).disabled = !queued;
  for (const id of ["edit-exit_capture", "edit-payment", "edit-quirk", "edit-park_s"]) {
    $(id).disabled = !(queued || inside);
  }
  $("editor-save").disabled = !(queued || inside);
  $("editor-remove").disabled = !["queued", "gone", "refused"].includes(car.state);
  // Once a car has entered, only exit-side driver behaviour still applies.
  for (const option of $("edit-quirk").options) {
    option.disabled = !queued && !EXIT_QUIRKS.includes(option.value);
  }
}

function openEditor(id, anchor) {
  editing = id;
  const car = editedCar();
  if (!car) return;
  $("edit-plate").value = car.plate;
  for (const key of FIELDS) $(`edit-${key}`).value = car.profile[key];
  $("edit-park_s").value = String(car.park_s);
  refreshEditor();
  const editor = $("editor");
  editor.hidden = false;
  const box = anchor.getBoundingClientRect(), width = editor.offsetWidth, height = editor.offsetHeight;
  editor.style.left = `${Math.max(16, Math.min(box.right + 10, innerWidth - width - 16))}px`;
  editor.style.top = `${Math.max(16, Math.min(box.top, innerHeight - height - 16))}px`;
  (car.state === "stuck" ? editor.querySelector("[data-retry]") : $("edit-payment")).focus();
}

function closeEditor() {
  editing = null;
  $("editor").hidden = true;
}

async function saveEditor(event) {
  event.preventDefault();
  const car = editedCar();
  if (!car) return;
  const profile = {exit_capture: $("edit-exit_capture").value, payment: $("edit-payment").value,
    park_s: Number($("edit-park_s").value)};
  if (car.state === "queued") {
    Object.assign(profile, {plate: $("edit-plate").value.toUpperCase(),
      entry_capture: $("edit-entry_capture").value, quirk: $("edit-quirk").value});
    if (profile.plate === car.plate) delete profile.plate;
  } else if (EXIT_QUIRKS.includes($("edit-quirk").value)) {
    profile.quirk = $("edit-quirk").value;
  }
  if (await command("car.update", {car: car.id, profile}, $("editor-save")) !== undefined) closeEditor();
}

// ----- wiring --------------------------------------------------------------
async function connect() {
  const token = new URLSearchParams(location.hash.slice(1)).get("token");
  // The capability never enters an HTTP URL, Referer, history entry, or API log.
  history.replaceState(null, "", location.pathname);
  if (token) {
    const response = await fetch("/api/session", {method: "POST",
      credentials: "same-origin", headers: {"X-ZVB-Token": token}});
    if (!response.ok) throw new Error(t("Session authentication failed"));
  }
  render(await request("/api/snapshot"));
  stream = new EventSource("/api/events");
  stream.addEventListener("snapshot", e => {
    setConnection("live", "Connected");
    render(JSON.parse(e.data));
  });
  stream.addEventListener("message", e => render(JSON.parse(e.data)));
  stream.onerror = () => setConnection("lost", "Reconnecting. The view may be out of date.");
}

function selectTab(tab) {
  document.querySelectorAll('[role="tab"]').forEach(other => {
    other.setAttribute("aria-selected", String(other === tab));
    $(other.getAttribute("aria-controls")).hidden = other !== tab;
  });
}

// Redraw the current snapshot, even though its revision is not newer.
function redraw() {
  const shown = state;
  state = null;
  if (shown) render(shown);
}

$("lane").onchange = redraw;
onLanguage(() => {
  for (const id of Object.keys(drawn)) delete drawn[id];
  const chosen = $("scenario").value;
  $("scenario").replaceChildren();
  setConnection(connectionShown.kind, connectionShown.message);
  redraw();
  if (chosen) $("scenario").value = chosen;
});
for (const op of ["start", "pause", "reset"]) $(op).onclick = () => command(op, {}, $(op));
$("select").onclick = () => command("select", {scenario: $("scenario").value}, $("select"));
$("add-car").onclick = () => command("car.add", {profile: {}}, $("add-car"));
for (const id of ["queue", "bays"]) {
  $(id).addEventListener("click", event => {
    const target = event.target.closest("[data-car]");
    if (target) openEditor(Number(target.dataset.car), target);
  });
}
$("editor-close").onclick = closeEditor;
$("editor-form").addEventListener("submit", saveEditor);
$("editor-remove").onclick = async () => {
  if (await command("car.remove", {car: editing}, $("editor-remove")) !== undefined) closeEditor();
};
document.querySelectorAll("[data-retry]").forEach(button => {
  button.onclick = async () => {
    if (await command("car.retry", {car: editing, payment: button.dataset.retry}, button) !== undefined) {
      closeEditor();
    }
  };
});
document.querySelectorAll('[role="tab"]').forEach(tab => { tab.onclick = () => selectTab(tab); });
document.querySelectorAll("[data-signal]").forEach(button => {
  button.onclick = () => command("submit", {lane: button.dataset.lane || currentLane(),
    signal: button.dataset.signal, value: Number(button.dataset.value)}, button);
});
for (const [id, signal] of [["obstruction", "sensor.obstruction"], ["jam", "fault.jam"]])
  $(id).onchange = () => command("submit", {lane: currentLane(), signal, value: Number($(id).checked)});
for (const [id, signal] of [["limits", "fault.limits"], ["noise", "sensor.noise"]])
  $(id).onchange = () => command("submit", {lane: currentLane(), signal, value: Number($(id).value)});
document.addEventListener("keydown", event => {
  if (event.key === "Escape") { closeEditor(); return; }
  if (event.ctrlKey || event.metaKey || event.altKey || !state) return;
  if (event.target.closest && event.target.closest("input, select, textarea, [role=dialog]")) return;
  const key = event.key.toLowerCase();
  if (key === " ") {
    event.preventDefault();
    const button = state.running ? $("pause") : $("start");
    if (!button.disabled) button.click();
  } else if (key === "r") {
    $("reset").click();
  } else if (key === "a" && !$("add-car").disabled) {
    $("add-car").click();
  }
});
// Wide screens have a column for diagnostics, so show them open there.
if (typeof matchMedia === "function") {
  const wide = matchMedia("(min-width: 1500px)");
  $("drawer").open = wide.matches;
  wide.addEventListener("change", event => { $("drawer").open = event.matches; });
}
connect().catch(error => {
  setConnection("lost", "Not connected. Open the link the launcher printed.");
  text("error", error.message);
});
