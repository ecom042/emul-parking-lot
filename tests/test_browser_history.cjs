/* Bounded host-only regression checks: execute the real renderer, no browser/Renode. */
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname, "../web/app.js"), "utf8");
const warning = "UART history incomplete.";

// Minimal DOM: records text, classes, attributes and rebuilt children.
function node() {
  const classes = new Set();
  return {
    textContent: "", options: [{}], value: "", disabled: false, dataset: {}, style: {},
    children: [], attributes: {},
    classList: {toggle(name, on) { if (on) classes.add(name); else classes.delete(name); },
      contains: name => classes.has(name)},
    setAttribute(name, value) { this.attributes[name] = value; },
    addEventListener() {},
    append(...items) { this.children.push(...items); },
    replaceChildren(...items) { this.children = items; },
  };
}

function renderer() {
  const elements = new Map();
  const element = id => {
    if (!elements.has(id)) elements.set(id, node());
    return elements.get(id);
  };
  const context = vm.createContext({
    document: {getElementById: element, querySelectorAll: () => [],
      createElement: () => node(), addEventListener() {}},
    URLSearchParams, location: {hash: "", pathname: "/"},
    history: {replaceState() {}},
    // Leave bootstrap pending: tests supply snapshots directly, without I/O.
    fetch: () => new Promise(() => {})
  });
  vm.runInContext(source, context, {timeout: 1000});
  return {
    render(snapshot) {
      // HTTP/SSE delivers a fresh object for every publication.
      context.snapshot = JSON.parse(JSON.stringify(snapshot));
      vm.runInContext("render(snapshot)", context, {timeout: 1000});
    },
    text: id => element(id).textContent,
    element
  };
}

function event(seq, signal = "tx_byte", value = 65) {
  return {run: "protocol-1", seq, time_us: seq * 10, tick: seq,
    category: "uart", device: "terminal", signal, value};
}

// Two lanes of owners; the tests exercise the entry lane's pair.
const OWNER = {physical: "entry.physical", protocol: "entry.protocol"};
const owned = events => Object.fromEntries(
  Object.entries(events).map(([kind, list]) => [OWNER[kind], list]));

function snapshot(revision, events = {}, overrides = {}) {
  return {
    revision, run: "epoch-1", run_index: 0, scenario: "lot",
    scenarios: [], commands: [], equipment: [], results: [], pixels: null,
    lot: {capacity: 4, sessions: []},
    lanes: {
      entry: {
        physical: {run: "physical-1", tick: revision, vehicle_mm: 1234,
          position_mdeg: 90000, adc_code: 987, obstruction: false, enable: false,
          direction: true, jam: false, limit_fault: 0},
        protocol: {run: "protocol-1", red: false, green: true, buzzer: false}},
      exit: {
        physical: {run: "physical-x", tick: revision, vehicle_mm: 6000,
          position_mdeg: 0, adc_code: 4095, obstruction: false, enable: false,
          direction: false, jam: false, limit_fault: 0},
        protocol: {run: "protocol-x", red: true, green: false, buzzer: false}}},
    events: owned({physical: [], protocol: [], ...events}),
    ...overrides
  };
}

for (const owner of ["physical", "protocol"]) {
  test(`first truncated ${owner} prefix is diagnosed without invalidating current state`, () => {
    const view = renderer();
    view.render(snapshot(1, {[owner]: [event(17)]}));
    assert.equal(view.text("uart").split(warning).length - 1, 1);
    assert.equal(view.text("connection"), "Connected");
    assert.equal(view.text("entry-vehicle"), "1234 mm");
    assert.equal(view.text("entry-adc"), "987");
    assert.equal(view.text("entry-angle"), "90.0°");
    assert.equal(JSON.parse(view.text("diagnostics")).lanes.entry.physical.tick, 1);
  });
}

test("complete prefix and overlapping publications do not diagnose a gap or duplicate UART", () => {
  const view = renderer();
  view.render(snapshot(1, {physical: [event(1)], protocol: [event(1), event(2)]}));
  const before = view.text("uart");
  assert(!before.includes(warning));
  view.render(snapshot(2, {physical: [event(1), event(2)],
    protocol: [event(1), event(2), event(3)]}));
  assert(!view.text("uart").includes(warning));
  assert.equal(view.text("uart"), before + "[30µs terminal tx_byte] A\n");
});

test("empty initial windows defer prefix diagnosis until the first event arrives", () => {
  const view = renderer();
  view.render(snapshot(1));
  assert.equal(view.text("uart"), "");
  view.render(snapshot(2, {protocol: [event(5)]}));
  assert(view.text("uart").includes(warning));
});

test("subsequent gaps are diagnosed once, including gaps between events in a frame", () => {
  const view = renderer();
  view.render(snapshot(1, {protocol: [event(1)]}));
  view.render(snapshot(2, {protocol: [event(4), event(6)]}));
  const before = view.text("uart");
  assert.equal(before.split(warning).length - 1, 2);
  view.render(snapshot(3, {protocol: [event(4), event(6)]}));
  assert.equal(view.text("uart"), before);
});

test("reset with fresh source runs clears prior history and restarts sequence tracking", () => {
  const view = renderer();
  view.render(snapshot(1, {protocol: [event(20)]}));
  assert(view.text("uart").includes(warning));
  const reset = snapshot(2, {}, {run: "epoch-2"});
  reset.lanes.entry.physical.run = "physical-2";
  reset.lanes.entry.protocol.run = "protocol-2";
  view.render(reset);
  assert.equal(view.text("uart"), "");
  reset.revision = 3;
  reset.events["entry.protocol"] = [{...event(1), run: "protocol-2"}];
  view.render(reset);
  assert.equal(view.text("uart"), "[10µs terminal tx_byte] A\n");
  view.render(snapshot(1, {protocol: [event(30)]}));
  assert.equal(view.text("uart"), "[10µs terminal tx_byte] A\n");
});

test("source-run changes, not host epochs, scope independent sequence cursors", () => {
  const view = renderer();
  view.render(snapshot(1, {physical: [event(1)], protocol: [event(1)]}));
  view.render(snapshot(2, {physical: [event(2)], protocol: [event(2)]},
    {run: "epoch-2"}));
  assert(!view.text("uart").includes(warning));
  // Only physical restarts. Its complete prefix must not hide a protocol gap,
  // nor reset the unchanged protocol cursor (which would duplicate byte 2).
  const next = snapshot(3, {physical: [{...event(1), run: "physical-2"}],
    protocol: [event(2), event(4)]});
  next.lanes.entry.physical.run = "physical-2";
  view.render(next);
  assert.equal(view.text("uart").split(warning).length - 1, 1);
  assert(!view.text("uart").includes("[20µs"));
  assert(view.text("uart").includes("[40µs"));
});

test("a newly observed source run can itself start with a truncated prefix", () => {
  const view = renderer();
  view.render(snapshot(1, {protocol: [event(1)]}));
  const next = snapshot(2, {physical: [{...event(9), run: "physical-2"}],
    protocol: [{...event(7), run: "protocol-2"}]});
  next.lanes.entry.physical.run = "physical-2";
  next.lanes.entry.protocol.run = "protocol-2";
  view.render(next);
  assert.equal(view.text("uart").split(warning).length - 1, 2);
  assert(!view.text("uart").includes("[10µs"));
});

test("local text eviction keeps a persistent suffix notice without invalidating snapshot", () => {
  const view = renderer();
  for (let frame = 0; frame < 16; frame++) {
    const events = Array.from({length: 128}, (_, i) => event(frame * 128 + i + 1));
    view.render(snapshot(frame + 1, {protocol: events}));
  }
  assert(view.text("uart").startsWith("[Recent UART suffix only;"));
  assert(view.text("uart").length < 33000);
  assert.equal(view.text("connection"), "Connected");
  assert(!view.text("uart").includes("[10µs"));
});

test("controls follow the run state", () => {
  const view = renderer();
  view.render(snapshot(1, {}, {running: true}));
  assert.equal(view.text("runstate"), "Running");
  assert.equal(view.element("start").disabled, true);
  assert.equal(view.element("pause").disabled, false);
  view.render(snapshot(2, {}, {running: false, finished: true}));
  assert.equal(view.text("runstate"), "Completed");
  assert.equal(view.element("start").disabled, true);
  assert.equal(view.element("pause").disabled, true);
  view.render(snapshot(3, {}, {failure: {kind: "firmware_failure", message: "motor_deadline"}}));
  assert.equal(view.text("runstate"), "Failed");
  assert.match(view.text("error"), /firmware broke a lab rule: motor_deadline/);
});

test("lane scene reflects equipment state, not firmware claims", () => {
  const view = renderer();
  const frame = snapshot(1);
  frame.lanes.exit.physical = {...frame.lanes.exit.physical, enable: true, direction: true,
    obstruction: true, jam: true, position_mdeg: 45000, vehicle_mm: 800};
  frame.lanes.exit.protocol.buzzer = true;
  view.render(frame);
  assert.equal(view.text("exit-motor"), "opening");
  assert.equal(view.text("exit-beam-state"), "blocked");
  assert.equal(view.text("exit-badge"), "Barrier jammed");
  // The exit scene is mirrored, so its arm swings the other way.
  assert.equal(view.element("exit-barrier").attributes.transform, "rotate(45)");
  assert.equal(view.element("exit-car").attributes.transform, "translate(320,75)");
  assert(view.element("exit-buzzer").classList.contains("on"));
  assert(view.element("entry-green").classList.contains("on"));
  assert(!view.element("entry-red").classList.contains("on"));
});

test("car queue fills the street, bays and verdict log", () => {
  const view = renderer();
  const car = (id, state, extra = {}) => ({id, plate: `ABC${id}D2${id}`, state, park_s: 5,
    spot: null, remaining_s: null, note: "", profile: {entry_capture: "normal",
      exit_capture: "normal", payment: "paid", quirk: "none"}, ...extra});
  view.render(snapshot(1, {}, {queue: {
    capacity: 3, inside: 2, halted: {exit: "barrier did not close"},
    lanes: {entry: {car: 1, phase: "wait"}, exit: null},
    cars: [car(1, "entering"), car(2, "queued", {profile: {entry_capture: "failed",
      exit_capture: "normal", payment: "paid", quirk: "none"}}),
      car(3, "parked", {spot: 0, remaining_s: 4.2}), car(4, "stuck", {spot: 2})],
    verdicts: [{car: 5, plate: "XYZ9A87", lane: "exit", tick: 900, expected: ["exited"],
      observed: "refused", status: "fail", reason: "no opening before deadline"},
      {car: 6, plate: "XYZ9A88", lane: "entry", tick: 950, expected: ["refused"],
        observed: "refused", status: "pass", reason: "assistance requested"},
      {car: 6, plate: "XYZ9A88", lane: "entry", tick: 960, expected: ["assisted"],
        observed: "pending", status: "info", reason: ""}]}}));
  assert.equal(view.element("queue").children.length, 1);
  assert.equal(view.element("bays").children.length, 3);
  assert.equal(view.text("lot-count"), "2 of 3 spots taken");
  assert.equal(view.text("entry-car-plate"), "ABC1D21");
  assert.match(view.text("exit-badge"), /Reset required: barrier did not close/);
  assert.equal(view.element("verdicts").children.length, 2);
  assert.equal(view.text("verdict-score"), "1 of 2 passed");
  assert.equal(view.element("add-car").disabled, false);
});
