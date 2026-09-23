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
  addEventListener(type, callback) { this.listeners ??= new Map(); this.listeners.set(type, callback); }
  removeEventListener(type) { this.listeners?.delete(type); }
}

const elements = new Map([
  ["#status", new Element()], ["#heartbeat", new Element()],
  ["#last-received", new Element()], ["#raw", new Element()],
  ["#b-runtime", new Element()], ["#g700-health", new Element()],
  ["#keyboard-health", new Element()], ["#input-g700-health", new Element()],
  ["#b-flash-loader", new Element()],
  ["#events", new Element()], ["#counters", new Element()],
  ["#usage-counters", new Element()], ["#output-counters", new Element()],
  ["#hcd-snapshots", new Element()], ["#capture-hcd-baseline", new Element()],
  ["#hcd-baseline-status", new Element()],
  ["#diagnostic-transport", new Element()],
  ["#timeline", new Element()],
  ["#connection-history", new Element()],
  ["#connect", new Element()],
  ["#flash-b-side", new Element()], ["#flash-b-status", new Element()],
]);
const document = {
  createElement: () => new Element(),
  querySelector: selector => elements.get(selector),
};
const testApi = {};
const hidListeners = new Map();
let permittedDevice;
let reconnectTick;
const hid = {
  addEventListener(type, callback) { hidListeners.set(type, callback); },
  async requestDevice() { return [permittedDevice]; },
  async getDevices() { return permittedDevice ? [permittedDevice] : []; },
};
const source = fs.readFileSync("tools/hid-host-diagnostics.html", "utf8");
const script = source.match(/<script>([\s\S]*)<\/script>/)[1];
vm.runInNewContext(script, {
  document,
  globalThis: {
    __HID_HOST_DIAGNOSTICS_TEST__: testApi,
    location: { search: "?restoreStableB=1" },
  },
  navigator: { hid },
  setInterval(callback) { reconnectTick = callback; return 1; },
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
assert.match(elements.get("#timeline").textContent, /output: built 61; queued 62; USB sent 63/);

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
view.setUint32(43, 4, true); // host event queue drops
testApi.addEvent({ reportId: 0x66, data: new DataView(bytes.buffer) });
const hcd = testApi.getLatestHcdSnapshot();
assert.equal(hcd.hidInstances, 6);
assert.equal(hcd.timeouts, 2);
assert.equal(hcd.configured, 7);
assert.equal(elements.get("#capture-hcd-baseline").disabled, false);
const hcdCells = elements.get("#hcd-snapshots").children[0].children;
assert.match(hcdCells[3].textContent, /1:C--- 2:CK-B 3:C-AB/);
assert.equal(hcdCells[7].textContent, "4");

bytes.fill(0);
bytes.set([0x48, 0x48, 0x44, 0x31, 2, 16, 1]);
bytes[24] = 8;
view.setUint32(25, 7, true); // event-15 generated
view.setUint32(29, 19, true); // diagnostic serial writes accepted
bytes[40] = 20;
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

bytes.fill(0);
bytes.set([0x48, 0x48, 0x44, 0x31, 2, 17, 1]);
bytes[24] = 1;
bytes[25] = 5;
testApi.addEvent({ reportId: 0x66, data: new DataView(bytes.buffer) });
assert.match(elements.get("#b-runtime").textContent, /HHD5 handshake confirmed/);

bytes.fill(0);
bytes.set([0x48, 0x48, 0x44, 0x31, 2, 18, 1, 2, 3]);
view.setUint16(22, 2, true); // receive-ready, not pending
bytes[24] = 8;
view.setUint32(25, 10, true);
view.setUint32(29, 11, true);
bytes[40] = 4;
view.setUint32(41, 1, true);
testApi.addEvent({ reportId: 0x66, data: new DataView(bytes.buffer) });
assert.match(elements.get("#g700-health").textContent, /callbacks 10; arms 11; arm failures 1; pending false; receive-ready true/);

bytes.fill(0);
bytes.set([0x48, 0x48, 0x44, 0x31, 2, 19, 1]);
view.setUint16(22, 52932, true);
view.setUint32(25, 5, true);
view.setUint32(29, 0, true);
testApi.addEvent({ reportId: 0x66, data: new DataView(bytes.buffer) });
assert.match(elements.get("#b-flash-loader").textContent, /verified; detail 00000000; image 52932 bytes/);
bytes.fill(0);
bytes.set([0x48, 0x48, 0x44, 0x31, 2, 20, 1, 1, 0]);
view.setUint16(16, 0x0853, true);
view.setUint16(18, 0x0142, true);
view.setUint16(22, 0, true);
bytes[24] = 8;
view.setUint32(25, 0, true);
view.setUint32(29, 1, true);
bytes[40] = 20;
view.setUint32(41, 0, true);
view.setUint32(45, 2, true);
view.setUint32(49, 1, true);
view.setUint32(53, 1, true);
view.setUint32(57, 7, true);
testApi.addEvent({ reportId: 0x66, data: new DataView(bytes.buffer) });
assert.match(elements.get("#keyboard-health").textContent,
  /callbacks 0; arms 1; arm failures 0; pending false; receive-ready false; host queue drops 2; transfer-completion drops 1; deferred-function drops 1; SOF deferred by reserve 7/);
console.log("hid-host-diagnostics event 12/13/14/15/16/20 parser: PASS");

function mockDevice() {
  return {
    vendorId: 0x213f, productId: 0x1109, productName: "HID Remapper",
    collections: [{ usagePage: 0xff00, usage: 0x20 }], opened: false,
    async open() { this.opened = true; this.openCount = (this.openCount || 0) + 1; },
    async close() { this.opened = false; this.closeCount = (this.closeCount || 0) + 1; },
    async sendFeatureReport(reportId, data) { this.sentFeatureReport = { reportId, data: new Uint8Array(data) }; },
    addEventListener() {}, removeEventListener() {},
  };
}

(async () => {
  const firstDevice = mockDevice();
  permittedDevice = firstDevice;
  await elements.get("#connect").listeners.get("click")();
  assert.equal(firstDevice.opened, true);
  assert.match(elements.get("#status").textContent, /Listening/);
  assert.match(elements.get("#connection-history").textContent, /Selected 213F:1109/);

  testApi.expireReports();
  await reconnectTick();
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(firstDevice.closeCount, 1);
  assert.equal(firstDevice.openCount, 2);
  assert.match(elements.get("#connection-history").textContent, /Diagnostic reports stale/);

  firstDevice.opened = false;
  permittedDevice = null;
  hidListeners.get("disconnect")({ device: firstDevice });
  assert.match(elements.get("#status").textContent, /Disconnected/);

  const reconnectedDevice = mockDevice();
  permittedDevice = reconnectedDevice;
  await reconnectTick();
  // The timer callback deliberately does not await the WebHID promise.
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(reconnectedDevice.opened, true);
  assert.match(elements.get("#status").textContent, /Listening/);
  assert.match(elements.get("#connection-history").textContent, /WebHID disconnect event/);
  bytes.fill(0);
  bytes.set([0x48, 0x48, 0x44, 0x31, 2, 17, 1]);
  bytes[24] = 1;
  bytes[25] = 5;
  view.setUint16(22, 48644, true);
  testApi.addEvent({ reportId: 0x66, data: new DataView(bytes.buffer) });
  assert.equal(elements.get("#flash-b-side").disabled, true);
  view.setUint16(22, 48980, true);
  testApi.addEvent({ reportId: 0x66, data: new DataView(bytes.buffer) });
  assert.equal(elements.get("#flash-b-side").disabled, false);
  view.setUint16(22, 48332, true);
  testApi.addEvent({ reportId: 0x66, data: new DataView(bytes.buffer) });
  assert.equal(elements.get("#flash-b-side").disabled, false);
  assert.equal(elements.get("#flash-b-side").textContent, "Restore verified running B side");
  await elements.get("#flash-b-side").listeners.get("click")();
  assert.equal(reconnectedDevice.sentFeatureReport.reportId, 100);
  assert.equal(reconnectedDevice.sentFeatureReport.data[0], 18);
  assert.equal(reconnectedDevice.sentFeatureReport.data[1], 14);
  console.log("hid-host-diagnostics permitted-device reconnect: PASS");
})().catch(error => { console.error(error); process.exitCode = 1; });
