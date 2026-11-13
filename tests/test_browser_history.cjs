/* Bounded host-only regression checks: execute the real renderer, no browser/Renode. */
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname, "../web/app.js"), "utf8");
const warning = "UART history incomplete.";

function renderer() {
  const elements = new Map();
  const element = id => {
    if (!elements.has(id)) elements.set(id, {
      textContent: "", options: [{}], classList: {toggle() {}}, setAttribute() {}
    });
    return elements.get(id);
  };
  const context = vm.createContext({
    document: {getElementById: element, querySelectorAll: () => []},
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
    text: id => element(id).textContent
  };
}

function event(seq, signal = "tx_byte", value = 65) {
  return {run: "protocol-1", seq, time_us: seq * 10, tick: seq,
    category: "uart", device: "terminal", signal, value};
}

function snapshot(revision, events = {}, overrides = {}) {
  return {
    revision, run: "epoch-1", run_index: 0, scenario: "manual",
    scenarios: [], commands: [], results: [], pixels: null,
    physical: {run: "physical-1", tick: revision, vehicle_mm: 1234,
      position_mdeg: 90000, adc_code: 987, obstruction: false, enable: false,
      direction: true, jam: false, limit_fault: 0},
    protocol: {run: "protocol-1", red: false, green: true, buzzer: false},
    events: {physical: [], protocol: [], ...events},
    ...overrides
  };
}

for (const owner of ["physical", "protocol"]) {
  test(`first truncated ${owner} prefix is diagnosed without invalidating current state`, () => {
    const view = renderer();
    view.render(snapshot(1, {[owner]: [event(17)]}));
    assert.equal(view.text("uart").split(warning).length - 1, 1);
    assert.equal(view.text("connection"), "Connected · current source snapshot");
    assert.match(view.text("sensor"), /Vehicle 1234 mm · ADC4 987/);
    assert.match(view.text("equipment"), /Barrier 90\.0°/);
    assert.equal(JSON.parse(view.text("diagnostics")).physical.tick, 1);
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
  reset.physical.run = "physical-2";
  reset.protocol.run = "protocol-2";
  view.render(reset);
  assert.equal(view.text("uart"), "");
  reset.revision = 3;
  reset.events.protocol = [{...event(1), run: "protocol-2"}];
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
  next.physical.run = "physical-2";
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
  next.physical.run = "physical-2";
  next.protocol.run = "protocol-2";
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
  assert.equal(view.text("connection"), "Connected · current source snapshot");
  assert(!view.text("uart").includes("[10µs"));
});
