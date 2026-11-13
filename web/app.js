"use strict";
const $ = (id) => document.getElementById(id);
let state = null, stream = null, pixels = null;
let uart = "", cursor = {physical: 0, protocol: 0}, sourceRuns = {};
let uartTruncated = false;
const text = (id, value) => { $(id).textContent = value; };

async function request(path, body) {
  const response = await fetch(path, {
    method: body ? "POST" : "GET", credentials: "same-origin",
    headers: body ? {"Content-Type": "application/json"} : {},
    body: body ? JSON.stringify(body) : undefined
  });
  const bodyText = await response.text();
  let result = {};
  try { result = bodyText ? JSON.parse(bodyText) : {}; } catch (_) {
    if (response.ok) throw new Error("Invalid response from local bridge");
  }
  if (!response.ok) throw new Error(result.error || `Request failed: HTTP ${response.status}`);
  return result;
}

function render(next) {
  // Subscribe/snapshot races can enqueue an older complete frame. Revisions
  // are process-wide and monotonic across reset; never roll the view backward.
  if (state && next.revision <= state.revision) return;
  state = next;
  text("connection", "Connected · current source snapshot");
  const p = next.physical, q = next.protocol;
  if (!p || !q) return;
  if ($("scenario").options.length === 0) {
    for (const name of next.scenarios) {
      const option = document.createElement("option");
      option.value = option.textContent = name;
      $("scenario").append(option);
    }
  }
  text("runstate", `${next.scenario} · run ${next.run.slice(0, 8)} · segment ${next.run_index + 1} · virtual tick ${p.tick} (${(p.tick / 100).toFixed(2)} s) · ${next.running ? "running" : next.finished ? "completed" : "paused"}`);
  if (next.failure) text("error", `${next.failure.kind}: ${next.failure.message}`);
  else {
    const failed = next.commands.findLast(c => c.status === "failed");
    if (failed) text("error", `Equipment command ${failed.event.value} failed at source tick ${failed.event.tick}; inspect diagnostics. Scene preconditions were not met.`);
  }
  $("car").setAttribute("transform", `translate(${100 + (6000 - p.vehicle_mm) * 0.09},145)`);
  // Down crosses the lane; source millidegrees rotate the arm out of the lane.
  $("barrier").setAttribute("transform", `rotate(${p.position_mdeg / 1000})`);
  $("red").classList.toggle("on", q.red);
  $("green").classList.toggle("on", q.green);
  $("beam").classList.toggle("active", p.obstruction);
  text("sensor", `Vehicle ${p.vehicle_mm} mm · ADC4 ${p.adc_code}/4095 · beam ${p.obstruction ? "ACTIVE" : "clear"}`);
  text("equipment", `Barrier ${(p.position_mdeg / 1000).toFixed(1)}° · motor ${p.enable ? "ENABLED" : "off"} / ${p.direction ? "opening" : "closing"} · red ${q.red} · green ${q.green} · buzzer ${q.buzzer}`);
  $("obstruction").checked = p.obstruction;
  $("jam").checked = p.jam;
  $("limits").value = String(p.limit_fault);
  if (next.pixels !== pixels) {
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
  for (const owner of ["physical", "protocol"]) {
    if (sourceRuns[owner] !== next[owner].run) {
      sourceRuns[owner] = next[owner].run;
      cursor[owner] = 0;
      uart = "";
      uartTruncated = false;
    }
  }
  for (const owner of ["physical", "protocol"]) {
    for (const event of next.events[owner]) {
      if (event.seq <= cursor[owner]) continue;
      // A complete current-state snapshot need not include the UART prefix.
      // This also applies on first connection/reload, when the cursor is zero.
      if (event.seq > cursor[owner] + 1) {
        uartTruncated = true;
        uart += "\n[Source window gap; state resynchronized. UART history incomplete.]\n";
      }
      cursor[owner] = event.seq;
      if (owner === "protocol" && ["rx_byte", "tx_byte"].includes(event.signal))
        uart += `[${event.time_us}µs ${event.device} ${event.signal}] ${event.value === 10 ? "\\n\n" : String.fromCharCode(event.value)}\n`;
    }
  }
  if (uart.length > 32768) uartTruncated = true;
  uart = uart.slice(-32768);
  text("uart", (uartTruncated
    ? "[Recent UART suffix only; current state snapshot remains authoritative.]\n"
    : "") + uart);
  text("diagnostics", JSON.stringify({physical: p, protocol: q, display: next.display}, null, 2));
  text("events", Object.entries(next.events).map(([owner, events]) =>
    `${owner}\n` + events.slice(-40).map(e => `run=${e.run} seq=${e.seq} ${e.time_us}µs tick=${e.tick} ${e.category}/${e.device}/${e.signal}=${e.value}`).join("\n")).join("\n\n"));
  text("results", JSON.stringify({commands: next.commands, checkpoints: next.results}, null, 2));
}

async function command(op, fields = {}) {
  if (!state) return;
  text("error", "");
  try {
    await request("/api/command", {v: 1, run: state.run,
      id: crypto.randomUUID().replaceAll("-", ""), op, ...fields});
    render(await request("/api/snapshot"));
  } catch (error) { text("error", error.message); }
}

async function connect() {
  const token = new URLSearchParams(location.hash.slice(1)).get("token");
  // The capability never enters an HTTP URL, Referer, history entry, or API log.
  history.replaceState(null, "", location.pathname);
  if (token) {
    const response = await fetch("/api/session", {method: "POST",
      credentials: "same-origin", headers: {"X-ZVB-Token": token}});
    if (!response.ok) throw new Error("Session authentication failed");
  }
  render(await request("/api/snapshot"));
  stream = new EventSource("/api/events");
  stream.addEventListener("snapshot", e => {
    text("connection", "Connected · current source snapshot");
    render(JSON.parse(e.data));
  });
  stream.addEventListener("message", e => render(JSON.parse(e.data)));
  stream.onerror = () => text("connection", "Disconnected · displayed state is stale · reconnecting for a full snapshot");
}
for (const op of ["start", "pause", "reset"]) $(op).onclick = () => command(op);
$("select").onclick = () => command("select", {scenario: $("scenario").value});
document.querySelectorAll("[data-signal]").forEach(button => {
  button.onclick = () => command("submit", {signal: button.dataset.signal, value: Number(button.dataset.value)});
});
for (const [id, signal] of [["obstruction", "sensor.obstruction"], ["jam", "fault.jam"]])
  $(id).onchange = () => command("submit", {signal, value: Number($(id).checked)});
for (const [id, signal] of [["limits", "fault.limits"], ["noise", "sensor.noise"]])
  $(id).onchange = () => command("submit", {signal, value: Number($(id).value)});
connect().catch(error => { text("connection", "Not authenticated or backend unavailable"); text("error", error.message); });
