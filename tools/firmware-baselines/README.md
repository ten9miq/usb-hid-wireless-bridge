# Historical A-side build inputs

These two sources reproduce the A application/flash helper used for the
verified native-keypad-dedup plus completion-reserve firmware. They are the
exact inputs from the successful 2026-10-11 startup-NumLock build, preserved
here so the build script does not require an untracked old build directory.
Their original line endings and comment whitespace are preserved by
`.gitattributes` because the hashes describe exact input bytes.

| File | SHA-256 |
| --- | --- |
| `verified-a-source.cc` | `1fc263fbbfd261068617106f75b13f0bf92c6e95cc5efe32a7c39e6ac0619ded` |
| `verified-a-flash.c` | `114cceb43bc7fdb896ca5dbce7682df250a3f4c3f993dd374ead672042a7afe4` |

The startup-OFF build must match the verified reference A byte for byte.
`tools/build-startup-numlock-candidate.py` enforces that check before enabling
the new option. Embedded/runtime B inputs are extracted from hash-checked
tracked UF2s. Local Pico SDK/TinyUSB dependencies and the ARM toolchain are
still required, as for existing builds.
