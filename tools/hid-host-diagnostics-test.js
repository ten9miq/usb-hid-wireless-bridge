"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

class Element {
  constructor() {
    this.children = [];
    this.textContent = "";
  }

  append(child) { this.children.push(child); }
  prepend(child) { this.children.unshift(child); }
  replaceChildren(...children) { this.children = children; }
  get lastElementChild() { return this.children.at(-1); }
  remove() {}
  addEventListener() {}
}

const elements = new Map([
  ["#status", new Element()], ["#heartbeat", new Element()],
  ["#last-received", new Element()], ["#raw", new Element()],
  ["#events", new Element()], ["#counters", new Element()],
  ["#usage-counters", new Element()], ["#output-counters", new Element()],
  ["#hcd-snapshots", new Element()], ["#capture-hcd-baseline", new Element()],
  ["#hcd-baseline-status", new Element()],
  ["#diagnostic-transport", new Element()],
  ["#connect", new Element()],
]);
const document = {
  createElement: () => new Element(),
  querySelector: selector => elements.get(selector),
};
const testApi = {};
const source = fs.readFileSync("tools/hid-host-diagnostics.html", "utf8");
const script = source.match(/<script>([\s\S]*)<\/script>/)[1];
vm.runInNewContext(script, {
  document,
  globalThis: { __HID_HOST_DIAGNOSTICS_TEST__: testApi },
  navigator: {},
  Date,
  Uint8Array,
});

const bytes = new Uint8Array(63);
const view = new DataView(bytes.buffer);
bytes.set([0x48, 0x48, 0x44, 0x31, 2, 12, 1, 3, 1]); // HHD1, v2, event 12
view.setUint16(16, 0x046d, true);
view.setUint16(18, 0xc06b, true);
view.setUint16(22, 8, true); // B-side last report length
bytes[24] = 8;
view.setUint32(25, 17, true);
view.setUint32(29, 250, true);
view.setUint16(36, 12, true); // A-side last report length
bytes[40] = 8;
view.setUint32(41, 16, true);
view.setUint32(45, 500, true);
testApi.addEvent({ reportId: 0x66, data: new DataView(bytes.buffer) });

assert.equal(testApi.counterSnapshots.size, 1);
assert.equal(elements.get("#counters").children.length, 1);
const cells = elements.get("#counters").children[0].children;
assert.match(cells[3].textContent, /count 17; last 8 B; age 250 ms/);
assert.match(cells[4].textContent, /count 16; last 12 B; age 500 ms/);

bytes[5] = 2; // unmount for the same address / instance
testApi.addEvent({ reportId: 0x66, data: new DataView(bytes.buffer) });
assert.equal(testApi.counterSnapshots.size, 0);
assert.equal(elements.get("#counters").children.length, 1);
assert.equal(elements.get("#counters").children[0].children[0].textContent,
  "No counter snapshots received");

bytes.fill(0);
bytes.set([0x48, 0x48, 0x44, 0x31, 2, 13, 1]);
bytes[15] = 2;
bytes[24] = 8;
view.setUint32(25, 21, true);
view.setUint32(29, 22, true);
bytes[40] = 20;
view.setUint32(41, 31, true);
view.setUint32(45, 32, true);
view.setInt32(49, -7, true);
view.setInt32(53, 9, true);
view.setUint32(57, 33, true);
testApi.addEvent({ reportId: 0x66, data: new DataView(bytes.buffer) });
assert.equal(testApi.usageCounterSnapshots.size, 1);
const usageCells = elements.get("#usage-counters").children[0].children;
assert.equal(usageCells[1].textContent, "21 / 22");
assert.equal(usageCells[3].textContent, "-7 / 9");

bytes.fill(0);
bytes.set([0x48, 0x48, 0x44, 0x31, 2, 14, 1]);
bytes[24] = 8;
view.setUint32(25, 41, true);
view.setUint32(29, 42, true);
bytes[40] = 20;
view.setUint32(41, 51, true);
view.setUint32(45, 52, true);
view.setUint32(49, 61, true);
view.setUint32(53, 62, true);
view.setUint32(57, 63, true);
testApi.addEvent({ reportId: 0x66, data: new DataView(bytes.buffer) });
const outputCells = elements.get("#output-counters").children[0].children;
assert.equal(outputCells[0].textContent, "41 / 42");
assert.equal(outputCells[4].textContent, "63");

bytes.fill(0);
bytes.set([0x48, 0x48, 0x44, 0x31, 2, 15, 1, 1, 15, 1, 7, 6]);
view.setUint32(13, 2, true);
view.setUint32(17, 3, true);
view.setUint32(21, 4, true);
view.setUint32(25, 0x0000000e, true);
view.setUint32(29, 0x00000006, true);
view.setUint16(33, 3, true);
view.setUint16(35, 7, true);
view.setUint16(37, 2, true);
view.setUint16(39, 4, true);
view.setUint16(41, 6, true);
testApi.addEvent({ reportId: 0x66, data: new DataView(bytes.buffer) });
const hcd = testApi.getLatestHcdSnapshot();
assert.equal(hcd.hidInstances, 6);
assert.equal(hcd.timeouts, 2);
assert.equal(hcd.configured, 7);
assert.equal(elements.get("#capture-hcd-baseline").disabled, false);
const hcdCells = elements.get("#hcd-snapshots").children[0].children;
assert.match(hcdCells[3].textContent, /1:C--- 2:CK-B 3:C-AB/);

bytes.fill(0);
bytes.set([0x48, 0x48, 0x44, 0x31, 2, 16, 1]);
bytes[24] = 8;
view.setUint32(25, 7, true); // event-15 generated
view.setUint32(29, 19, true); // diagnostic serial writes accepted
bytes[40] = 16;
view.setUint32(41, 3, true); // diagnostic serial writes rejected
view.setUint32(45, 0x00010302, true); // ordinary=2, event-12=3, event-15 pending
view.setUint32(49, 11, true); // event-12 generated
view.setUint32(53, 10, true); // event-12 sent
testApi.addEvent({ reportId: 0x66, data: new DataView(bytes.buffer) });
const transport = testApi.getLatestDiagnosticTransportSnapshot();
assert.equal(transport.hcdGenerated, 7);
assert.equal(transport.serialFailure, 3);
assert.equal(transport.counterSent, 10);
const transportCells = elements.get("#diagnostic-transport").children[0].children;
assert.equal(transportCells[1].textContent, "success 19; failure 3");
assert.match(transportCells[2].textContent, /ordinary 2; event 12 3; event 15 yes/);
console.log("hid-host-diagnostics event 12/13/14/15/16 parser: PASS");
