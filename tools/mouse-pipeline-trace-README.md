# Mouse pipeline trace

`mouse-pipeline-trace.html` is a WebHID reader for a firmware build made with
`-DMOUSE_PIPELINE_TRACE=ON`. Open it in Brave, Chrome, or Edge from localhost/HTTPS.
It uses the existing configuration Feature report (ID 100), not a new input
report, so the release descriptor is unchanged.

For example, from the repository root run `python -m http.server 8000 -d tools`
and open `http://localhost:8000/mouse-pipeline-trace.html`.

1. Connect the configuration HID collection. Use the device/instance from the
   host diagnostics page to restrict B records to the RollerMouse input
   endpoint; leave both values `FF` only while identifying that endpoint.
2. Click **Clear**, then **Arm**. Reproduce the stop/jump. Press **F7** to add a
   mark if useful, then press **F8** to freeze immediately after the jump.
3. Fetch A and B records separately. B replies traverse UART only after an
   explicit INFO/RECORD request and are cached on A; the page waits 35 ms
   before reading that cache.

The records have a shared 24-byte ABI. The A-side only emits decoded/mapped/
queued/sent stages for reports containing Generic Desktop Cursor X or Y. B is
endpoint-filterable because it cannot parse the descriptor itself. This is a
diagnostic build: no trace event is sent during normal movement, and frozen
ring contents are lost on reset.

`B_USB_CALLBACK` and `A_UART_RECEIVED` use the 12-byte payload region for the
first raw report bytes; a 16-byte report immediately adds a four-byte `_CONT`
record with the same timestamp for bytes 12..15. Their record `flags` byte is
the captured length. The reader renders those bytes as hex, while later stages
retain decoded X/Y and queue/send fields.
