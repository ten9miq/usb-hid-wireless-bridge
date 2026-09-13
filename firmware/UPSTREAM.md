# HID Remapper upstream pin

- Repository: <https://github.com/jfedor2/hid-remapper>
- Release: `r2026-05-25`
- Commit: `722ea0534e2e3f9e4a584ee688ac236f03424cf9`
- Imported path: `firmware/hid-remapper`

`firmware/hid-remapper` is a vendored source snapshot. It is not a nested Git
repository or submodule. The local WBT2-V4 A/B changes are also recorded in
`patches/wbt2-v4-simple-reports.patch` so they can be applied to a clean copy of
the pinned upstream commit.

## A/B scope

- Keep the existing two-interface USB configuration unchanged: the remapped
  HID output remains interface 0 and the configuration/monitor channel remains
  interface 1.
- Change the default keyboard input report from an NKRO bitmap to an
  8-byte 6KRO array report (modifier, reserved, six key usages). The array
  accepts usages through `0x91`, retaining the Japanese/international keys
  exposed by the original descriptor.
- Change the default relative mouse input report to three buttons plus signed
  8-bit X, Y, and wheel fields.
- Keep report IDs because keyboard, mouse, consumer control, and LED reports
  still share interface 0. An ID-less mouse cannot coexist with those report-ID
  reports on the same HID interface.

The absolute-mouse and gamepad descriptors are intentionally unchanged.
