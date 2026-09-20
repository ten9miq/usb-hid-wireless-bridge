#include <bsp/board_api.h>
#include <tusb.h>
#ifdef HID_HOST_DIAGNOSTICS
#include "host/hcd.h"
#endif

#include "usb_midi_host.h"

#include "hardware/watchdog.h"
#include "pico/stdio.h"
#include "pico/time.h"

#include "activity_led.h"
#include "dual.h"
#include "hid_host_utils.h"
#include "interval_override.h"
#include "out_report.h"
#include "serial.h"

uint8_t buffer[SERIAL_MAX_PAYLOAD_SIZE + sizeof(device_connected_t)];
bool initialized = false;

struct hid_input_state_t {
    bool active;
    uint8_t dev_addr;
    uint8_t instance;
    bool report_pending;
    uint16_t len;
    uint8_t report[CFG_TUH_HID_EPIN_BUFSIZE];
};

static hid_input_state_t hid_input_states[CFG_TUH_HID];
static uint8_t next_pending_hid_input = 0;

static hid_input_state_t* find_hid_input_state(uint8_t dev_addr, uint8_t instance) {
    for (hid_input_state_t& state : hid_input_states) {
        if (state.active && (state.dev_addr == dev_addr) && (state.instance == instance)) {
            return &state;
        }
    }

    return nullptr;
}

static hid_input_state_t* register_hid_input(uint8_t dev_addr, uint8_t instance) {
    hid_input_state_t* state = find_hid_input_state(dev_addr, instance);
    if (state != nullptr) {
        return state;
    }

    for (hid_input_state_t& candidate : hid_input_states) {
        if (!candidate.active) {
            candidate = {};
            candidate.active = true;
            candidate.dev_addr = dev_addr;
            candidate.instance = instance;
            return &candidate;
        }
    }

    return nullptr;
}

static void unregister_hid_input(uint8_t dev_addr, uint8_t instance) {
    hid_input_state_t* state = find_hid_input_state(dev_addr, instance);
    if (state != nullptr) {
        *state = {};
    }
}

#ifdef HID_HOST_DIAGNOSTICS
struct hid_host_diagnostic_context_t {
    bool active;
    hid_host_diagnostic_t diagnostic;
    uint32_t report_count;
    uint32_t last_report_timestamp_ms;
    uint16_t last_report_len;
};

static hid_host_diagnostic_context_t hid_host_diagnostic_contexts[CFG_TUH_HID];
// These events are infrequent and diagnostic-only.  Keep a small separate
// queue so a full UART buffer cannot discard enumeration evidence or delay
// HID report forwarding.
static const uint8_t HID_HOST_DIAGNOSTIC_PENDING_QUEUE_SIZE = 8;
static dual_hid_host_diagnostic_t pending_hid_host_diagnostics[HID_HOST_DIAGNOSTIC_PENDING_QUEUE_SIZE];
static uint8_t pending_hid_host_diagnostic_head = 0;
static uint8_t pending_hid_host_diagnostic_tail = 0;
static uint8_t pending_hid_host_diagnostic_items = 0;
static uint64_t next_hid_host_diagnostic_receive_report = 0;
static const uint64_t HID_HOST_DIAGNOSTIC_RECEIVE_REPORT_US = 1000000;
static uint64_t next_hid_host_diagnostic_report_counters = 0;
static const uint64_t HID_HOST_DIAGNOSTIC_REPORT_COUNTERS_US = 1000000;
static const uint8_t HID_HOST_DIAGNOSTIC_MAX_REPORT_COUNTERS = 8;
static uint64_t next_hid_host_hcd_snapshot = 0;
static uint64_t next_hid_host_diagnostic_transport_snapshot = 0;
// Keep the latest HCD snapshot outside the ordinary diagnostic FIFO.  HID
// reports may keep that FIFO blocked indefinitely, while A needs this record
// to service its heartbeat-priority event-15 input report.
static bool hid_host_hcd_snapshot_pending = false;
static dual_hid_host_diagnostic_t pending_hid_host_hcd_snapshot = {};
static bool hid_host_diagnostic_transport_snapshot_pending = false;
static dual_hid_host_diagnostic_t pending_hid_host_diagnostic_transport_snapshot = {};
// Periodic counter records must not share the ordinary diagnostic FIFO: that
// FIFO intentionally waits behind forwarded HID reports.  This bounded lane
// retains one snapshot for each supported active HID context and is serviced
// after event 15, regardless of report-forwarding backpressure.
static dual_hid_host_diagnostic_t
    pending_hid_host_priority_diagnostics[HID_HOST_DIAGNOSTIC_MAX_REPORT_COUNTERS];
static uint8_t pending_hid_host_priority_diagnostic_head = 0;
static uint8_t pending_hid_host_priority_diagnostic_tail = 0;
static uint8_t pending_hid_host_priority_diagnostic_items = 0;
static uint32_t hid_host_control_error_count = 0;
static uint32_t hid_host_endpoint_open_failure_count = 0;
static uint32_t hid_host_hcd_snapshot_generated_count = 0;
static uint32_t hid_host_diagnostic_serial_write_success_count = 0;
static uint32_t hid_host_diagnostic_serial_write_failure_count = 0;
static uint32_t hid_host_report_counters_generated_count = 0;
static uint32_t hid_host_report_counters_sent_count = 0;

static bool write_hid_host_diagnostic(const dual_hid_host_diagnostic_t& diagnostic) {
    bool written = serial_write_nonblocking((const uint8_t*) &diagnostic, sizeof(diagnostic));
    if (written) {
        hid_host_diagnostic_serial_write_success_count++;
        if (diagnostic.diagnostic.event == HidHostDiagnosticEvent::REPORT_COUNTERS) {
            hid_host_report_counters_sent_count++;
        }
    } else {
        hid_host_diagnostic_serial_write_failure_count++;
    }
    return written;
}

static uint32_t hid_host_diagnostic_pending_state() {
    // Bytes 0..7 and 8..15 are queue depths; bits 16 and 17 identify the
    // dedicated event-15 and event-16 retry slots respectively.
    return pending_hid_host_diagnostic_items |
        ((uint32_t) pending_hid_host_priority_diagnostic_items << 8) |
        (hid_host_hcd_snapshot_pending ? (1u << 16) : 0u) |
        (hid_host_diagnostic_transport_snapshot_pending ? (1u << 17) : 0u);
}

static hid_host_diagnostic_context_t* find_hid_host_diagnostic_context(
    uint8_t dev_addr, uint8_t instance) {
    for (hid_host_diagnostic_context_t& context : hid_host_diagnostic_contexts) {
        if (context.active &&
            (context.diagnostic.dev_addr == dev_addr) &&
            (context.diagnostic.instance == instance)) {
            return &context;
        }
    }

    return nullptr;
}

static hid_host_diagnostic_context_t* register_hid_host_diagnostic_context(
    uint8_t dev_addr, uint8_t instance) {
    hid_host_diagnostic_context_t* context = find_hid_host_diagnostic_context(dev_addr, instance);
    if (context != nullptr) {
        return context;
    }

    for (hid_host_diagnostic_context_t& candidate : hid_host_diagnostic_contexts) {
        if (!candidate.active) {
            candidate = {};
            candidate.active = true;
            candidate.diagnostic.dev_addr = dev_addr;
            candidate.diagnostic.instance = instance;
            return &candidate;
        }
    }

    return nullptr;
}

static void unregister_hid_host_diagnostic_context(uint8_t dev_addr, uint8_t instance) {
    hid_host_diagnostic_context_t* context = find_hid_host_diagnostic_context(dev_addr, instance);
    if (context != nullptr) {
        *context = {};
    }
}

static void queue_hid_host_diagnostic(const dual_hid_host_diagnostic_t& diagnostic) {
    if (pending_hid_host_diagnostic_items == HID_HOST_DIAGNOSTIC_PENDING_QUEUE_SIZE) {
        return;
    }

    pending_hid_host_diagnostics[pending_hid_host_diagnostic_tail] = diagnostic;
    pending_hid_host_diagnostic_tail =
        (pending_hid_host_diagnostic_tail + 1) % HID_HOST_DIAGNOSTIC_PENDING_QUEUE_SIZE;
    pending_hid_host_diagnostic_items++;
}

static void flush_pending_hid_host_diagnostics() {
    if (pending_hid_host_diagnostic_items == 0) {
        return;
    }

    dual_hid_host_diagnostic_t& diagnostic = pending_hid_host_diagnostics[pending_hid_host_diagnostic_head];
    if (!write_hid_host_diagnostic(diagnostic)) {
        return;
    }

    pending_hid_host_diagnostic_head =
        (pending_hid_host_diagnostic_head + 1) % HID_HOST_DIAGNOSTIC_PENDING_QUEUE_SIZE;
    pending_hid_host_diagnostic_items--;
}

static bool queue_hid_host_priority_diagnostic(const dual_hid_host_diagnostic_t& diagnostic) {
    if (pending_hid_host_priority_diagnostic_items == HID_HOST_DIAGNOSTIC_MAX_REPORT_COUNTERS) {
        return false;
    }

    pending_hid_host_priority_diagnostics[pending_hid_host_priority_diagnostic_tail] = diagnostic;
    pending_hid_host_priority_diagnostic_tail =
        (pending_hid_host_priority_diagnostic_tail + 1) % HID_HOST_DIAGNOSTIC_MAX_REPORT_COUNTERS;
    pending_hid_host_priority_diagnostic_items++;
    return true;
}

static void flush_pending_hid_host_priority_diagnostics() {
    if (pending_hid_host_priority_diagnostic_items == 0) {
        return;
    }

    dual_hid_host_diagnostic_t& diagnostic =
        pending_hid_host_priority_diagnostics[pending_hid_host_priority_diagnostic_head];
    if (!write_hid_host_diagnostic(diagnostic)) {
        return;
    }

    pending_hid_host_priority_diagnostic_head =
        (pending_hid_host_priority_diagnostic_head + 1) % HID_HOST_DIAGNOSTIC_MAX_REPORT_COUNTERS;
    pending_hid_host_priority_diagnostic_items--;
}

static void send_hid_host_diagnostic(
    HidHostDiagnosticEvent event, uint8_t dev_addr, uint8_t instance, bool success, uint16_t report_length = 0) {
    hid_host_diagnostic_t diagnostic = {};
    hid_host_diagnostic_context_t* context = find_hid_host_diagnostic_context(dev_addr, instance);
    if (context != nullptr) {
        diagnostic = context->diagnostic;
    }
    diagnostic.event = event;
    diagnostic.dev_addr = dev_addr;
    diagnostic.instance = instance;
    diagnostic.report_length = report_length;
    if (success) {
        diagnostic.flags |= HID_HOST_DIAGNOSTIC_FLAG_SUCCESS;
    } else {
        diagnostic.flags &= ~HID_HOST_DIAGNOSTIC_FLAG_SUCCESS;
    }
    dual_hid_host_diagnostic_t message = {};
    message.diagnostic = diagnostic;
    // Store the complete context with the event before any later mount or
    // unmount can replace the per-instance cache.
    queue_hid_host_diagnostic(message);
}

static void set_hid_host_diagnostic_context(
    uint8_t dev_addr,
    uint8_t instance,
    uint16_t vid,
    uint16_t pid,
    uint8_t hub_addr,
    uint8_t hub_port,
    const tuh_itf_info_t& itf_info,
    bool itf_info_valid,
    uint16_t desc_len,
    bool has_input) {
    hid_host_diagnostic_context_t* context = register_hid_host_diagnostic_context(dev_addr, instance);
    if (context == nullptr) {
        return;
    }

    hid_host_diagnostic_t& diagnostic = context->diagnostic;
    diagnostic = {};
    context->report_count = 0;
    context->last_report_timestamp_ms = 0;
    context->last_report_len = 0;
    diagnostic.dev_addr = dev_addr;
    diagnostic.instance = instance;
    diagnostic.vid = vid;
    diagnostic.pid = pid;
    diagnostic.hub_addr = hub_addr;
    diagnostic.hub_port = hub_port;
    diagnostic.report_descriptor_length = desc_len;
    if (has_input) {
        diagnostic.flags |= HID_HOST_DIAGNOSTIC_FLAG_HAS_INPUT;
    }
    if (itf_info_valid) {
        diagnostic.flags |= HID_HOST_DIAGNOSTIC_FLAG_INTERFACE_INFO_VALID;
        diagnostic.interface_number = itf_info.desc.bInterfaceNumber;
        diagnostic.interface_class = itf_info.desc.bInterfaceClass;
        diagnostic.interface_subclass = itf_info.desc.bInterfaceSubClass;
        diagnostic.interface_protocol = itf_info.desc.bInterfaceProtocol;
        diagnostic.endpoint_count = itf_info.desc.bNumEndpoints;
    }
}

static void record_hid_host_report(uint8_t dev_addr, uint8_t instance, uint16_t len) {
    hid_host_diagnostic_context_t* context = find_hid_host_diagnostic_context(dev_addr, instance);
    if (context == nullptr) {
        return;
    }

    context->report_count++;
    context->last_report_len = len;
    context->last_report_timestamp_ms = (uint32_t) (time_us_64() / 1000);
}

static void queue_hid_host_report_counters() {
    // Preserve a complete periodic set across UART backpressure rather than
    // overwriting or diverting it into the normal diagnostic FIFO.
    if (pending_hid_host_priority_diagnostic_items != 0) {
        return;
    }

    uint64_t now = time_us_64();
    if ((next_hid_host_diagnostic_report_counters != 0) &&
        (now < next_hid_host_diagnostic_report_counters)) {
        return;
    }

    uint8_t queued = 0;
    for (const hid_host_diagnostic_context_t& context : hid_host_diagnostic_contexts) {
        if (!context.active || (queued == HID_HOST_DIAGNOSTIC_MAX_REPORT_COUNTERS)) {
            continue;
        }

        // report_bytes is a compact B-side counter snapshot: uint32_t count,
        // then uint32_t age in milliseconds. A fills its matching values when
        // it receives this record over UART.
        hid_host_diagnostic_t diagnostic = context.diagnostic;
        diagnostic.event = HidHostDiagnosticEvent::REPORT_COUNTERS;
        diagnostic.flags |= HID_HOST_DIAGNOSTIC_FLAG_SUCCESS;
        diagnostic.report_length = context.last_report_len;
        diagnostic.report_bytes_length = HID_HOST_DIAGNOSTIC_REPORT_COUNTER_BYTES;
        uint32_t last_age_ms = context.last_report_timestamp_ms == 0
            ? UINT32_MAX
            : (uint32_t) (now / 1000) - context.last_report_timestamp_ms;
        memcpy(diagnostic.report_bytes, &context.report_count, sizeof(context.report_count));
        memcpy(diagnostic.report_bytes + sizeof(context.report_count), &last_age_ms, sizeof(last_age_ms));
        dual_hid_host_diagnostic_t message = {};
        message.diagnostic = diagnostic;
        if (!queue_hid_host_priority_diagnostic(message)) {
            return;
        }
        hid_host_report_counters_generated_count++;
        queued++;
    }

    next_hid_host_diagnostic_report_counters = now + HID_HOST_DIAGNOSTIC_REPORT_COUNTERS_US;
}

static void queue_hid_host_hcd_snapshot() {
    if (hid_host_hcd_snapshot_pending) {
        return;
    }

    uint64_t now = time_us_64();
    if ((next_hid_host_hcd_snapshot != 0) && (now < next_hid_host_hcd_snapshot)) {
        return;
    }

    hcd_rp2040_diagnostic_snapshot_t hcd = {};
    bool hcd_snapshot_available = hcd_rp2040_diagnostic_snapshot(&hcd);

    hid_host_hcd_snapshot_t snapshot = {};
    snapshot.event = HidHostDiagnosticEvent::HCD_SNAPSHOT;
    snapshot.flags = HID_HOST_DIAGNOSTIC_FLAG_SUCCESS;
    snapshot.format_version = HID_HOST_HCD_SNAPSHOT_VERSION;
    // A missing HCD diagnostic provider is still a valid baseline: A must
    // receive an event-15 record with the current wire version instead of
    // waiting forever for its first snapshot.  Leave all payload fields zero
    // in that case.
    if (hcd_snapshot_available) {
        snapshot.interrupt_slot_count = hcd.interrupt_slot_count;
        snapshot.int_ep_suppressed = hcd.int_ep_suppressed;
        snapshot.epx_state = hcd.epx_state;
        for (const hid_host_diagnostic_context_t& context : hid_host_diagnostic_contexts) {
            snapshot.hid_instance_count += context.active;
        }
        snapshot.control_timeout_count = hcd.control_timeout_count;
        snapshot.control_error_count = hid_host_control_error_count;
        snapshot.endpoint_open_failure_count = hid_host_endpoint_open_failure_count;
        snapshot.saved_int_ep_ctrl = hcd.saved_int_ep_ctrl;
        snapshot.current_int_ep_ctrl = hcd.current_int_ep_ctrl;
        snapshot.interrupt_enable_mask = hcd.interrupt_enable_mask;
        snapshot.configured_mask = hcd.configured_mask;
        snapshot.claimed_mask = hcd.claimed_mask;
        snapshot.active_mask = hcd.active_mask;
        snapshot.busy_mask = hcd.busy_mask;
    }

    memcpy(&pending_hid_host_hcd_snapshot.diagnostic, &snapshot, sizeof(snapshot));
    hid_host_hcd_snapshot_pending = true;
    hid_host_hcd_snapshot_generated_count++;
}

static bool flush_hid_host_hcd_snapshot() {
    if (!hid_host_hcd_snapshot_pending) {
        return true;
    }
    if (!write_hid_host_diagnostic(pending_hid_host_hcd_snapshot)) {
        return false;
    }

    hid_host_hcd_snapshot_pending = false;
    next_hid_host_hcd_snapshot = time_us_64() + HID_HOST_DIAGNOSTIC_REPORT_COUNTERS_US;
    return true;
}

static void queue_hid_host_diagnostic_transport_snapshot() {
    if (hid_host_diagnostic_transport_snapshot_pending) {
        return;
    }

    uint64_t now = time_us_64();
    if ((next_hid_host_diagnostic_transport_snapshot != 0) &&
        (now < next_hid_host_diagnostic_transport_snapshot)) {
        return;
    }

    hid_host_diagnostic_t snapshot = {};
    snapshot.event = HidHostDiagnosticEvent::B_DIAGNOSTIC_TRANSPORT;
    snapshot.flags = HID_HOST_DIAGNOSTIC_FLAG_SUCCESS;
    snapshot.report_bytes_length = HID_HOST_DIAGNOSTIC_REPORT_COUNTER_BYTES;
    snapshot.report_descriptor_bytes_length = 16;
    uint32_t counters[] = {
        hid_host_hcd_snapshot_generated_count,
        hid_host_diagnostic_serial_write_success_count,
        hid_host_diagnostic_serial_write_failure_count,
        hid_host_diagnostic_pending_state(),
        hid_host_report_counters_generated_count,
        hid_host_report_counters_sent_count,
    };
    memcpy(snapshot.report_bytes, counters, sizeof(snapshot.report_bytes));
    memcpy(snapshot.report_descriptor_bytes, counters + 2,
           sizeof(counters) - sizeof(snapshot.report_bytes));
    pending_hid_host_diagnostic_transport_snapshot.diagnostic = snapshot;
    hid_host_diagnostic_transport_snapshot_pending = true;
}

static bool flush_hid_host_diagnostic_transport_snapshot() {
    if (!hid_host_diagnostic_transport_snapshot_pending) {
        return true;
    }
    if (!write_hid_host_diagnostic(pending_hid_host_diagnostic_transport_snapshot)) {
        return false;
    }

    hid_host_diagnostic_transport_snapshot_pending = false;
    next_hid_host_diagnostic_transport_snapshot =
        time_us_64() + HID_HOST_DIAGNOSTIC_REPORT_COUNTERS_US;
    return true;
}

static void service_hid_host_priority_diagnostics() {
    queue_hid_host_hcd_snapshot();
    queue_hid_host_report_counters();
    // Snapshot after the event-15/event-12 producers so this heartbeat shows
    // their newly pending work as well as their cumulative counters.
    queue_hid_host_diagnostic_transport_snapshot();
    // Event 16 is the transport probe, so it stays ahead of event 15/12. A
    // successful probe proves that a missing event-15/event-12 is not caused
    // by the B-to-A diagnostic UART path.
    if (flush_hid_host_diagnostic_transport_snapshot()) {
        if (flush_hid_host_hcd_snapshot()) {
            flush_pending_hid_host_priority_diagnostics();
        }
    }
}

extern "C" void tuh_diagnostic_event_cb(
    uint8_t event,
    uint8_t dev_addr,
    uint8_t hub_addr,
    uint8_t hub_port,
    uint8_t detail,
    uint8_t result,
    uint16_t value) {
    hid_host_diagnostic_t diagnostic = {};
    diagnostic.event = static_cast<HidHostDiagnosticEvent>(event);
    diagnostic.dev_addr = dev_addr;
    diagnostic.hub_addr = hub_addr;
    diagnostic.hub_port = hub_port;

    switch (diagnostic.event) {
        case HidHostDiagnosticEvent::ENUMERATION_CONTROL_FAILURE:
            hid_host_control_error_count++;
            diagnostic.enumeration_stage = detail;
            diagnostic.transfer_result = result;
            // value contains the failed setup request code.
            diagnostic.report_length = value;
            break;
        case HidHostDiagnosticEvent::ENDPOINT_OPEN_FAILURE:
            hid_host_endpoint_open_failure_count++;
            diagnostic.endpoint_address = detail;
            diagnostic.endpoint_packet_size = value;
            break;
        case HidHostDiagnosticEvent::HID_INSTANCE_CAPACITY:
            diagnostic.interface_number = detail;
            diagnostic.report_length = value;
            break;
        default:
            break;
    }

    dual_hid_host_diagnostic_t message = {};
    message.diagnostic = diagnostic;
    queue_hid_host_diagnostic(message);
}
#endif

static bool arm_hid_report(uint8_t dev_addr, uint8_t instance) {
    bool armed = tuh_hid_receive_report(dev_addr, instance);
#ifdef HID_HOST_DIAGNOSTICS
    send_hid_host_diagnostic(HidHostDiagnosticEvent::RECEIVE_ARM, dev_addr, instance, armed);
#endif
    return armed;
}

static void arm_ready_hid_inputs() {
    for (hid_input_state_t& state : hid_input_states) {
        if (state.active && !state.report_pending && tuh_hid_receive_ready(state.dev_addr, state.instance)) {
            arm_hid_report(state.dev_addr, state.instance);
        }
    }
}

bool serial_callback(const uint8_t* data, uint16_t len) {
    switch ((DualCommand) data[0]) {
        case DualCommand::B_INIT:
            interval_override = ((b_init_t*) data)->interval_override;
            initialized = true;
            break;
        case DualCommand::RESTART:
            watchdog_reboot(0, 0, 0);
            break;
        case DualCommand::SEND_OUT_REPORT: {
            send_out_report_t* msg = (send_out_report_t*) data;
            do_queue_out_report(msg->report, len - sizeof(send_out_report_t), msg->report_id, msg->dev_addr, msg->interface, OutType::OUTPUT);
            break;
        }
        case DualCommand::SET_FEATURE_REPORT: {
            set_feature_report_t* msg = (set_feature_report_t*) data;
            do_queue_out_report(msg->report, len - sizeof(set_feature_report_t), msg->report_id, msg->dev_addr, msg->interface, OutType::SET_FEATURE);
            break;
        }
        case DualCommand::GET_FEATURE_REPORT: {
            get_feature_report_t* msg = (get_feature_report_t*) data;
            do_queue_get_report(msg->report_id, msg->dev_addr, msg->interface, msg->len);
            break;
        }
        default:
            break;
    }

    return false;
}

void request_b_init() {
    request_b_init_t msg;
    serial_write_nonblocking((uint8_t*) &msg, sizeof(msg));
}

static bool send_hid_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    if (len > CFG_TUH_HID_EPIN_BUFSIZE) {
        return false;
    }

    report_received_t* msg = (report_received_t*) buffer;
    msg->command = DualCommand::REPORT_RECEIVED;
    msg->dev_addr = dev_addr;
    msg->interface = instance;
    memcpy(msg->report, report, len);
    return serial_write_nonblocking((uint8_t*) msg, len + sizeof(report_received_t));
}

static void flush_pending_hid_reports() {
    uint8_t start = next_pending_hid_input;
    for (uint8_t count = 0; count < CFG_TUH_HID; count++) {
        uint8_t state_index = (start + count) % CFG_TUH_HID;
        hid_input_state_t& state = hid_input_states[state_index];
        if (!state.active || !state.report_pending) {
            continue;
        }
        if (!send_hid_report(state.dev_addr, state.instance, state.report, state.len)) {
            next_pending_hid_input = state_index;
            return;
        }

        state.report_pending = false;
        arm_hid_report(state.dev_addr, state.instance);
        next_pending_hid_input = (state_index + 1) % CFG_TUH_HID;
    }
}

static bool has_pending_hid_reports() {
    for (const hid_input_state_t& state : hid_input_states) {
        if (state.active && state.report_pending) {
            return true;
        }
    }

    return false;
}

int main() {
    serial_init();
    board_init();
    stdio_init_all();

    while (!initialized) {
        request_b_init();
        serial_read(serial_callback);
    }

    // TinyUSB otherwise switches every Boot-capable keyboard/mouse interface
    // to Boot protocol during enumeration.  Prefer the device's full report
    // descriptor so composite and extended keyboard collections are preserved.
    // This is intentionally generic rather than tied to a VID/PID quirk.
    tuh_hid_set_default_protocol(HID_PROTOCOL_REPORT);
    tusb_init();

    while (true) {
        // serial_read() first drains bytes already accepted into the UART
        // buffer.
        serial_read(serial_callback);
#ifdef HID_HOST_DIAGNOSTICS
        // Use that newly available UART space for periodic diagnostics before
        // queued HID reports can consume it again.  The helper's shared
        // period and pending state prevent same-loop duplicate records.
        service_hid_host_priority_diagnostics();
#endif
        // Give reports which previously hit backpressure first use of the
        // remaining space before tuh_task() can deliver another report from
        // a high-rate interface.
        flush_pending_hid_reports();
        tuh_task();
        // Enumeration of another device can leave an already-mounted HID
        // input endpoint idle.  Re-arm every independently tracked interface
        // which has neither an active transfer nor a report held for UART.
        arm_ready_hid_inputs();
        do_send_out_report();
#ifdef HID_HOST_DIAGNOSTICS
        // Ordinary mount/enumeration diagnostics remain best-effort behind
        // report forwarding; periodic events above do not use this FIFO.
        if (!has_pending_hid_reports()) {
            flush_pending_hid_host_diagnostics();
        }
#endif
        activity_led_off_maybe();
    }

    return 0;
}

void report_received_callback(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    activity_led_on();

#ifdef HID_HOST_DIAGNOSTICS
    record_hid_host_report(dev_addr, instance, len);
#endif

    if (len > CFG_TUH_HID_EPIN_BUFSIZE) {
        return;
    }

    hid_input_state_t* state = find_hid_input_state(dev_addr, instance);
    if (state == nullptr) {
        state = register_hid_input(dev_addr, instance);
    }
    if (state == nullptr) {
        return;
    }

    // Once any interface has hit UART backpressure, route every newly
    // completed interface through the same per-interface pending set.  A
    // high-rate source therefore cannot bypass and repeatedly consume the
    // small amount of UART space freed ahead of an older pending source.
    if (has_pending_hid_reports() || !send_hid_report(dev_addr, instance, report, len)) {
        // Apply backpressure to this interface without blocking tuh_task().
        // The transfer is re-armed after the complete report has been queued,
        // so button/key releases are neither dropped nor reordered.
        state->report_pending = true;
        state->len = len;
        memcpy(state->report, report, len);
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
#ifdef HID_HOST_DIAGNOSTICS
    // Mount callbacks are useful enumeration evidence, but a received report
    // is direct proof that the B-side HID callback is running.  Keep this
    // nonblocking diagnostic traffic to one packet per second globally.
    uint64_t now = time_us_64();
    if ((next_hid_host_diagnostic_receive_report == 0) ||
        (now >= next_hid_host_diagnostic_receive_report)) {
        send_hid_host_diagnostic(HidHostDiagnosticEvent::RECEIVE_REPORT, dev_addr, instance, true, len);
        next_hid_host_diagnostic_receive_report = now + HID_HOST_DIAGNOSTIC_RECEIVE_REPORT_US;
    }
#endif

    report_received_callback(dev_addr, instance, report, len);

    hid_input_state_t* state = find_hid_input_state(dev_addr, instance);
    if ((state != nullptr) && !state->report_pending) {
        arm_hid_report(dev_addr, instance);
    }
}

void descriptor_received_callback(uint16_t vendor_id, uint16_t product_id, const uint8_t* report_descriptor, int len, uint16_t interface, uint8_t hub_port, uint8_t itf_num) {
    device_connected_t* msg = (device_connected_t*) buffer;
    msg->command = DualCommand::DEVICE_CONNECTED;
    msg->vid = vendor_id;
    msg->pid = product_id;
    msg->dev_addr = (interface >> 8) & 0xFF;
    msg->interface = interface & 0xFF;
    msg->hub_port = hub_port;
    msg->itf_num = itf_num;
    memcpy(msg->report_descriptor, report_descriptor, len);
    serial_write((uint8_t*) msg, len + sizeof(device_connected_t));
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    printf("tuh_hid_mount_cb\n");

    uint8_t hub_addr;
    uint8_t hub_port;
    tuh_get_hub_addr_port(dev_addr, &hub_addr, &hub_port);

    uint16_t vid;
    uint16_t pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    tuh_itf_info_t itf_info = {};
    bool itf_info_valid = tuh_hid_itf_get_info(dev_addr, instance, &itf_info);
    uint8_t itf_num = itf_info_valid ? itf_info.desc.bInterfaceNumber : instance;
    bool has_input = itf_info_valid && itf_info.desc.bNumEndpoints && hid_report_descriptor_has_input(desc_report, desc_len);

#ifdef HID_HOST_DIAGNOSTICS
    set_hid_host_diagnostic_context(
        dev_addr, instance, vid, pid, hub_addr, hub_port, itf_info, itf_info_valid, desc_len, has_input);
    send_hid_host_diagnostic(HidHostDiagnosticEvent::MOUNT, dev_addr, instance, true);
#endif

    descriptor_received_callback(vid, pid, desc_report, desc_len, (uint16_t) (dev_addr << 8) | instance, hub_port, itf_num);
    if (has_input) {
        register_hid_input(dev_addr, instance);
        // Also recovers any previously mounted input interface which became
        // idle while this device was being enumerated.
        arm_ready_hid_inputs();
    }
}

void umount_callback(uint8_t dev_addr, uint8_t instance) {
    device_disconnected_t msg;
    msg.dev_addr = dev_addr;
    msg.interface = instance;
    serial_write((uint8_t*) &msg, sizeof(msg));
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    printf("tuh_hid_umount_cb %d %d\n", dev_addr, instance);
#ifdef HID_HOST_DIAGNOSTICS
    send_hid_host_diagnostic(HidHostDiagnosticEvent::UMOUNT, dev_addr, instance, true);
    unregister_hid_host_diagnostic_context(dev_addr, instance);
#endif
    unregister_hid_input(dev_addr, instance);
    umount_callback(dev_addr, instance);
}

void tuh_sof_cb() {
    start_of_frame_t msg;
    serial_write_nonblocking((uint8_t*) &msg, sizeof(msg));
}

void get_report_cb(uint8_t dev_addr, uint8_t interface, uint8_t report_id, uint8_t report_type, uint8_t* report, uint16_t len) {
    get_feature_response_t* msg = (get_feature_response_t*) buffer;
    msg->command = DualCommand::GET_FEATURE_RESPONSE;
    msg->dev_addr = dev_addr;
    msg->interface = interface;
    msg->report_id = report_id;
    memcpy(msg->report, report, len);
    serial_write((uint8_t*) msg, len + sizeof(get_feature_response_t));
}

void set_report_complete_cb(uint8_t dev_addr, uint8_t interface, uint8_t report_id) {
    set_feature_complete_t* msg = (set_feature_complete_t*) buffer;
    msg->command = DualCommand::SET_FEATURE_COMPLETE;
    msg->dev_addr = dev_addr;
    msg->interface = interface;
    msg->report_id = report_id;
    serial_write((uint8_t*) msg, sizeof(set_feature_complete_t));
}

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets) {
    activity_led_on();

    uint8_t hub_addr;
    uint8_t hub_port;
    tuh_get_hub_addr_port(dev_addr, &hub_addr, &hub_port);

    midi_received_t* msg = (midi_received_t*) buffer;
    msg->command = DualCommand::MIDI_RECEIVED;
    msg->hub_port = hub_port;
    while (tuh_midi_packet_read(dev_addr, msg->msg)) {
        serial_write_nonblocking((uint8_t*) msg, sizeof(midi_received_t));
    }
}
