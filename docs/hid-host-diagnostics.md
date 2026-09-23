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
| 5 | 1 | event: `0` A-side heartbeat (about once per second), `1` mount, `2` unmount, `3` `tuh_hid_receive_report` arm, `4` `tuh_hid_report_received_cb` invoked (at most once per second), `5` `DEVICE_CONNECTED` received by A, `6` HCD attach, `7` HCD remove, `8` enumeration control-transfer failure, `9` endpoint-open failure, `10` HID instance-capacity exhaustion, `11` A-side parsed Cursor X/Y usage, `12` per-device report counters, `13` A-side physical-hub Cursor counters, `14` A-side mouse output counters, `15` B-side RP2040 HCD snapshot, `16` B diagnostic UART transport heartbeat, `17` B diagnostic runtime identity, `18` compact B-side G700s endpoint health, `19` B flash-loader status preserved across watchdog reboot |
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
| 43..46 | 4 | HHD5: TinyUSB host event-queue drops (`uint32_t`, little-endian); earlier builds report zero |
| 47..62 | 16 | reserved, zero |

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

For the intermittent G700s startup failure, the HHD3 diagnostic build adds
two heartbeat-priority records without changing the normal firmware build:

- Event `17` reports whether A received the `HHD3` marker in B's existing
  `REQUEST_B_INIT` handshake. `confirmed` verifies that this diagnostic B
  runtime, rather than a stale B runtime, started and reached A over UART.
- Event `18` is synthesized by A from a compact 16-byte B-to-A message once
  per second. It selects the lowest-index input interface of `046D:C07C`.
  The viewer shows B-side report callbacks, receive-arm attempts and failures,
  `report_pending`, and `tuh_hid_receive_ready()`. Address zero means B has
  not registered a G700s input interface. The compact message is independent
  of the larger event-12/15/16 transport; if `17` is confirmed but `18` is
  absent, inspect the small-message transport before interpreting the absent
  large diagnostic records.
- Event `19` reports the B-side RAM flash loader or the explicit A-side
  `FLASH_B_SIDE` operation. The viewer identifies the source. Its phase is
  `0` not observed, `1` entered, `2` SWD setup, `3` programming, `4` flash
  readback, or `5` every B image byte verified. The detail field is the SWD
  error code in phases 2/3, or the first differing byte offset in phase 4;
  phase 4 with the high bit set indicates an SWD read error. A reads these
  values from its watchdog scratch registers after the RAM loader reboots it,
  or directly after the A-side command completes.
  The loader's program call pads the final image chunk with erased `0xFF`
  bytes to meet the RP2040 ROM's 256-byte program-length requirement.

For a stall, compare two event-18 snapshots while continuously moving G700s.
If callbacks stop while arms remain ahead and `receive-ready` is false, the
transfer is still claimed/busy and the fault is at or before B's USB host
callback. If `receive-ready` is true but arms do not increase, B failed to
re-arm the endpoint. If `report_pending` stays true, B is waiting for UART
space. These are diagnostic observations, not automatic recovery actions.

### G700s diagnostic B update observed on 2026-09-23

The tested G700s is connected by USB cable to HID-Remapper, not through a
wireless receiver. Its observed VID:PID is `046D:C07C` on physical hub port 2.
Receiver pairing explanations do not apply to these captures.

The combined UF2 and standalone RAM B-loader attempts left event `17` at
`HHD3 handshake NOT seen` and event `19` at `not observed`. This does not by
itself distinguish a loader that did not start from a status that failed to
survive reboot. Repeating those same writes is not evidence of a B update.

For one approved diagnostic attempt, A was temporarily flashed with an A-only
UF2 embedding the 52,932-byte HHD3 B image (`0239C6EC…59E44D`):
`remapper_dual_a-temporary-hhd4-embedded-b-webui-flash.uf2` (SHA-256
`837ACAFC029ED9485D7263BDAA835CE2A25A53B959BAFC46A356BEDCCEE4255A`).
The diagnostic viewer's **Flash embedded B side** button sends configuration
command `14` using Feature Report ID `100`, protocol version `18`, and the
normal 32-byte CRC32 packet. A-side diagnostic event `19` reported
`WebUI B flash: verified`, detail zero, image 52,932 bytes; after power
cycling, event `17` reported `HHD3 handshake confirmed`. This verifies that
the intended B image was read back from B flash and then ran. One subsequent
event-18 sample showed G700s address 2/instance 3, callbacks 1, arms 2,
arm failures 0, pending false, receive-ready false. A single sample does not
establish whether G700s was being moved during a stall.

The temporary A build deliberately changes the normally pinned embedded B
image and enables diagnostics, so it is not a release candidate. The prepared
A-only restoration image
`remapper_dual_a-hhd4-pinned-embedded-b-restore.uf2` (SHA-256
`81ADE93B6D2D8E8A2CC2333BB5FD8EED839E48AF082F5AB6C80281D9411FFF5D`)
returns the embedded B to the verified 48,644-byte payload while retaining
the HHD4 diagnostic reports. This A-only image was flashed after the
reproduction log was captured; the RP2 bootloader disappeared and the
configuration HID enumerated. It does not overwrite the separately flashed B
runtime. The verified combined release UF2 remains unchanged.

The clearest stopped G700s run showed RollerMouse still moving. While the
mouse was moved, the G700s B callback and A UART counts stayed at 46 for at
least seven seconds; G700s had 47 successful receive-arm attempts, zero arm
failures, no UART report pending, and `tuh_hid_receive_ready()` was false.
The RP2040 HCD snapshot showed endpoint slot 5 as `CK-B` (configured,
claimed, not HCD-active, TinyUSB busy), while other live slots were `CKAB`.
Enumeration order makes slot 5 a likely G700s mouse endpoint, but the current
snapshot does not encode its device address, so that identity is an inference.
EPX was inactive and interrupt polling was not suppressed. This points to a
lost or unprocessed transfer completion rather than a global host stop; it
does not yet prove why the completion was lost. In the HHD3 runtime used for
that capture, the TinyUSB host queue had a depth of 16 and a callback was
enqueued on every 1 ms SOF. A full queue could discard the completion event
after HCD clears its active bit while leaving TinyUSB busy; this is a
code-supported hypothesis, not yet an observed queue-overflow count.

The HHD5 diagnostic candidate coalesces queued SOF callbacks to at most one,
increases the host event queue depth to 64, and exposes an event-drop counter
in event `15` bytes 43..46. This does not change USB descriptors, mouse report
formats, or mapping. Its temporary A-only installer embeds a 52,980-byte B
image (SHA-256 `8796A6F38F0216A68132EA8B055A38438610A28B459816E9316BD7BFC2B64461`):
`remapper_dual_a-temporary-hhd5-queue-coalesced-embedded-b.uf2` (SHA-256
`B5538EFD82A77EC0E819856549ECA8BAA02E00EDBA189BDA10CD7B8B6626ACF6`).
The paired A-only restoration image with the pinned 48,644-byte embedded B is
`remapper_dual_a-hhd5-pinned-embedded-b-restore.uf2` (SHA-256
`645A6F66DF4AF160C8F44705C9EFBC162C43733934F223BE9540EBDAC3D935C3`).
These images are diagnostic candidates; success requires the B flash readback,
HHD5 handshake, and repeated G700s/RollerMouse input checks on the device.
The temporary A image was flashed and the explicit A-side B-flash operation
reported readback verification of 52,980 bytes; HHD5 then replied after a
power cycle. With that temporary A, G700s still hesitated for about three
seconds and RollerMouse position jumps returned. The pinned-embedded-B A-only
restoration image was subsequently flashed, and its RP2 bootloader drive
disappeared. After a full power cycle, RollerMouse position jumps disappeared.
Across five more PC-side USB power cycles with all input devices connected,
the user observed the G700s initial hesitation on every cycle, no complete
G700s stop, and no RollerMouse jump. This is a limited five-cycle result; it
does not prove the intermittent stop is eliminated, nor explain the remaining
initial hesitation. The temporary-A jump must not be attributed solely to
the B-side SOF change.

For the remaining hesitation, G700s alone reached smooth motion quickly after
initial enumeration. G700s with only RollerMouse, and G700s with only the
keyboards, each showed a shorter intermittent-motion period. With all devices
attached, the period was roughly three seconds. The custom v5 board's four
USB input ports are its built-in hub
([upstream board description](https://github.com/jfedor2/hid-remapper/blob/master/custom-boards/README.md));
the board and attached devices are
powered from the PC-facing USB-C connection in this setup. These comparisons
are consistent with cumulative enumeration work or startup power loading and
do not distinguish the two. A follow-up hot-plug observation while G700s is
already moving is pending.

Each hot-plugged device then produced a G700s movement pause of just under a
second: first RollerMouse, then each keyboard/keypad. The G700s is wired USB.
This strongly links the startup hesitation to additional-device arrival, but
it still does not isolate USB control-transfer scheduling from USB power
transients. The user later prioritized preventing complete G700s stops and
accepted the short initial hesitation.

For a production-oriented stop-prevention trial, the same SOF coalescing and
64-entry host queue were built with `HID_HOST_DIAGNOSTICS=OFF`. The resulting B
image is 48,980 bytes (SHA-256
`568B7ACF20CD19D5E4454E3EB1B8358F1DBCE34E917F10CB7B96651D29CA67ED`).
The prepared temporary A-only installer is
`remapper_dual_a-temporary-sof-coalesce-production-b-installer.uf2` (SHA-256
`45BFF346B321604C030DC161D301835DC9BB0340CEA14AAB3FEE93E2F2AC2EFC`).
The final A-only restoration image
`remapper_dual_a-verified-stable-only.uf2` (SHA-256
`0D6C9595554DC82B9391B1A7F1AE7F7504B9B783E878BD209053FC511FA61E7C`)
contains all 966 A flash blocks byte-for-byte from the verified combined UF2,
with only the UF2 block count changed to make it independently loadable. The
B image was sent through A's configuration command `14`; the browser showed
the readback-verified status before the next power cycle. The verified A-only
restoration image was then flashed, and the RP2 bootloader drive disappeared.
The normal B build intentionally has no HHD handshake or diagnostic reports.
Physical keyboard, RollerMouse, and repeated G700s-stop checks for this final
pair found a RollerMouse position-jump regression, although G700s and the
keyboard/keypad still worked. The 48,980-byte production B candidate is
rejected. After this A-only restoration, a further **Flash B Side**
command would replace the new B runtime with A's embedded historical B image.

The verified combined baseline has different B images in its two stages:
48,644 bytes embedded in A and 48,332 bytes actually installed by the RAM
B-flash stage. For exact recovery, `extract-runtime-b-hex` in
`tools/verify-firmware-release.py` extracts the 48,332-byte image and checks
SHA-256 `9D77CC7378FCEB10F692338198DB1F4682412A0E48028E81126FBF0C761B8219`.
The temporary A-only installer
`remapper_dual_a-temporary-restore-verified-runtime-b.uf2` (SHA-256
`DED887AD999EAD0F69AFAE06E87F338AE74DDCAA3A838492A3E88408FF8AAF85`)
embeds that exact running image and reports SWD programming/readback status.
It was flashed after the regression. The A-side `FLASH_B_SIDE` operation
reported readback verification of all 48,332 bytes. The verified A-only
restoration image was then flashed; the RP2 bootloader drive disappeared and
the configuration HID enumerated. In the subsequent user check, RollerMouse
movement followed by stopping produced zero position jumps in ten trials.
G700s separately failed to move twice after HID-Remapper connection. This
confirms recovery of the RollerMouse behavior, not resolution of the G700s
startup stall. The G700s stop count is not a RollerMouse jump count.

The queue/SOF experiment is now opt-in through `HOST_QUEUE_CAPACITY_64` and
`HOST_SOF_COALESCE`; both default to OFF so an ordinary source build does not
silently repeat the rejected combination.

### Reproducing the verified B before another trial

The verified running B extracted from the release UF2 is 48,332 bytes,
SHA-256 `9D77CC7378FCEB10F692338198DB1F4682412A0E48028E81126FBF0C761B8219`.
The retained old build has the same B binary. A clean TinyUSB tree made from
its pinned Git revision plus the tracked enumeration, endpoint-capacity,
EPX-race, and HCD-snapshot patches (without the later fairness patch) produced
`usbh.c.obj` and `hcd_rp2040.c.obj` identical to the old build after stripping
debug sections. Thus the host-controller source has been identified, but the
whole B image has not yet been reproduced. Linking that TinyUSB with the
current B application sources and default queue settings produced 48,644
bytes, SHA-256 `AB599FAC142647381FD1E1F256057B3DB0D3E9E05F67A589ED9BAC6726B71B16`.
The retained old B object has a `pending_hid_reports` array; the current B
object has `hid_input_states`, reflecting a changed report-pending design.
This was a material source difference, not merely a build option. The matching
historical source was recovered from Git object
`087d2e4fedb5c783a231596488feac8c4a8faada` and preserved as
`src/remapper_dual_b_verified_20260919.cc`. With `DUAL_B_SOURCE_OVERRIDE`
pointing to that file, the clean TinyUSB patch sequence above, and
`VERIFIED_B_BUILD_DATE=Sep 19 2026`, the B BIN exactly matches the verified
48,332-byte SHA-256. Without the build-date override, the only differing
stripped object is Pico SDK's binary-info date (Sep 23 versus Sep 19).

From that exact reproduction, enabling only `HOST_QUEUE_CAPACITY_64=ON`
(SOF coalescing and one-hot fairness both OFF) produced a 48,332-byte B BIN,
SHA-256 `7DB4B42D476825F6E0296E16B60BE66D64E6558BA312E95A4F152FEE3C206D12`.
After stripping debug sections, 85 of 86 B object files match the verified
build; only TinyUSB `usbh.c.obj` differs. Freshly converting the verified A
ELF yielded the exact A-only UF2 hash
`0D6C9595554DC82B9391B1A7F1AE7F7504B9B783E878BD209053FC511FA61E7C`.
Combining that A with the new `flash_b_side.uf2` produced the trial
candidate `remapper_dual_combined-verified-a-g700-queue64-only-candidate.uf2`,
SHA-256 `8BC16D3D69B0ECD2149AC03A50E4256EB67357439FB081CE4DEF09E0AD6F6DD3`.
`tools/verify-g700-queue-candidate.py` confirms unchanged A payload, the
single expected B runtime payload, CMake options, and UF2 RAM layout. This is
a static gate only. The candidate was flashed after confirming the RP2
bootloader on G:, and Windows enumerated the Remapper again. In the user's
initial hardware report, both mice moved, RollerMouse move-stop caused no
position jump, and G700s had zero complete stops in ten Remapper USB
disconnect/reconnect cycles. Startup hesitation remained but is acceptable
to the user if it does not become a complete stop. This is a ten-cycle result,
not proof that a rare failure has been eliminated. The user confirmed that
`Flash B Side` was not pressed during the trial and reported that ordinary
input appears to work. Individual results for the top-row digits, NumLock
ON/OFF keypad, equals, and JIS keys were not explicitly enumerated; keep the
full release regression gate open. On this evidence the user designated the
flashed queue64-only image the current best stable operational version.
The older verified combined UF2 remains available as a separate rollback.

On 2026-09-24, the second HID-Remapper running the restored queue64-only
combined UF2 was reported to have frequent keyboard and wired G700s stops,
while RollerMouse kept moving without position jumps. During a stop, the
HID-Remapper WebUI Monitor showed neither affected keyboard key presses nor
G700s Cursor X/Y. This localizes the missing input before Monitor's
post-receive decode path; it does not by itself distinguish B-side USB receive,
B-to-A UART delivery, or A-side descriptor/receive handling. The result must
not be described as a Windows-only output or keypad-Mapping failure. No new
diagnostic firmware was flashed for this observation.
At the next joint stop, reconnecting only the keyboard restored keyboard
input but not G700s. On a separate joint stop, reconnecting only the wired
G700s restored G700s but not keyboard input. RollerMouse continued to work.
This is evidence for independently stalled device/interface receive paths,
not a single Windows output freeze or a complete B-side host shutdown.
It does not yet prove whether each path is stuck in TinyUSB/HCD polling or in
per-instance B-to-A forwarding; B-side counters are needed for that boundary.
With RollerMouse removed, the user observed no complete G700s stops during
that trial, but the keyboard gave no key input at all. Its NumLock LED stayed
lit and followed the separate keypad's NumLock changes. This shows an LED
control path to the keyboard, not a functioning keyboard input path.
The user confirmed that the Remapper was power-cycled after RollerMouse
removal, so the dead keyboard is not simply a previously stalled input
endpoint persisting across the trial. The absence of a G700s stop in this
trial is not proof that RollerMouse alone causes the stop. It also does not
prove host-queue saturation: fewer connected devices did not restore the
keyboard input.
In that clean, RollerMouse-free state, unplugging and reconnecting only the
keyboard restored its key input. G700s and the keypad remained connected.
The keyboard input path can therefore be established by a fresh per-device
mount, while LED output alone is insufficient evidence for a live interrupt
IN endpoint. The next diagnostic must identify whether the original mount
failed to open/arm that endpoint, or whether an armed transfer became stuck.
After also removing G700s, keyboard input failed on the first attempt, but
subsequent HID-Remapper USB-C power cycles with keyboard and keypad connected
made the keyboard work every time reported. This contrasts with the repeated
keyboard failure with G700s present and RollerMouse absent. The first failed
attempt prevents claiming G700s is strictly necessary; a controlled G700s
hot-plug into an already working keyboard is the next no-firmware test.
In that test, adding G700s after the keyboard and keypad were already working
did not stop the keyboard. Thus G700s presence and ordinary steady-state
traffic alone did not reproduce the keyboard failure in this sequence.
Cold-start enumeration/receive-arm order remains a stronger hypothesis than
an immediate G700s hot-plug effect, but requires a matching all-device
hot-plug comparison before treating the startup condition as decisive.
The user then hot-plugged RollerMouse last, without rebooting the Remapper.
Keyboard, G700s, and RollerMouse all continued to work during the observation.
Together with the earlier all-attached-at-boot failures on this second unit,
this makes concurrent startup enumeration/initial receive-arm scheduling the
leading investigation target. It is not proof of a particular queue overflow,
HCD slot fault, or device-specific root cause. Further repeated unplug tests
are lower value than capturing B-side mount, receive-arm, callback, endpoint
busy/active, and host-event-queue-drop state during a failed cold start.

### Temporary startup input-endpoint probe (2026-09-24, static gate passed)

The second Remapper is the only target. The candidate
`firmware/artifacts/remapper_dual_combined-startup-input-endpoint-diagnostics-test.uf2`
has SHA-256 `1593A9D71EC24B1D502BBA28845C314AA38DE3E1D99734053E5A8E2446A67E9C`.
It uses the verified queue64 B source and the same preserved TinyUSB source.
With diagnostics OFF, a fresh B rebuild is exactly the 48,332-byte verified
runtime (`7DB4B42D476825F6E0296E16B60BE66D64E6558BA312E95A4F152FEE3C206D12`).
The diagnostic build retains queue64, disables SOF coalescing and one-hot
fairness, and adds event 20 once per second for the keyboard `0853:0142` and
wired G700s `046D:C07C`. It reports registration, callback and arm counts,
arm failures, UART report pending/receive-ready flags, and host-event queue
drops. It does not change the normal B receive/re-arm logic, descriptors, or
mouse report processing. A temporary diagnostic A forwards event 20 via the
configuration HID. `tools/verify-startup-input-diagnostics.py` verifies the
fresh A/B build inputs, embedded B, RAM loader, UF2 structure, and fixed
diagnostic-OFF baseline. `tools/hid-host-diagnostics-test.js` covers the UI
event-20 parser. These are static checks only; a failed or successful hardware
startup must still be observed. Restore the untouched best-stable combined
UF2 after diagnosis. Do not press WebUI `Flash B Side` during this test.
The UF2 was written to the second unit after confirming `G:\INFO_UF2.TXT`
identified the RP2 bootloader and repeating the static gate. The G drive
disappeared and Windows enumerated `USB\VID_CAFE&PID_BAF2`; this proves
bootloader exit and A-side USB enumeration, not yet that the diagnostic B
runtime is running or that any input works. The local diagnostic page at
`http://localhost:8000/hid-host-diagnostics.html` returned HTTP 200 with
the new event-20 display fields. Await B keyboard/G700s probe lines before
claiming successful diagnostic deployment.
The first event-20 readback confirmed that diagnostic B is running: keyboard
`0853:0142` at address 1/instance 0 and G700s `046D:C07C` at address
2/instance 3 were both registered. Each had `arms=1`, `arm failures=0`,
`callbacks=0`, `pending=false`, and `receive-ready=false`; the shared host
queue-drop counter was 1980. In the next sample at 1:32:41, keyboard
callbacks/arms had risen to 117/118 and G700s to 4105/4106 with zero arm
failures; queue drops remained 1980. The initial zero callbacks were not
evidence of a stuck transfer in this run. The high drop total was accumulated
earlier, apparently during startup, but input callbacks progressed afterward.
Total drops alone do not identify whether SOF or transfer-completion events
were lost, and this successful B-side activity is not a failed-start capture.
After a later USB-C reconnect, the viewer still said `Listening` but its
last heartbeat and event-20 lines remained at 1:34:07. Windows still listed
`USB\VID_CAFE&PID_BAF2` as connected; therefore the pasted counters were
stale and cannot describe the new startup. The diagnostic HTML was updated
to reopen a permitted WebHID device after five seconds without a new
diagnostic report, and to clear previous-session counters on each open.
The local JavaScript reconnect/parser test passes. The user must reload the
page to receive this client-only fix; no firmware rewrite is needed.
The user confirmed that, after refreshing the page, the heartbeat and
endpoint-health timestamps eventually advanced again after a USB reconnect.
Auto-reconnect is therefore usable for a fresh failed-start capture, though
it may take a little time; wait for new timestamps before interpreting data.
In a fresh failed start at 1:42:47, the keyboard did not respond while
RollerMouse still moved. Event 20 showed keyboard address 1/instance 0 with
callbacks 1, arms 2, arm failures 0, UART pending false, receive-ready false.
G700s address 2/instance 3 had callbacks 6685 and arms 6686; the shared host
queue-drop total was 1982. Thus the host was not globally stopped and the
keyboard had been successfully re-armed after its first callback, but its
next callback did not arrive during the reported key presses. The stuck
boundary is the keyboard's interrupt-IN transfer or its completion delivery,
not the keyboard mapping or a B-to-A UART report waiting to flush. The queue
drop total supports an overflow possibility but does not identify the event
type or prove that this keyboard completion was one of the dropped events.

### Queue-drop type diagnostic candidate (prepared, not flashed)

The first failure confirmed a keyboard-specific armed/busy stall, but the
1982 total host queue drops do not say whether a transfer-completion event
or a deferred SOF/function event was discarded. A separate candidate
`firmware/artifacts/remapper_dual_combined-startup-input-drop-types-diagnostics-test.uf2`
has SHA-256 `39258D06F72C5E33C0EAFF4DB133C48D0ADA43F533AA224C9DE6C59A2C68ED81`.
It adds diagnostic-only counts for dropped `HCD_EVENT_XFER_COMPLETE` and
`USBH_EVENT_FUNC_CALL` events to event 20. The B receive/re-arm logic,
queue64 capacity, SOF coalescing OFF, one-hot fairness OFF, descriptors, and
mouse report handling remain unchanged. Its diagnostic-OFF B rebuild remains
byte-identical to the verified queue64 B runtime. The specialized static
gate and the browser parser test both pass. It has not been written to a
device; do not interpret its new fields as measured until a fresh boot with
this candidate produces new event-20 reports.
The user explicitly confirmed the current RP2 G: drive was the second test
unit. After rechecking `G:\INFO_UF2.TXT`, the fixed candidate hash, and the
specialized static gate, the new combined UF2 was copied to G:. The drive
disappeared and Windows enumerated `USB\VID_CAFE&PID_BAF2`. This confirms
bootloader exit and A-side enumeration only; await fresh event-20 lines with
both `transfer-completion drops` and `deferred-function drops` before using
the new counters. Do not press WebUI `Flash B Side`.
The first failed-keyboard boot with this version reported keyboard callbacks
1/arms 2/arm failures 0/pending false/receive-ready false, while G700s
callbacks/arms were 1680/1681 and RollerMouse still moved. The shared host
queue had 1983 drops: 1982 deferred-function events and one
`HCD_EVENT_XFER_COMPLETE`. This is a strong match for a lost completion
leaving one device's TinyUSB endpoint busy after HCD activity, but the drop
record does not include device/endpoint identity, so assigning that one
completion specifically to the keyboard remains an inference. A candidate
mitigation should reserve room for completion events by omitting SOF defer
events only when the 64-entry queue nears capacity; normal-load SOF behavior
must remain unchanged to protect RollerMouse motion.

### Completion-event queue reserve trial (prepared, not flashed)

`firmware/artifacts/remapper_dual_combined-completion-queue-reserve-diagnostics-test.uf2`
has SHA-256 `B37C32CEB5C9D5033064F60BC3AF87DFB51FA99FFCB0216F20B7E5312E66A23B`.
The only host-behavior change is gated by `HOST_COMPLETION_QUEUE_RESERVE=ON`:
when fewer than 17 of the 64 host-event slots remain, the RP2040 HCD skips
deferring that frame's SOF callback, preserving at least 16 slots for
transfer completions and attach/remove events. At lower queue occupancy,
SOF behavior is unchanged. No descriptor, mouse report, mapping, or HID
receive/re-arm code changes are part of this candidate. Diagnostic event 20
additionally shows `SOF deferred by reserve` alongside dropped transfer
completions and dropped deferred functions. An independently rebuilt
reserve-OFF/diagnostics-OFF B BIN is byte-identical to the verified 48,332-byte
queue64 B (`7DB4B42D476825F6E0296E16B60BE66D64E6558BA312E95A4F152FEE3C206D12`).
The specialized UF2 gate and diagnostic-page parser tests pass. Static checks
do not establish that keyboard/G700s stops are fixed or that RollerMouse
remains jump-free; both must be checked on the second test unit before any
production build or stable designation.
The user explicitly confirmed that the newly present G: RP2 bootloader was
the second test unit and authorized this behavior-changing diagnostic trial.
After rechecking the UF2 hash and static gate, the candidate was written.
The G drive disappeared and Windows enumerated `USB\VID_CAFE&PID_BAF2`.
Neither the diagnostic B runtime nor keyboard/G700s/RollerMouse behavior is
yet confirmed after this write. The untouched best-stable UF2 remains the
rollback. WebUI `Flash B Side` must remain unused.
The first fresh event-20 readback at 2:05:33 showed keyboard callbacks/arms
32/33 and G700s 3181/3182, with zero arm failures and zero host queue,
transfer-completion, and deferred-function drops. `SOF deferred by reserve`
was 2060, proving the new guard was exercised under startup load rather
than merely compiling in. This is a positive queue-integrity observation
for one boot, not yet a pass for Windows input, repeated cold starts, or
RollerMouse move-stop behavior.
The user subsequently confirmed that keyboard and G700s worked in Windows
and RollerMouse move-stop did not jump in this boot. With all four devices
connected, ten further HID-Remapper USB-C restart cycles were reported normal:
keyboard and G700s moved/typed, and RollerMouse move-stop did not jump.
This is 10/10 for the diagnostic candidate, not proof that rare failures are
impossible or that the unflashed production-form candidate has passed.

An unflashed production-form candidate has also been prepared at
`firmware/artifacts/remapper_dual_combined-best-stable-completion-queue-reserve-production-test.uf2`
(SHA-256 `9383B54B97038BD8E66DCF114A6E895FA2FB6F47CBAEA5FB5CD1BEFF08B91231`).
Its A flash payload is byte-identical to the current best stable A, regenerated
from a verified A ELF. Its B uses queue64 and completion reserve ON with all
diagnostics, SOF coalescing, and one-hot fairness OFF. The reserve-OFF B rebuild
is still byte-identical to the verified queue64 runtime. The generated
`flash_b_side` RAM loader and embedded production B runtime passed
`tools/verify-completion-queue-reserve-candidate.py`. This artifact is not a
stable release. The diagnostic candidate's ten-cycle test has now been
reported as normal; the production-form candidate still requires its own
second-unit hardware trial before any stable designation or first-unit flash.
The user connected the second test unit in RP2 boot mode to advance to this
production-form trial. After rechecking its `G:\INFO_UF2.TXT`, the fixed
candidate hash, and the specialized static gate, the UF2 was written. The
G drive disappeared and Windows enumerated `USB\VID_CAFE&PID_BAF2`.
Keyboard/G700s input and RollerMouse move-stop after this production-form
write are not yet confirmed. This A is the same as the best stable A, so the
old keypad-versus-Mapping behavior is unchanged by this B-only trial.
Do not press WebUI `Flash B Side`, which would overwrite the running B with
the older B image embedded in A.
The exact verified TinyUSB source currently lives in the ignored
`build-g700-clean-tinyusb-src` directory. The static baseline hash proves
what was built for this trial, but before committing or releasing the new
behavior, preserve its source change as a tracked patch or pinned source
asset so the candidate can be rebuilt without relying on ignored files.

The user then reported ten USB-C restart cycles with all four devices
connected: keyboard input worked on every cycle, wired G700s moved on every
cycle, and RollerMouse move-stop had no position jump. This is 10/10 for the
production-form queue-reserve candidate, not a guarantee against rarer
stalls. The keypad/NumLock/equals/JIS regression checks and WBT2 path are
still separate and have not been reported for this trial. Keep the original
best-stable UF2 intact and do not flash the first unit yet.
The user then supplied `C:\Users\ruin_\Downloads\hid-remapper-config.json`
(SHA-256 `096E477D00376F1D1DEA9DD3710E06EB264D0DFDF0FD2244414B2932F94CF1D9`).
It has 35 mappings and differs from the saved 45-mapping complete JSON only
by removing the ten keypad-digit-to-top-row-number mappings. On the second
unit with DIP3 ON and this native-keypad mapping, the user initially reported
PC-direct keypad digits and all symbols entering normally, and NumLock OFF
producing arrows, Home, PgUp and PgDn normally. The user then noticed "two
entries each"; it is not yet clear whether two characters were actually
inserted or whether this refers to expected Raw Input DOWN/UP records.
Therefore keypad duplicate-input status is open, not passed. The specific
JIS `0x87`/`0x89` keys and WBT2-V4 path remain separately unreported.
The user clarified that actual keypad double input occurred. The production
B queue-reserve trial therefore passes the reported mouse/keyboard startup
checks but FAILS the keypad single-entry requirement with the 35-mapping
native-keypad JSON. Its A flash is byte-identical to the old best-stable A
and still has the known path that can insert a keypad usage already present
via pass-through. A separate A-side keypad deduplication change is required;
the user specified that only the digit keys double, while `/ * = - + . Enter`
are each entered once. Thus the fix stays limited to keypad-digit reinsertion;
the prior A-dedup trial fixed the duplicate but ran with the old B host and
did not satisfy overall device stability. A combined A-dedup + queue-reserve
B candidate is the next controlled test, not a stable release yet.
The combined candidate is now prepared but unflashed:
`firmware/artifacts/remapper_dual_combined-native-keypad-dedup-completion-reserve-test.uf2`
(SHA-256 `72CCE415BFCBF8E5023AA40CB307DCDC3A552C511F6AF3F9EC90CB0AE98A224F`).
A is byte-identical to the earlier keypad-dedup A component, freshly built
with `NATIVE_KEYPAD_DEDUP=ON`; B is byte-identical to the current
completion-queue-reserve production B RAM stage. Both diagnostics remain OFF.
`tools/verify-native-keypad-reserve-candidate.py` checks the exact two stages,
fresh A BIN, pinned embedded B, and build options. The static gate passed;
real keypad single-entry, NumLock-OFF navigation, operator/equals/JIS input,
keyboard/G700s cold starts and RollerMouse move-stop all remain to be tested
on the second unit. Do not flash the first unit or press `Flash B Side`.
The user placed the second test unit in RP2 boot mode. After confirming
`G:\INFO_UF2.TXT`, candidate SHA-256 and the two-stage static gate, this
combined trial was written. The G drive disappeared and Windows enumerated
`USB\VID_CAFE&PID_BAF2`. These checks establish firmware boot, not keypad
single-entry or mouse/keyboard stability. Hardware results are pending.
The user then confirmed PC-direct, DIP3 ON, native 35-mapping behavior:
keypad 1 and 0 each inserted one character, `/ * = - + . Enter` each worked
once, and NumLock OFF produced arrows, Home, PgUp and PgDn correctly. This
passes the reported keypad single-entry and navigation checks for the
combined candidate. The four-device cold-start and RollerMouse tests were
pending at that point; JIS `0x87`/`0x89` and WBT2-V4 were also unreported.
The user then tested the same second Remapper directly connected to the PC
with all four HID devices attached. Across ten USB-C disconnect/reconnect
cycles, RealForce ordinary keys and wired G700s worked each time, and
RollerMouse move-stop never produced a position jump. This is a 10/10
PC-direct result for the combined A-dedup + B queue-reserve candidate, not
proof against rarer failures. WBT2-V4 transport and individual JIS
`0x87`/`0x89` keys remain to be checked before considering wider deployment.
The user later confirmed the PC-direct JIS `0x87`/`0x89` keys work. An initial
WBT2-V4 response saying "none occur" is ambiguous between no observed
failures and no key input, so wireless-path acceptance was left open until
positive keypad output was explicitly confirmed.
The user clarified that all listed WBT2-V4 functions were normal: keypad
digits/operators, NumLock-OFF navigation, keyboard and G700s input, and no
RollerMouse move-stop jump in the initial connection. This supersedes the
ambiguity above. Repeated WBT2 reconnections and rapid keypad/operator
sequences remain untested.
The user then reported that all ten WBT2-V4-path USB-C reconnection trials
with four devices were stable, and repeated keypad digits plus `/ * = - + . Enter`
did not trigger NumLock lock-up, double input, device stops, or RollerMouse
position jumps. This complements the earlier PC-direct 10/10 result. It is
the broadest second-unit hardware validation of the combined A-dedup + B
queue-reserve candidate so far, not proof against rarer failures or permission
to overwrite the previous best-stable UF2. The candidate's production source
still needs a tracked/pinned TinyUSB patch before reproducible release.

A separate run had G700s B/A counts and nonzero A-side mouse output counts
rising while the user reported no cursor movement. That observation requires
its own Windows-side check and must not be merged with the seven-second
B-side stall above.

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
