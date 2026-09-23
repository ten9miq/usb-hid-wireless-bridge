#ifndef _HID_HOST_DIAGNOSTICS_H_
#define _HID_HOST_DIAGNOSTICS_H_

#include <stdint.h>

enum class HidHostDiagnosticEvent : uint8_t {
    // A-side liveness report. It has no B-side device context.
    HEARTBEAT = 0,
    MOUNT = 1,
    UMOUNT = 2,
    RECEIVE_ARM = 3,
    RECEIVE_REPORT = 4,
    // A-side confirmation that descriptor_received_callback data arrived
    // from B. This does not depend on a B-side TinyUSB mount diagnostic.
    DEVICE_CONNECTED = 5,
    // TinyUSB host-stack evidence emitted before a class-driver mount.
    HCD_DEVICE_ATTACH = 6,
    HCD_DEVICE_REMOVE = 7,
    ENUMERATION_CONTROL_FAILURE = 8,
    ENDPOINT_OPEN_FAILURE = 9,
    HID_INSTANCE_CAPACITY = 10,
    // A-side HID report parsing reached monitor_usage() for Cursor X or Y.
    // report_bytes contains the little-endian usage followed by its value.
    PARSED_USAGE = 11,
    // Per-interface B-side callback and A-side UART receive counters. This
    // uses the existing 58-byte diagnostic record and is diagnostic-only.
    REPORT_COUNTERS = 12,
    // A-side physical-hub counters: parsed X/Y, read_input X/Y, last parsed X/Y,
    // received reports. Seven little-endian 32-bit words span report_bytes
    // (first 8 bytes) and report_descriptor_bytes (remaining 20 bytes).
    USAGE_COUNTERS = 13,
    // A-side merged output counters: input frames X/Y, accumulated additions
    // X/Y, nonzero XY reports built/queued/USB accepted. hub_port is 0 because
    // source identity no longer exists after mapping aggregation.
    OUTPUT_COUNTERS = 14,
    // B-side RP2040 USB host-controller state.  This uses the dedicated
    // hid_host_hcd_snapshot_t wire layout below rather than the legacy fields.
    HCD_SNAPSHOT = 15,
    // B-side diagnostic UART transport health. This fixed payload distinguishes
    // a missing event-15/event-12 producer from B-to-A UART backpressure.
    B_DIAGNOSTIC_TRANSPORT = 16,
    B_RUNTIME_IDENTITY = 17,
    B_G700_ENDPOINT_HEALTH = 18,
    B_FLASH_LOADER_STATUS = 19,
};

// A-side watchdog scratch registers survive the RAM B-loader's reboot.
#define B_FLASH_LOADER_STATUS_MAGIC 0x34444848u // "HHD4"

#define HID_HOST_DIAGNOSTIC_FLAG_SUCCESS 0x01
#define HID_HOST_DIAGNOSTIC_FLAG_HAS_INPUT 0x02
#define HID_HOST_DIAGNOSTIC_FLAG_INTERFACE_INFO_VALID 0x04
#define HID_HOST_DIAGNOSTIC_MAX_REPORT_BYTES 8
#define HID_HOST_DIAGNOSTIC_REPORT_COUNTER_BYTES 8
// A diagnostic Input report has 58 bytes after the HHD1/version header.
// Reserve its remaining 22 bytes for a DEVICE_CONNECTED descriptor fragment.
// Fragments are identified by report_descriptor_offset and can reconstruct a
// descriptor longer than one USB HID packet without changing normal builds.
#define HID_HOST_DIAGNOSTIC_MAX_DESCRIPTOR_BYTES 22

struct __attribute__((packed)) hid_host_diagnostic_t {
    HidHostDiagnosticEvent event;
    uint8_t flags;
    uint8_t dev_addr;
    uint8_t instance;
    uint8_t interface_number;
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint8_t endpoint_count;
    uint8_t hub_addr;
    uint8_t hub_port;
    uint16_t vid;
    uint16_t pid;
    uint16_t report_descriptor_length;
    uint16_t report_length;
    // Event RECEIVE_REPORT carries the first min(report_length, 8) bytes.
    // The fixed 63-byte HID payload has room for this backwards-compatible
    // extension; other events leave these fields zeroed.
    uint8_t report_bytes_length;
    uint8_t report_bytes[HID_HOST_DIAGNOSTIC_MAX_REPORT_BYTES];
    // Pre-mount TinyUSB diagnostics. enumeration_stage is TinyUSB's internal
    // enumeration state for ENUMERATION_CONTROL_FAILURE. transfer_result is
    // TinyUSB's xfer_result_t value. Endpoint failures use endpoint_address
    // and endpoint_packet_size; the other events leave those fields zero.
    uint8_t enumeration_stage;
    uint8_t transfer_result;
    uint8_t endpoint_address;
    uint16_t endpoint_packet_size;
    // DEVICE_CONNECTED carries the report descriptor in consecutive chunks.
    // report_descriptor_length remains the total length for every chunk.
    uint16_t report_descriptor_offset;
    uint8_t report_descriptor_bytes_length;
    uint8_t report_descriptor_bytes[HID_HOST_DIAGNOSTIC_MAX_DESCRIPTOR_BYTES];
};

// Fixed event-15 payload.  The first byte deliberately matches
// hid_host_diagnostic_t::event so it can use the existing UART and WebHID
// transport without changing the 63-byte HHD1 report.
struct __attribute__((packed)) hid_host_hcd_snapshot_t {
    HidHostDiagnosticEvent event;
    uint8_t flags;
    uint8_t format_version;
    uint8_t interrupt_slot_count;
    uint8_t int_ep_suppressed;
    uint8_t epx_state;
    uint8_t hid_instance_count;
    uint8_t reserved0;
    uint32_t control_timeout_count;
    uint32_t control_error_count;
    uint32_t endpoint_open_failure_count;
    uint32_t saved_int_ep_ctrl;
    uint32_t current_int_ep_ctrl;
    uint16_t interrupt_enable_mask;
    uint16_t configured_mask;
    uint16_t claimed_mask;
    uint16_t active_mask;
    uint16_t busy_mask;
    uint8_t reserved[20];
};

#define HID_HOST_HCD_SNAPSHOT_VERSION 1
#define HID_HOST_HCD_EPX_CONFIGURED 0x01
#define HID_HOST_HCD_EPX_ACTIVE 0x02
#define HID_HOST_HCD_EPX_PENDING 0x04

static_assert(sizeof(hid_host_hcd_snapshot_t) == sizeof(hid_host_diagnostic_t),
              "HCD snapshot must preserve the diagnostic wire size");

#endif
