#include <bsp/board_api.h>
#include <tusb.h>

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

struct pending_hid_report_t {
    bool occupied;
    uint8_t dev_addr;
    uint16_t len;
    uint8_t report[CFG_TUH_HID_EPIN_BUFSIZE];
};

static pending_hid_report_t pending_hid_reports[CFG_TUH_HID];
static uint8_t next_pending_hid_report = 0;

#ifdef HID_HOST_DIAGNOSTICS
static hid_host_diagnostic_t hid_host_diagnostics[CFG_TUH_HID];
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
    if (!serial_write_nonblocking((uint8_t*) &diagnostic, sizeof(diagnostic))) {
        return;
    }

    pending_hid_host_diagnostic_head =
        (pending_hid_host_diagnostic_head + 1) % HID_HOST_DIAGNOSTIC_PENDING_QUEUE_SIZE;
    pending_hid_host_diagnostic_items--;
}

static void send_hid_host_diagnostic(
    HidHostDiagnosticEvent event, uint8_t dev_addr, uint8_t instance, bool success, uint16_t report_length = 0) {
    hid_host_diagnostic_t diagnostic = {};
    if (instance < CFG_TUH_HID) {
        diagnostic = hid_host_diagnostics[instance];
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

static bool arm_hid_report(uint8_t dev_addr, uint8_t instance) {
    bool armed = tuh_hid_receive_report(dev_addr, instance);
    send_hid_host_diagnostic(HidHostDiagnosticEvent::RECEIVE_ARM, dev_addr, instance, armed);
    return armed;
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
    if (instance >= CFG_TUH_HID) {
        return;
    }

    hid_host_diagnostic_t& diagnostic = hid_host_diagnostics[instance];
    diagnostic = {};
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
            diagnostic.enumeration_stage = detail;
            diagnostic.transfer_result = result;
            // value contains the failed setup request code.
            diagnostic.report_length = value;
            break;
        case HidHostDiagnosticEvent::ENDPOINT_OPEN_FAILURE:
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
    uint8_t start = next_pending_hid_report;
    for (uint8_t count = 0; count < CFG_TUH_HID; count++) {
        uint8_t instance = (start + count) % CFG_TUH_HID;
        pending_hid_report_t& pending = pending_hid_reports[instance];
        if (!pending.occupied) {
            continue;
        }
        if (!send_hid_report(pending.dev_addr, instance, pending.report, pending.len)) {
            next_pending_hid_report = instance;
            return;
        }

        pending.occupied = false;
#ifdef HID_HOST_DIAGNOSTICS
        arm_hid_report(pending.dev_addr, instance);
#else
        tuh_hid_receive_report(pending.dev_addr, instance);
#endif
        next_pending_hid_report = (instance + 1) % CFG_TUH_HID;
    }
}

static bool has_pending_hid_reports() {
    for (uint8_t instance = 0; instance < CFG_TUH_HID; instance++) {
        if (pending_hid_reports[instance].occupied) {
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
        // buffer.  Give reports which previously hit backpressure first use
        // of that space before tuh_task() can deliver another report from a
        // high-rate interface.
        serial_read(serial_callback);
        flush_pending_hid_reports();
        tuh_task();
        do_send_out_report();
#ifdef HID_HOST_DIAGNOSTICS
        // Run only after normal report forwarding has drained. A failed
        // nonblocking write leaves the event at the queue head for a later
        // main-loop retry.
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

    if ((instance >= CFG_TUH_HID) || (len > CFG_TUH_HID_EPIN_BUFSIZE)) {
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
        pending_hid_report_t& pending = pending_hid_reports[instance];
        pending.occupied = true;
        pending.dev_addr = dev_addr;
        pending.len = len;
        memcpy(pending.report, report, len);
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

    if ((instance < CFG_TUH_HID) && !pending_hid_reports[instance].occupied) {
#ifdef HID_HOST_DIAGNOSTICS
        arm_hid_report(dev_addr, instance);
#else
        tuh_hid_receive_report(dev_addr, instance);
#endif
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
#ifdef HID_HOST_DIAGNOSTICS
        arm_hid_report(dev_addr, instance);
#else
        tuh_hid_receive_report(dev_addr, instance);
#endif
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
    if (instance < CFG_TUH_HID) {
        hid_host_diagnostics[instance] = {};
    }
#endif
    if ((instance < CFG_TUH_HID) && (pending_hid_reports[instance].dev_addr == dev_addr)) {
        pending_hid_reports[instance].occupied = false;
    }
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
