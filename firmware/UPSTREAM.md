# HID Remapper upstream pin

- Repository: <https://github.com/jfedor2/hid-remapper>
- Release: `r2026-05-25`
- Commit: `722ea0534e2e3f9e4a584ee688ac236f03424cf9`
- Imported path: `firmware/hid-remapper`

`firmware/hid-remapper` is a vendored source snapshot. It is not a nested Git
repository or submodule. The first simple-report WBT2-V4 A/B change is also
recorded in `patches/wbt2-v4-simple-reports.patch` so that stage can be applied
to a clean copy of the pinned upstream commit. The later Boot-interface stage
is maintained in the vendored source and described below.

RP2040 HCD snapshot diagnostics are recorded separately in
`patches/tinyusb-rp2040-hcd-snapshot-diagnostics.patch`. Apply it after
`tinyusb-host-enumeration-diagnostics.patch` and
`tinyusb-rp2040-epx-interrupt-race.patch`; it depends on both diagnostic hooks
and the EPX interrupt-suppression state introduced by those patches.

RP2040 asynchronous interrupt-endpoint polling fairness is recorded in
`patches/tinyusb-rp2040-interrupt-polling-fairness.patch`. Apply the RP2040
patches in this order: `tinyusb-rp2040-hid-endpoint-capacity.patch`,
`tinyusb-rp2040-epx-interrupt-race.patch`,
`tinyusb-rp2040-hcd-snapshot-diagnostics.patch`, then this fairness patch.
The snapshot patch still requires `tinyusb-host-enumeration-diagnostics.patch`
before the RP2040 sequence.

## A/B scope

- Keep the existing two-interface USB configuration unchanged: the remapped
  HID output remains interface 0 and the configuration/monitor channel remains
  interface 1.
- Change the default keyboard input report from an NKRO bitmap to an
  8-byte 6KRO array report (modifier, reserved, six key usages). Its Usage and
  Logical ranges both run from `0x04` through `0x91`, so each array byte is the
  actual HID usage ID. This includes keypad usages `0x59` through `0x63` and
  retains the Japanese/international keys exposed by the original descriptor;
  zero-filled unused slots are explicitly declared as null state.
- Change the default relative mouse input report to three buttons plus signed
  8-bit X, Y, and wheel fields.
- Keep report IDs because keyboard, mouse, consumer control, and LED reports
  still share interface 0. An ID-less mouse cannot coexist with those report-ID
  reports on the same HID interface.
- Retry an input report when TinyUSB reports the interrupt endpoint busy;
  otherwise queued relative mouse movement can be discarded without having
  reached USB.

The absolute-mouse and gamepad descriptors are intentionally unchanged.

## Boot-interface stage

After the simple-report A/B still failed for the keypad and mouse, the
`remapper_dual_a` target was changed to expose descriptor 0 as four HID
interfaces: ID-less Boot Keyboard, ID-less Boot Mouse, ID-less Consumer
Control, and the existing configuration/monitor channel.  Internal report IDs
remain unchanged and are translated to interfaces only at the USB send path,
so persisted mappings and the configuration format remain compatible.

After the three-byte Boot Mouse version passed movement and button testing, its
ID-less report was extended to four bytes by appending the wheel field.  The
Boot Keyboard descriptor uses the conventional `0x00` through `0x65` usage
and logical ranges; keypad usages `0x59` through `0x63` remain valid array
values.  Other output descriptor selections keep their original two-interface
configuration.

## Input-host protocol

TinyUSB host defaults Boot-subclass HID interfaces to Boot protocol during
enumeration.  The `remapper_dual_b` target now selects `HID_PROTOCOL_REPORT`
before USB host initialization so every device is consumed according to its
full report descriptor.  This keeps multi-interface and multi-collection
devices generic and avoids a receiver- or keyboard-specific VID/PID quirk.

The split Boot Keyboard interface caches the last five-bit LED output state,
forwards a normalized one-byte value into the existing usage-based output
mapping, and returns the same value for an Output GET_REPORT request.  This
keeps Num Lock and the other standard LED usages available across physical
keyboard enumeration without adding device-specific handling.

Some keyboards also advertise an NKRO bitmap as a one-bit Array input even
though they send one independent bit per usage.  The descriptor parser treats
that field as Variable only when the usage-range cardinality exactly matches
the report count (and is greater than two), with logical range 0 through 1.
This is the generic structural equivalent of the Linux Topre Array-to-Variable
report-descriptor fix and does not depend on a VID/PID table.

Some keypads emit a short Num Lock tap before a keypad press and another short
tap after its release to maintain their internal lock state.  The WBT2 input
path recognizes the complete usage-state sequence, suppresses only the two
wrapper taps, and leaves the keypad usage unchanged.  An incomplete sequence
replays the withheld Num Lock down/up pair on consecutive mapping frames, and
a held Num Lock is passed through with its matching release.  Recognition is
based on report usages and timing rather than a VID/PID table.
