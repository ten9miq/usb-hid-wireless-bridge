#include <cstdio>
#include <cstring>

#include "pico/time.h"
#ifdef HID_HOST_DIAGNOSTICS
#include "hardware/structs/watchdog.h"
#endif

#include "descriptor_parser.h"
#include "dual.h"
#include "interval_override.h"
#include "remapper.h"
#include "serial.h"
#include "tick.h"

#include "dual_b_binary.h"

extern "C" {
#include "adi.h"
#include "flash.h"
#include "swd.h"
}

void send_b_init() {
    b_init_t msg;
    msg.interval_override = interval_override;
    serial_write((uint8_t*) &msg, sizeof(msg));
}

static int64_t tick_timer_callback(alarm_id_t id, void* user_data) {
    set_tick_pending();
    return 0;
}

#ifdef MOUSE_PIPELINE_TRACE
// B only sends these in response to an explicit Feature report request.  Keep
// the latest reply on A so HID GET_REPORT never has to block on the UART.
static mouse_pipeline_trace_record_t remote_trace_record = {};
static mouse_pipeline_trace_info_t remote_trace_info = {};
static bool remote_trace_record_valid = false;
static bool remote_trace_info_valid = false;
#endif

#ifdef HID_HOST_DIAGNOSTICS
static uint64_t next_hid_host_diagnostic_receive_report = 0;
static const uint64_t HID_HOST_DIAGNOSTIC_RECEIVE_REPORT_US = 1000000;
static const uint8_t HID_HOST_DIAGNOSTIC_CONTEXT_COUNT = 16;

struct hid_host_diagnostic_context_t {
    bool occupied;
    hid_host_diagnostic_t diagnostic;
    uint32_t report_count;
    uint32_t last_report_timestamp_ms;
    uint16_t last_report_len;
};

static hid_host_diagnostic_context_t hid_host_diagnostic_contexts[HID_HOST_DIAGNOSTIC_CONTEXT_COUNT];
static hid_host_diagnostic_t latest_hid_host_hcd_snapshot = {};
static bool hid_host_hcd_snapshot_valid = false;
static hid_host_diagnostic_t latest_hid_host_diagnostic_transport_snapshot = {};
static bool hid_host_diagnostic_transport_snapshot_valid = false;
static uint8_t b_diagnostic_runtime_version = 0;
static hid_host_diagnostic_t latest_b_g700_health = {};
static bool b_g700_health_valid = false;
static bool b_flash_loader_status_valid = false;
static uint32_t b_flash_loader_phase = 0;
static uint32_t b_flash_loader_detail = 0;
static uint32_t b_flash_loader_image_length = 0;
static uint8_t b_flash_loader_status_source = 0;

static void set_b_flash_status(uint32_t phase, uint32_t detail = 0) {
    b_flash_loader_status_valid = true;
    b_flash_loader_phase = phase;
    b_flash_loader_detail = detail;
    b_flash_loader_image_length = dual_b_binary_length;
    b_flash_loader_status_source = 1; // WebUI FLASH_B_SIDE command
}

bool make_b_runtime_identity_snapshot(hid_host_diagnostic_t* snapshot) {
    *snapshot = {};
    snapshot->event = HidHostDiagnosticEvent::B_RUNTIME_IDENTITY;
    snapshot->flags = b_diagnostic_runtime_version ? HID_HOST_DIAGNOSTIC_FLAG_SUCCESS : 0;
    snapshot->report_length = dual_b_binary_length;
    snapshot->report_bytes_length = 1;
    snapshot->report_bytes[0] = b_diagnostic_runtime_version;
    return true;
}

bool make_b_g700_health_snapshot(hid_host_diagnostic_t* snapshot) {
    if (!b_g700_health_valid) return false;
    *snapshot = latest_b_g700_health;
    return true;
}

bool make_b_flash_loader_status_snapshot(hid_host_diagnostic_t* snapshot) {
    *snapshot = {};
    snapshot->event = HidHostDiagnosticEvent::B_FLASH_LOADER_STATUS;
    snapshot->flags = b_flash_loader_status_valid && b_flash_loader_phase == 5
        ? HID_HOST_DIAGNOSTIC_FLAG_SUCCESS : 0;
    snapshot->report_length = b_flash_loader_image_length;
    snapshot->report_bytes_length = 8;
    memcpy(snapshot->report_bytes, &b_flash_loader_phase, sizeof(b_flash_loader_phase));
    memcpy(snapshot->report_bytes + 4, &b_flash_loader_detail, sizeof(b_flash_loader_detail));
    snapshot->report_descriptor_bytes_length = 1;
    snapshot->report_descriptor_bytes[0] = b_flash_loader_status_source;
    return true;
}

static hid_host_diagnostic_t* find_hid_host_diagnostic_context(uint8_t dev_addr, uint8_t instance) {
    for (uint8_t i = 0; i < HID_HOST_DIAGNOSTIC_CONTEXT_COUNT; i++) {
        hid_host_diagnostic_context_t& context = hid_host_diagnostic_contexts[i];
        if (context.occupied &&
            (context.diagnostic.dev_addr == dev_addr) &&
            (context.diagnostic.instance == instance)) {
            return &context.diagnostic;
        }
    }

    return NULL;
}

static hid_host_diagnostic_context_t* find_or_register_hid_host_diagnostic_context(
    uint8_t dev_addr, uint8_t instance) {
    hid_host_diagnostic_context_t* empty_context = NULL;
    for (uint8_t i = 0; i < HID_HOST_DIAGNOSTIC_CONTEXT_COUNT; i++) {
        hid_host_diagnostic_context_t& candidate = hid_host_diagnostic_contexts[i];
        if (candidate.occupied &&
            (candidate.diagnostic.dev_addr == dev_addr) &&
            (candidate.diagnostic.instance == instance)) {
            return &candidate;
        }
        if (!candidate.occupied && (empty_context == NULL)) {
            empty_context = &candidate;
        }
    }

    hid_host_diagnostic_context_t* context = empty_context;
    if (context == NULL) {
        context = &hid_host_diagnostic_contexts[instance % HID_HOST_DIAGNOSTIC_CONTEXT_COUNT];
    }
    *context = {};
    context->occupied = true;
    context->diagnostic.dev_addr = dev_addr;
    context->diagnostic.instance = instance;
    return context;
}

static void store_hid_host_diagnostic_context(const hid_host_diagnostic_t& diagnostic) {
    hid_host_diagnostic_context_t* context = find_or_register_hid_host_diagnostic_context(
        diagnostic.dev_addr, diagnostic.instance);
    context->diagnostic = diagnostic;
}

static void record_hid_host_report_received(uint8_t dev_addr, uint8_t instance, uint16_t len) {
    hid_host_diagnostic_context_t* context = find_or_register_hid_host_diagnostic_context(dev_addr, instance);
    context->report_count++;
    context->last_report_len = len;
    context->last_report_timestamp_ms = (uint32_t) (time_us_64() / 1000);
}

static void queue_hid_host_report_counters(const hid_host_diagnostic_t& b_counters) {
    hid_host_diagnostic_t counters = b_counters;
    hid_host_diagnostic_context_t* context = find_or_register_hid_host_diagnostic_context(
        counters.dev_addr, counters.instance);
    // Retain the latest B-side values so the heartbeat-priority A-side
    // snapshot can include them even when the normal diagnostic FIFO is busy.
    context->diagnostic.report_bytes_length = counters.report_bytes_length;
    memcpy(context->diagnostic.report_bytes, counters.report_bytes, sizeof(context->diagnostic.report_bytes));
    uint64_t now_ms = time_us_64() / 1000;
    uint32_t last_age_ms = context->last_report_timestamp_ms == 0
        ? UINT32_MAX
        : (uint32_t) now_ms - context->last_report_timestamp_ms;
    counters.endpoint_packet_size = context->last_report_len;
    counters.report_descriptor_bytes_length = HID_HOST_DIAGNOSTIC_REPORT_COUNTER_BYTES;
    memcpy(counters.report_descriptor_bytes, &context->report_count, sizeof(context->report_count));
    memcpy(counters.report_descriptor_bytes + sizeof(context->report_count), &last_age_ms, sizeof(last_age_ms));
    queue_hid_host_diagnostic((const uint8_t*) &counters, sizeof(counters));
}

bool make_hid_host_diagnostic_counter_snapshot(hid_host_diagnostic_t* snapshot) {
    // This producer is independent of B-side diagnostic queue availability:
    // contexts originate from DEVICE_CONNECTED or REPORT_RECEIVED on A.
    for (uint8_t i = 0; i < HID_HOST_DIAGNOSTIC_CONTEXT_COUNT; i++) {
        hid_host_diagnostic_context_t& context = hid_host_diagnostic_contexts[i];
        if (!context.occupied) {
            continue;
        }

        *snapshot = context.diagnostic;
        snapshot->event = HidHostDiagnosticEvent::REPORT_COUNTERS;
        snapshot->flags |= HID_HOST_DIAGNOSTIC_FLAG_SUCCESS;

        // Preserve B-side counts once delivered over UART. Before that,
        // encode the documented "never" age rather than an ambiguous zero.
        if (snapshot->report_bytes_length < HID_HOST_DIAGNOSTIC_REPORT_COUNTER_BYTES) {
            snapshot->report_bytes_length = HID_HOST_DIAGNOSTIC_REPORT_COUNTER_BYTES;
            memset(snapshot->report_bytes, 0, sizeof(snapshot->report_bytes));
            uint32_t b_last_age_ms = UINT32_MAX;
            memcpy(snapshot->report_bytes + sizeof(uint32_t), &b_last_age_ms, sizeof(b_last_age_ms));
        }

        uint64_t now_ms = time_us_64() / 1000;
        uint32_t a_last_age_ms = context.last_report_timestamp_ms == 0
            ? UINT32_MAX
            : (uint32_t) now_ms - context.last_report_timestamp_ms;
        snapshot->endpoint_packet_size = context.last_report_len;
        snapshot->report_descriptor_bytes_length = HID_HOST_DIAGNOSTIC_REPORT_COUNTER_BYTES;
        memcpy(snapshot->report_descriptor_bytes, &context.report_count, sizeof(context.report_count));
        memcpy(snapshot->report_descriptor_bytes + sizeof(context.report_count), &a_last_age_ms, sizeof(a_last_age_ms));
        return true;
    }

    return false;
}

bool make_hid_host_hcd_snapshot(hid_host_diagnostic_t* snapshot) {
    if (!hid_host_hcd_snapshot_valid) {
        return false;
    }
    *snapshot = latest_hid_host_hcd_snapshot;
    return true;
}

bool make_hid_host_diagnostic_transport_snapshot(hid_host_diagnostic_t* snapshot) {
    if (!hid_host_diagnostic_transport_snapshot_valid) {
        return false;
    }
    *snapshot = latest_hid_host_diagnostic_transport_snapshot;
    return true;
}

static void clear_hid_host_diagnostic_context(uint8_t dev_addr, uint8_t instance) {
    for (uint8_t i = 0; i < HID_HOST_DIAGNOSTIC_CONTEXT_COUNT; i++) {
        hid_host_diagnostic_context_t& context = hid_host_diagnostic_contexts[i];
        if (context.occupied &&
            (context.diagnostic.dev_addr == dev_addr) &&
            (context.diagnostic.instance == instance)) {
            // A device can expose more than one HID interface.  Do not let
            // one unmount discard counters for another still-mounted one.
            context = {};
        }
    }
}

static void queue_hid_host_descriptor_chunks(
    const hid_host_diagnostic_t& diagnostic,
    const uint8_t* report_descriptor,
    uint16_t report_descriptor_length) {
    // DEVICE_CONNECTED is emitted once per fragment.  Each record retains
    // the same device context and total descriptor length, while its offset
    // makes the complete descriptor reconstructable by the WebHID viewer.
    for (uint16_t offset = 0; offset < report_descriptor_length;) {
        hid_host_diagnostic_t fragment = diagnostic;
        uint16_t remaining = report_descriptor_length - offset;
        uint8_t fragment_length =
            (remaining > HID_HOST_DIAGNOSTIC_MAX_DESCRIPTOR_BYTES) ?
            HID_HOST_DIAGNOSTIC_MAX_DESCRIPTOR_BYTES : (uint8_t) remaining;
        fragment.report_descriptor_offset = offset;
        fragment.report_descriptor_bytes_length = fragment_length;
        memcpy(fragment.report_descriptor_bytes, report_descriptor + offset, fragment_length);
        queue_hid_host_diagnostic((const uint8_t*) &fragment, sizeof(fragment));
        offset += fragment_length;
    }
}
#endif

bool serial_callback(const uint8_t* data, uint16_t len) {
    bool ret = false;
    switch ((DualCommand) data[0]) {
        case DualCommand::DEVICE_CONNECTED: {
            device_connected_t* msg = (device_connected_t*) data;
#ifdef HID_HOST_DIAGNOSTICS
            // This command is emitted by descriptor_received_callback on B,
            // so it directly proves the descriptor crossed the A/B transport
            // even when B-side mount diagnostic traffic was not observed.
            if (len >= sizeof(device_connected_t)) {
                hid_host_diagnostic_t diagnostic = {};
                diagnostic.event = HidHostDiagnosticEvent::DEVICE_CONNECTED;
                diagnostic.flags = HID_HOST_DIAGNOSTIC_FLAG_SUCCESS;
                diagnostic.dev_addr = msg->dev_addr;
                diagnostic.instance = msg->interface;
                diagnostic.interface_number = msg->itf_num;
                diagnostic.hub_port = msg->hub_port;
                diagnostic.vid = msg->vid;
                diagnostic.pid = msg->pid;
                diagnostic.report_descriptor_length = len - sizeof(device_connected_t);
                store_hid_host_diagnostic_context(diagnostic);
                queue_hid_host_descriptor_chunks(
                    diagnostic,
                    msg->report_descriptor,
                    diagnostic.report_descriptor_length);
            }
#endif
            parse_descriptor(msg->vid, msg->pid, msg->report_descriptor, len - sizeof(device_connected_t), (uint16_t) (msg->dev_addr << 8) | msg->interface, msg->itf_num);
            device_connected_callback((uint16_t) (msg->dev_addr << 8) | msg->interface, msg->vid, msg->pid, msg->hub_port);
            break;
        }
        case DualCommand::DEVICE_DISCONNECTED: {
            device_disconnected_t* msg = (device_disconnected_t*) data;
#ifdef HID_HOST_DIAGNOSTICS
            clear_hid_host_diagnostic_context(msg->dev_addr, msg->interface);
#endif
            device_disconnected_callback(msg->dev_addr);
            break;
        }
        case DualCommand::REPORT_RECEIVED: {
            report_received_t* msg = (report_received_t*) data;
#ifdef MOUSE_PIPELINE_TRACE
            mouse_pipeline_trace_raw_event(MousePipelineTraceEvent::A_UART_RECEIVED, msg->report,
                                           len - sizeof(report_received_t));
#endif
#ifdef HID_HOST_DIAGNOSTICS
            uint16_t report_length = len - sizeof(report_received_t);
            record_hid_host_report_received(msg->dev_addr, msg->interface, report_length);
            // This arrives only after the B-side report has made it through
            // the UART queue, unlike B-side diagnostic traffic which may be
            // dropped while that queue is busy.
            uint64_t now = time_us_64();
            if ((next_hid_host_diagnostic_receive_report == 0) ||
                (now >= next_hid_host_diagnostic_receive_report)) {
                hid_host_diagnostic_t diagnostic = {};
                hid_host_diagnostic_t* context = find_hid_host_diagnostic_context(msg->dev_addr, msg->interface);
                if (context != NULL) {
                    diagnostic = *context;
                }
                diagnostic.event = HidHostDiagnosticEvent::RECEIVE_REPORT;
                diagnostic.flags = HID_HOST_DIAGNOSTIC_FLAG_SUCCESS;
                diagnostic.dev_addr = msg->dev_addr;
                diagnostic.instance = msg->interface;
                diagnostic.report_length = report_length;
                diagnostic.report_bytes_length =
                    (report_length > HID_HOST_DIAGNOSTIC_MAX_REPORT_BYTES) ?
                    HID_HOST_DIAGNOSTIC_MAX_REPORT_BYTES : report_length;
                memcpy(diagnostic.report_bytes, msg->report, diagnostic.report_bytes_length);
                queue_hid_host_diagnostic((const uint8_t*) &diagnostic, sizeof(diagnostic));
                next_hid_host_diagnostic_receive_report = now + HID_HOST_DIAGNOSTIC_RECEIVE_REPORT_US;
            }
#endif
            handle_received_report(msg->report, len - sizeof(report_received_t), (uint16_t) (msg->dev_addr << 8) | msg->interface);
            ret = true;
            break;
        }
#ifdef MOUSE_PIPELINE_TRACE
        case DualCommand::MOUSE_PIPELINE_TRACE_RESPONSE: {
            if (len != sizeof(mouse_pipeline_trace_response_t)) {
                break;
            }
            const mouse_pipeline_trace_response_t* response =
                (const mouse_pipeline_trace_response_t*) data;
            if (response->want_info) {
                remote_trace_info = response->payload.info;
                remote_trace_info_valid = response->valid;
            } else {
                remote_trace_record = response->payload.record;
                remote_trace_record_valid = response->valid;
            }
            break;
        }
#endif
        case DualCommand::REQUEST_B_INIT:
#ifdef HID_HOST_DIAGNOSTICS
            if (len == 5 && data[1] == 'H' && data[2] == 'H' &&
                data[3] == 'D' && (data[4] == '3' || data[4] == '5')) {
                b_diagnostic_runtime_version = data[4] - '0';
            }
#endif
            send_b_init();
            break;
#ifdef HID_HOST_DIAGNOSTICS
        case DualCommand::B_G700_HEALTH:
            if (len == sizeof(dual_b_g700_health_t)) {
                const dual_b_g700_health_t* probe = (const dual_b_g700_health_t*) data;
                hid_host_diagnostic_t snapshot = {};
                snapshot.event = HidHostDiagnosticEvent::B_G700_ENDPOINT_HEALTH;
                snapshot.flags = probe->dev_addr ? HID_HOST_DIAGNOSTIC_FLAG_SUCCESS : 0;
                snapshot.dev_addr = probe->dev_addr;
                snapshot.instance = probe->instance;
                snapshot.vid = 0x046d;
                snapshot.pid = 0xc07c;
                snapshot.report_length = probe->flags;
                snapshot.report_bytes_length = 8;
                memcpy(snapshot.report_bytes, &probe->callbacks, sizeof(probe->callbacks));
                memcpy(snapshot.report_bytes + 4, &probe->arms, sizeof(probe->arms));
                snapshot.report_descriptor_bytes_length = 4;
                memcpy(snapshot.report_descriptor_bytes, &probe->arm_failures,
                       sizeof(probe->arm_failures));
                latest_b_g700_health = snapshot;
                b_g700_health_valid = true;
            }
            break;
#endif
        case DualCommand::START_OF_FRAME:
            add_alarm_in_us(300, tick_timer_callback, NULL, true);
            break;
        case DualCommand::GET_FEATURE_RESPONSE: {
            get_feature_response_t* msg = (get_feature_response_t*) data;
            handle_get_report_response((uint16_t) (msg->dev_addr << 8) | msg->interface, msg->report_id, msg->report, len - sizeof(get_feature_response_t));
            break;
        }
        case DualCommand::SET_FEATURE_COMPLETE: {
            set_feature_complete_t* msg = (set_feature_complete_t*) data;
            handle_set_report_complete((uint16_t) (msg->dev_addr << 8) | msg->interface, msg->report_id);
            break;
        }
        case DualCommand::MIDI_RECEIVED: {
            midi_received_t* msg = (midi_received_t*) data;
            handle_received_midi(msg->hub_port, msg->msg);
            ret = true;
            break;
        }
#ifdef HID_HOST_DIAGNOSTICS
        case DualCommand::HID_HOST_DIAGNOSTIC:
            if (len != sizeof(dual_hid_host_diagnostic_t)) {
                break;
            }
            {
                const hid_host_diagnostic_t* diagnostic =
                    (const hid_host_diagnostic_t*) (data + sizeof(DualCommand));
                if (diagnostic->event == HidHostDiagnosticEvent::REPORT_COUNTERS) {
                    queue_hid_host_report_counters(*diagnostic);
                } else if (diagnostic->event == HidHostDiagnosticEvent::HCD_SNAPSHOT) {
                    latest_hid_host_hcd_snapshot = *diagnostic;
                    hid_host_hcd_snapshot_valid = true;
                } else if (diagnostic->event == HidHostDiagnosticEvent::B_DIAGNOSTIC_TRANSPORT) {
                    latest_hid_host_diagnostic_transport_snapshot = *diagnostic;
                    hid_host_diagnostic_transport_snapshot_valid = true;
                } else {
                    queue_hid_host_diagnostic(data + sizeof(DualCommand), len - sizeof(DualCommand));
                }
            }
            break;
#endif
        default:
            break;
    }

    return ret;
}

void extra_init() {
#ifdef HID_HOST_DIAGNOSTICS
    b_flash_loader_status_valid = watchdog_hw->scratch[0] == B_FLASH_LOADER_STATUS_MAGIC;
    if (b_flash_loader_status_valid) {
        b_flash_loader_phase = watchdog_hw->scratch[1];
        b_flash_loader_detail = watchdog_hw->scratch[2];
        b_flash_loader_image_length = watchdog_hw->scratch[3];
        b_flash_loader_status_source = 2; // RAM flash loader status
        watchdog_hw->scratch[0] = 0;
    }
#endif
    serial_init();
}

uint32_t get_gpio_valid_pins_mask() {
    return GPIO_VALID_PINS_BASE & ~(
#ifdef PICO_DEFAULT_UART_TX_PIN
                                      (1 << PICO_DEFAULT_UART_TX_PIN) |
#endif
#ifdef PICO_DEFAULT_UART_RX_PIN
                                      (1 << PICO_DEFAULT_UART_RX_PIN) |
#endif
                                      (1 << PIN_SWDIO) |
                                      (1 << PIN_SWDCLK) |
                                      (1 << SERIAL_TX_PIN) |
                                      (1 << SERIAL_RX_PIN) |
                                      (1 << SERIAL_CTS_PIN) |
                                      (1 << SERIAL_RTS_PIN));
}

void read_report(bool* new_report, bool* tick) {
#ifndef DUAL_A_SERIAL_DRAIN_LIMIT
#define DUAL_A_SERIAL_DRAIN_LIMIT 1
#endif

    bool any_input_report = false;
    for (uint8_t i = 0; i < DUAL_A_SERIAL_DRAIN_LIMIT; i++) {
        bool input_report = serial_read(serial_callback);
        any_input_report |= input_report;
        if (!input_report) {
            break;
        }
    }

    *new_report = any_input_report;
    *tick = get_and_clear_tick_pending();
}

void interval_override_updated() {
    restart_t msg;
    serial_write((uint8_t*) &msg, sizeof(msg));
}

#ifdef MOUSE_PIPELINE_TRACE
void mouse_pipeline_trace_remote_control(
    MousePipelineTraceAction action, uint8_t filter_dev_addr, uint8_t filter_instance) {
    mouse_pipeline_trace_control_t msg = {};
    msg.action = action;
    msg.filter_dev_addr = filter_dev_addr;
    msg.filter_instance = filter_instance;
    serial_write((const uint8_t*) &msg, sizeof(msg));
}

void mouse_pipeline_trace_remote_request(bool want_info, uint16_t chronological_index) {
    mouse_pipeline_trace_request_t msg = {};
    msg.want_info = want_info;
    msg.chronological_index = chronological_index;
    if (want_info) {
        remote_trace_info_valid = false;
    } else {
        remote_trace_record_valid = false;
    }
    serial_write((const uint8_t*) &msg, sizeof(msg));
}

bool mouse_pipeline_trace_remote_get_record(mouse_pipeline_trace_record_t* record) {
    if (!remote_trace_record_valid) {
        return false;
    }
    *record = remote_trace_record;
    return true;
}

bool mouse_pipeline_trace_remote_get_info(mouse_pipeline_trace_info_t* info) {
    if (!remote_trace_info_valid) {
        return false;
    }
    *info = remote_trace_info;
    return true;
}
#endif

bool swd_initialized = false;

void flash_b_side() {
#ifdef HID_HOST_DIAGNOSTICS
    set_b_flash_status(1);
#endif
    int rc = SWD_OK;
    if (!swd_initialized) {
        rc = swd_init();
        if (rc != SWD_OK) {
#ifdef HID_HOST_DIAGNOSTICS
            set_b_flash_status(2, rc);
#endif
            return;
        }
        swd_initialized = true;
    }
    rc = dp_init();
    if (rc != SWD_OK) {
#ifdef HID_HOST_DIAGNOSTICS
        set_b_flash_status(2, rc);
#endif
        return;
    }

    rc = core_select(0);
    if (rc == SWD_OK) rc = core_reset_halt();
    if (rc == SWD_OK) rc = core_select(1);
    if (rc == SWD_OK) rc = core_reset_halt();
    if (rc == SWD_OK) rc = core_select(0);
    if (rc != SWD_OK) {
#ifdef HID_HOST_DIAGNOSTICS
        set_b_flash_status(2, rc);
#endif
        return;
    }

#ifdef HID_HOST_DIAGNOSTICS
    set_b_flash_status(3);
#endif
    rc = rp2040_add_flash_bit(0, dual_b_binary, dual_b_binary_length);
    if (rc == SWD_OK) rc = rp2040_add_flash_bit(0xffffffff, NULL, 0);
    if (rc != SWD_OK) {
#ifdef HID_HOST_DIAGNOSTICS
        set_b_flash_status(3, rc);
#endif
        return;
    }

#ifdef HID_HOST_DIAGNOSTICS
    set_b_flash_status(4);
    uint8_t readback[256];
    for (uint32_t offset = 0; offset < dual_b_binary_length; offset += sizeof(readback)) {
        uint32_t count = dual_b_binary_length - offset;
        if (count > sizeof(readback)) count = sizeof(readback);
        rc = mem_read_block(0x10000000u + offset, count, readback);
        if (rc != SWD_OK) {
            set_b_flash_status(4, 0x80000000u | static_cast<uint32_t>(rc));
            return;
        }
        for (uint32_t i = 0; i < count; i++) {
            if (readback[i] != dual_b_binary[offset + i]) {
                set_b_flash_status(4, offset + i);
                return;
            }
        }
    }
    set_b_flash_status(5);
#endif
}

uint8_t buffer[64 + sizeof(send_out_report_t)];

void queue_out_report(uint16_t interface, uint8_t report_id, const uint8_t* report, uint8_t len) {
    // XXX
    // This is called from process_mapping() so we probably shouldn't be writing directly
    // to serial here, as it can block.
    send_out_report_t* msg = (send_out_report_t*) buffer;
    msg->command = DualCommand::SEND_OUT_REPORT;
    msg->dev_addr = interface >> 8;
    msg->interface = interface & 0xFF;
    msg->report_id = report_id;
    memcpy(msg->report, report, len);
    serial_write((uint8_t*) msg, len + sizeof(send_out_report_t));
}

void queue_set_feature_report(uint16_t interface, uint8_t report_id, const uint8_t* report, uint8_t len) {
    set_feature_report_t* msg = (set_feature_report_t*) buffer;
    msg->command = DualCommand::SET_FEATURE_REPORT;
    msg->dev_addr = interface >> 8;
    msg->interface = interface & 0xFF;
    msg->report_id = report_id;
    memcpy(msg->report, report, len);
    serial_write((uint8_t*) msg, len + sizeof(set_feature_report_t));
}

void queue_get_feature_report(uint16_t interface, uint8_t report_id, uint8_t len) {
    get_feature_report_t* msg = (get_feature_report_t*) buffer;
    msg->command = DualCommand::GET_FEATURE_REPORT;
    msg->dev_addr = interface >> 8;
    msg->interface = interface & 0xFF;
    msg->report_id = report_id;
    msg->len = len;
    serial_write((uint8_t*) msg, sizeof(get_feature_report_t));
}

void send_out_report() {
}

void sof_callback() {
}
