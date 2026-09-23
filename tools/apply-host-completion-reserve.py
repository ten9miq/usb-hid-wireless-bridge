#!/usr/bin/env python3
"""Reapply the tested RP2040 host queue reserve to the pinned TinyUSB snapshot.

The source root must be a copy of the verified pre-reserve TinyUSB tree. This
script checks all three input hashes before modifying anything and checks the
output hashes before writing, so it cannot silently patch a different version.
"""

import argparse
import hashlib
from pathlib import Path


PATCHES = {
    "src/host/usbh.c": {
        "before_sha256": "a56d0aaaa028ef706e21e1168fd39e3bc9b964aca1902c30e89e66df3c6e1b58",
        "after_sha256": "0e1dee58bb764124a8ac28b0f7e8e9743f16e4362993172074fd73fb064d9231",
        "replacements": [
            (
                "OSAL_QUEUE_DEF(usbh_int_set, _usbh_qdef, CFG_TUH_TASK_QUEUE_SZ, hcd_event_t);\n"
                "static osal_queue_t _usbh_q;\n",
                "OSAL_QUEUE_DEF(usbh_int_set, _usbh_qdef, CFG_TUH_TASK_QUEUE_SZ, hcd_event_t);\n"
                "#ifdef HID_HOST_DIAGNOSTICS\n"
                "static volatile uint32_t diagnostic_queue_drop_count;\n"
                "static volatile uint32_t diagnostic_xfer_drop_count;\n"
                "static volatile uint32_t diagnostic_func_drop_count;\n"
                "#endif\n"
                "static osal_queue_t _usbh_q;\n\n"
                "#ifdef CFG_TUH_COMPLETION_QUEUE_RESERVE\n"
                "uint16_t usbh_event_queue_remaining(void) {\n"
                "  // Called only from the RP2040 USB IRQ; the consumer locks this queue by\n"
                "  // disabling that IRQ, so this count is stable while choosing a SOF defer.\n"
                "  return tu_fifo_remaining(&_usbh_q->ff);\n"
                "}\n"
                "#endif\n",
            ),
            (
                "TU_ATTR_ALWAYS_INLINE static inline bool queue_event(hcd_event_t const * event, bool in_isr) {\n"
                "  TU_ASSERT(osal_queue_send(_usbh_q, event, in_isr));\n"
                "  tuh_event_hook_cb(event->rhport, event->event_id, in_isr);\n"
                "  return true;\n"
                "}\n",
                "TU_ATTR_ALWAYS_INLINE static inline bool queue_event(hcd_event_t const * event, bool in_isr) {\n"
                "#ifdef HID_HOST_DIAGNOSTICS\n"
                "  bool const queued = osal_queue_send(_usbh_q, event, in_isr);\n"
                "  if (!queued) {\n"
                "    diagnostic_queue_drop_count++;\n"
                "    if (event->event_id == HCD_EVENT_XFER_COMPLETE) diagnostic_xfer_drop_count++;\n"
                "    if (event->event_id == USBH_EVENT_FUNC_CALL) diagnostic_func_drop_count++;\n"
                "  }\n"
                "  TU_ASSERT(queued);\n"
                "#else\n"
                "  TU_ASSERT(osal_queue_send(_usbh_q, event, in_isr));\n"
                "#endif\n"
                "  tuh_event_hook_cb(event->rhport, event->event_id, in_isr);\n"
                "  return true;\n"
                "}\n",
            ),
            (
                "  return dev->ep_status[epnum][dir].busy;\n"
                "}\n\n"
                "#ifdef HID_HOST_DIAGNOSTICS\n"
                "bool usbh_diagnostic_edpt_state",
                "  return dev->ep_status[epnum][dir].busy;\n"
                "}\n\n"
                "#ifdef HID_HOST_DIAGNOSTICS\n"
                "uint32_t usbh_diagnostic_queue_drop_count(void) {\n"
                "  return diagnostic_queue_drop_count;\n"
                "}\n\n"
                "uint32_t usbh_diagnostic_xfer_drop_count(void) {\n"
                "  return diagnostic_xfer_drop_count;\n"
                "}\n\n"
                "uint32_t usbh_diagnostic_func_drop_count(void) {\n"
                "  return diagnostic_func_drop_count;\n"
                "}\n"
                "#endif\n\n"
                "#ifdef HID_HOST_DIAGNOSTICS\n"
                "bool usbh_diagnostic_edpt_state",
            ),
        ],
    },
    "src/host/hcd.h": {
        "before_sha256": "a0a8e252e2299499e1f1c27972716f1eaf1fde00a63c1f5bcd906a1e341ea524",
        "after_sha256": "a501a2a8918a2b32cdf9505d8634b7ec0ec6dd93f795677ac1fb2acbda076e03",
        "replacements": [
            (
                "//------------- Event API -------------//\n\n"
                "// Called by HCD to notify stack",
                "//------------- Event API -------------//\n"
                "#ifdef CFG_TUH_COMPLETION_QUEUE_RESERVE\n"
                "uint16_t usbh_event_queue_remaining(void);\n"
                "#endif\n\n"
                "// Called by HCD to notify stack",
            ),
            (
                "#ifdef HID_HOST_DIAGNOSTICS\n"
                "extern void tuh_diagnostic_event_cb",
                "#ifdef HID_HOST_DIAGNOSTICS\n"
                "uint32_t usbh_diagnostic_queue_drop_count(void);\n"
                "uint32_t usbh_diagnostic_xfer_drop_count(void);\n"
                "uint32_t usbh_diagnostic_func_drop_count(void);\n"
                "#ifdef CFG_TUH_COMPLETION_QUEUE_RESERVE\n"
                "uint32_t hcd_rp2040_diagnostic_sof_reserve_skip_count(void);\n"
                "#endif\n"
                "extern void tuh_diagnostic_event_cb",
            ),
        ],
    },
    "src/portable/raspberrypi/rp2040/hcd_rp2040.c": {
        "before_sha256": "985b5bd32070d7b12b5900c1a2e5c7738ee92821b3d775a78e9faaf1a9836c73",
        "after_sha256": "54b8add6050156d15ac0246ecc2d12594086e96b24540efccf289867ed88dcf3",
        "replacements": [
            (
                "#ifdef HID_HOST_DIAGNOSTICS\n"
                "static uint32_t diagnostic_control_timeout_count;\n"
                "#endif\n",
                "#ifdef HID_HOST_DIAGNOSTICS\n"
                "static uint32_t diagnostic_control_timeout_count;\n"
                "#ifdef CFG_TUH_COMPLETION_QUEUE_RESERVE\n"
                "static volatile uint32_t diagnostic_sof_reserve_skip_count;\n"
                "#endif\n"
                "#endif\n",
            ),
            (
                "    if (tuh_sof_cb) {\n"
                "      hcd_event_t event;\n"
                "      event.rhport              = RHPORT_NATIVE;\n"
                "      event.event_id            = USBH_EVENT_FUNC_CALL;\n"
                "      event.func_call.func      = call_tuh_sof_cb;\n"
                "      event.func_call.param     = NULL;\n"
                "      hcd_event_handler(&event, true);\n"
                "    }\n",
                "    if (tuh_sof_cb) {\n"
                "#ifdef CFG_TUH_COMPLETION_QUEUE_RESERVE\n"
                "      // SOF's deferred callback only sends an A-side timing hint. Under\n"
                "      // startup pressure, keep 16 of the 64 event slots for completion and\n"
                "      // attach events. Normal-load SOF timing is unchanged.\n"
                "      if (usbh_event_queue_remaining() <= 16) {\n"
                "#ifdef HID_HOST_DIAGNOSTICS\n"
                "        diagnostic_sof_reserve_skip_count++;\n"
                "#endif\n"
                "      } else\n"
                "#endif\n"
                "      {\n"
                "      hcd_event_t event;\n"
                "      event.rhport              = RHPORT_NATIVE;\n"
                "      event.event_id            = USBH_EVENT_FUNC_CALL;\n"
                "      event.func_call.func      = call_tuh_sof_cb;\n"
                "      event.func_call.param     = NULL;\n"
                "      hcd_event_handler(&event, true);\n"
                "      }\n"
                "    }\n",
            ),
            (
                "#ifdef HID_HOST_DIAGNOSTICS\n"
                "  diagnostic_control_timeout_count = 0;\n"
                "#endif\n",
                "#ifdef HID_HOST_DIAGNOSTICS\n"
                "  diagnostic_control_timeout_count = 0;\n"
                "#ifdef CFG_TUH_COMPLETION_QUEUE_RESERVE\n"
                "  diagnostic_sof_reserve_skip_count = 0;\n"
                "#endif\n"
                "#endif\n",
            ),
            (
                "  restore_interrupts(irq_state);\n"
                "  return true;\n"
                "}\n"
                "#endif\n\n"
                "bool hcd_edpt_open",
                "  restore_interrupts(irq_state);\n"
                "  return true;\n"
                "}\n"
                "#ifdef CFG_TUH_COMPLETION_QUEUE_RESERVE\n"
                "uint32_t hcd_rp2040_diagnostic_sof_reserve_skip_count(void)\n"
                "{\n"
                "  return diagnostic_sof_reserve_skip_count;\n"
                "}\n"
                "#endif\n"
                "#endif\n\n"
                "bool hcd_edpt_open",
            ),
        ],
    },
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def patched(data: bytes, replacements: list[tuple[str, str]]) -> bytes:
    newline = b"\r\n" if b"\r\n" in data else b"\n"
    for before, after in replacements:
        old = before.encode("utf-8").replace(b"\n", newline)
        new = after.encode("utf-8").replace(b"\n", newline)
        if data.count(old) != 1:
            raise ValueError(f"expected exactly one source anchor, found {data.count(old)}")
        data = data.replace(old, new, 1)
    return data


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tinyusb_root", type=Path)
    parser.add_argument("--check", action="store_true", help="verify the base and output without writing")
    args = parser.parse_args()

    outputs = {}
    for relative, specification in PATCHES.items():
        path = args.tinyusb_root / relative
        source = path.read_bytes()
        if digest(source) != specification["before_sha256"]:
            raise ValueError(f"base source hash mismatch: {path}")
        result = patched(source, specification["replacements"])
        if digest(result) != specification["after_sha256"]:
            raise ValueError(f"patched source hash mismatch: {path}")
        outputs[path] = result
    if not args.check:
        for path, result in outputs.items():
            path.write_bytes(result)
    print(f"PASS: {len(outputs)} pinned TinyUSB host files {'checked' if args.check else 'patched'}")


if __name__ == "__main__":
    main()
