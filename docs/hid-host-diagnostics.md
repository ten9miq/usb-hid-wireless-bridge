# USB HID host diagnostics

`HID_HOST_DIAGNOSTICS=ON` is a diagnostic-only CMake option for the dual A/B
firmware.  It leaves the normal build and its USB descriptor unchanged.  The
enabled build emits B-side USB host events and an A-side heartbeat through
A-side interface 3, the existing vendor-defined configuration interface
(`0x84`).  It never uses the Boot Keyboard, Boot Mouse, or Consumer input
interfaces.

## Build

From `firmware/hid-remapper/firmware`, configure a separate directory:

```powershell
cmake -G Ninja -S . -B build-hid-host-diagnostics -DPICO_BOARD=remapper_v7 -DHID_HOST_DIAGNOSTICS=ON -DPICO_NO_PICOTOOL=OFF
cmake --build build-hid-host-diagnostics --target remapper_dual_combined
```

Both `remapper_dual_a` and the B binary embedded into it receive the same
compile definition. `PICO_NO_PICOTOOL=OFF` lets the Pico SDK use an installed
or source-built `picotool` to create the UF2 files. Do not combine a diagnostic
A image with a normal B image.

## PC-side report

The enabled descriptor adds only vendor-defined Input Report ID `102` (`0x66`)
to the configuration interface.  Existing Monitor packets remain ID `101`; a
diagnostic packet is intentionally a separate report, so it cannot be decoded
as a keyboard, mouse, consumer-control, or Monitor usage update.

Use a WebHID or HID capture client that opens the HID Remapper configuration
interface and filters `inputreport` events to report ID `102`.  A minimal
WebHID viewer is [`tools/hid-host-diagnostics.html`](../tools/hid-host-diagnostics.html);
serve it from `localhost` or HTTPS, then select the configuration HID device.
The data payload is always 63 bytes. Ignore packets unless bytes `0..3` are
ASCII `HHD1`. Version `2` adds descriptor fragments to event `5`; the bundled
viewer also accepts the earlier version `1` packets.

| Payload offset | Size | Meaning |
|---:|---:|---|
| 5 | 1 | event: `0` A-side heartbeat (about once per second), `1` mount, `2` unmount, `3` `tuh_hid_receive_report` arm, `4` `tuh_hid_report_received_cb` invoked (at most once per second), `5` `DEVICE_CONNECTED` received by A, `6` HCD attach, `7` HCD remove, `8` enumeration control-transfer failure, `9` endpoint-open failure, `10` HID instance-capacity exhaustion, `11` A-side parsed Cursor X/Y usage, `12` per-device report counters, `13` A-side physical-hub Cursor counters, `14` A-side mouse output counters, `15` B-side RP2040 HCD snapshot, `16` B diagnostic UART transport heartbeat |
| 6 | 1 | flags: bit 0 arm/callback success, bit 1 descriptor has Input, bit 2 interface descriptor info was obtained |
| 7, 8 | 1 each | TinyUSB `dev_addr`, HID `instance` |
| 9..14 | 1 each | `bInterfaceNumber`, class, subclass, protocol, endpoint count, hub address |
| 15 | 1 | hub port |
| 16..17, 18..19 | 2 LE each | VID, PID |
| 20..21, 22..23 | 2 LE each | report-descriptor length, received report length (`4` records the callback length) |
| 24 | 1 | raw-byte length: event `4` records `min(received report length, 8)`; other events use zero |
| 25..32 | 8 | first report bytes for A-side event `4`; event `11` stores little-endian `uint32_t` usage then `int32_t` parsed value; unused bytes are zero |
| 33 | 1 | TinyUSB enumeration stage for event `8` |
| 34 | 1 | TinyUSB `xfer_result_t` for event `8` |
| 35 | 1 | endpoint address for event `9` |
| 36..37 | 2 LE | endpoint packet size for event `9` |
| 38..39 | 2 LE | report-descriptor fragment offset for event `5` |
| 40 | 1 | descriptor fragment byte length for event `5` (at most 22) |
| 41..62 | 22 | raw report-descriptor fragment for event `5`; unused bytes are zero |

The A side sends a heartbeat about once per second after its configuration HID
interface is ready, even when B emits no events. On a normal input-capable
interface, expect a mount event followed by an arm event with success bit 0
set. A mount with bit 1 clear documents why no report receive request was
attempted. A failed arm is reported with bit 0 clear. Event `4` is emitted by
A when a normal `REPORT_RECEIVED` transport record arrives from B. It includes
the received report length and the first up to eight raw report bytes, plus the
cached `DEVICE_CONNECTED` VID/PID and interface context when available. This
confirms both B-side reception and A/B transport even if B-side mount
diagnostics are missing. Event `5` is emitted by A when the
normal B-side `descriptor_received_callback` transport record arrives. It
includes the device address, HID instance, callback interface number, VID/PID,
report descriptor length, and hub port without relying on B-side mount
diagnostics. Version `2` emits one event `5` per 22-byte descriptor fragment,
with a byte offset in each packet; the viewer reassembles and prints the full
descriptor after the final fragment. Thus a 67-byte G700s descriptor is
available as four ordered event `5` packets (22 + 22 + 22 + 1 bytes).
Event `6` is the earliest point at which TinyUSB sees a new connection. Its
device address is deliberately `0`, because address allocation happens later;
use its hub address and port to identify the downstream physical port. Events
`8`--`10` are emitted before HID mount and are the useful distinction when a
second device never produces a mount callback: `8` gives the failed control
request and enumeration stage, `9` records the endpoint whose HCD allocation
failed, and `10` reports exhaustion of `CFG_TUH_HID` with the interface number
and configured capacity. These hooks exist only in the diagnostic build.
The queue and B-to-A forwarding
are nonblocking. B-side diagnostic events retain their complete VID/PID and
interface context in a small pending queue until a nonblocking UART write
succeeds; normal HID report forwarding is serviced first. Diagnostic packets
can still be dropped if that pending queue fills under sustained traffic and
must not be used to infer input loss.

Event `11` is emitted by A immediately after `monitor_usage()` for a nonzero
relative Cursor X (`0x00010030`) or Cursor Y (`0x00010031`) value. It is
limited independently for X and Y to four records per second so diagnostics
cannot disturb host input. Its address/instance and hub fields identify the
source; moving the G700s connected to hub port 2 must produce event `11` rows
with hub `0:2` and those two usages even while the RollerMouse is active.

Event `12` is emitted for up to eight active address/instance pairs per
second when B-side diagnostics are available. In addition, the A side sends
one snapshot for its first active context immediately after each heartbeat;
this heartbeat-priority packet keeps the counter table observable while the
ordinary diagnostic FIFO is busy. It preserves the normal diagnostic
descriptor and event layout while using the event-specific bytes as two
independent snapshots: bytes `25..28`
and `29..32` are B-side callback count and last-report age in milliseconds;
bytes `41..44` and `45..48` are A-side UART-receive count and age. Byte
offsets `22..23` and `36..37` carry the respective B/A last report lengths.
`0xffffffff` age means that side has not received a report. The bundled
viewer renders these values as the per-device counters table, so a B-side
stall and an A/B transport stall are distinguishable without changing the
normal-build payload or descriptor. On an event `2` unmount, the viewer
removes that address/instance row. The A side likewise clears its cached
`DEVICE_CONNECTED` context and both counter states when it receives the
ordinary `DEVICE_DISCONNECTED` transport command, so disconnected interfaces
do not reappear in later heartbeat-priority event `12` snapshots. These
cleanup paths are compiled only with `HID_HOST_DIAGNOSTICS=ON`.

Event `13` is a round-robin A-side snapshot for every observed physical hub
port. Seven little-endian `uint32_t` values span bytes `25..32` and `41..60`:
nonzero parsed X/Y counts, `read_input()` reach counts for X/Y, the last
signed parsed X/Y values, and received-report count. Event `14` uses the same
seven-word layout for process-wide output counters: mapping-input nonzero
frames X/Y, nonzero X/Y accumulator additions, then mouse reports built,
newly accepted by the output queue, and accepted by USB. Both use only the
diagnostic report and are emitted after the heartbeat-priority event `12`, so
normal firmware builds and descriptors are unchanged.

Event `15` is the latest B-side RP2040 HCD snapshot and is sent immediately
after each A-side heartbeat, ahead of events `12`--`14`. It has its own fixed
layout (all multi-byte values are little-endian):

| Payload offset | Size | Meaning |
|---:|---:|---|
| 5 | 1 | event `15` |
| 6 | 1 | flags; bit 0 means snapshot valid |
| 7 | 1 | event-15 format version (`1`) |
| 8 | 1 | interrupt endpoint slot count (currently 15) |
| 9 | 1 | `int_ep_suppressed` |
| 10 | 1 | EPX state: bit 0 configured, bit 1 active, bit 2 pending |
| 11 | 1 | active HID instance count |
| 13..16 | 4 | EPX control receive-timeout count |
| 17..20 | 4 | enumeration control-failure count |
| 21..24 | 4 | endpoint-open failure count |
| 25..28 | 4 | `saved_int_ep_ctrl` |
| 29..32 | 4 | current hardware `int_ep_ctrl` |
| 33..34 | 2 | current interrupt-endpoint enable mask, slots 1..15 in bits 0..14 |
| 35..36 | 2 | configured-slot mask |
| 37..38 | 2 | TinyUSB claimed-slot mask |
| 39..40 | 2 | HCD active-transfer mask |
| 41..42 | 2 | TinyUSB busy-transfer mask |
| 43..62 | 20 | reserved, zero |

The WebHID viewer renders each endpoint slot as `C/K/A/B` for configured,
claimed, HCD active, and TinyUSB busy. A dash means the corresponding bit is
clear. The `Capture HCD baseline` button freezes one sample while `Latest`
continues to update.

Event `16` is generated by B about once per second and retained by A as a
heartbeat-priority snapshot, so the WebHID viewer displays it independently of
the ordinary diagnostic-event FIFO. Its fixed event-specific payload uses six
little-endian `uint32_t` values; event `15` and `12` retain their existing
payloads unchanged:

| Payload offset | Size | Meaning |
|---:|---:|---|
| 25..28 | 4 | B-side event-15 records generated |
| 29..32 | 4 | B-side diagnostic `serial_write_nonblocking()` successes |
| 41..44 | 4 | B-side diagnostic `serial_write_nonblocking()` failures |
| 45..48 | 4 | pending state: bits 0..7 ordinary diagnostic FIFO depth, bits 8..15 event-12 priority FIFO depth, bit 16 event-15 retry pending, bit 17 event-16 retry pending |
| 49..52 | 4 | B-side event-12 records generated and retained in its priority FIFO |
| 53..56 | 4 | B-side event-12 records successfully written to the A/B UART |

The serial counters cover B-side diagnostic records only, including retries;
the event-16 packet reports the values immediately before its own UART write.
Therefore, a rising event-15-generated count with no event-15 WebHID update
but a continuing event-16 heartbeat isolates the delay after B generated its
snapshot. A rising failure count or nonzero pending fields identifies B-to-A
diagnostic UART backpressure. A non-rising event-15-generated count with a
continuing event-16 heartbeat instead shows that the event-15 producer is not
running. This diagnostic state does not prove mouse movement or normal HID
report delivery.

## Two-mouse connection-order capture

Run both orders separately from a power-cycled diagnostic firmware state:

1. Open the viewer, connect the configuration HID, and wait for a heartbeat
   followed by an event-15 `Latest` row.
2. Connect only the first mouse: G700s for run A, RollerMouse for run B. Wait
   until the HID instance count and endpoint masks remain unchanged for two
   heartbeats, then select **Capture HCD baseline**.
3. Connect the second mouse: RollerMouse for run A, G700s for run B. Do not
   reconnect the viewer. Wait for two more heartbeats and record both the
   frozen `Baseline` and updating `Latest` rows, plus events `8`--`10` and the
   event-12 per-device counter table.
4. Compare `int_ep_suppressed`, saved/current `int_ep_ctrl`, EPX active or
   pending state, all five slot masks, and the three cumulative failure
   counters. A counter delta proves that failure occurred during the second
   insertion; a stuck suppression/EPX state or mismatched claimed/busy/active
   masks identifies the layer at which progress stopped.

This capture diagnoses state but does not itself prove mouse movement or
hot-plug behavior; those remain device-side interactive checks.

No device flashing or interactive HID test is performed by the build.
