#ifndef _DUAL_H_
#define _DUAL_H_

#include <stdint.h>

#include "hid_host_diagnostics.h"
#include "mouse_pipeline_trace.h"

enum class DualCommand : uint8_t {
    DEVICE_CONNECTED = 1,
    DEVICE_DISCONNECTED = 2,
    REPORT_RECEIVED = 3,
    REQUEST_B_INIT = 4,
    B_INIT = 5,
    RESTART = 6,
    SEND_OUT_REPORT = 7,
    START_OF_FRAME = 8,
    SET_FEATURE_REPORT = 9,
    GET_FEATURE_REPORT = 10,
    GET_FEATURE_RESPONSE = 11,
    SET_FEATURE_COMPLETE = 12,
    MIDI_RECEIVED = 13,
    HID_HOST_DIAGNOSTIC = 14,
    MOUSE_PIPELINE_TRACE_CONTROL = 15,
    MOUSE_PIPELINE_TRACE_REQUEST = 16,
    MOUSE_PIPELINE_TRACE_RESPONSE = 17,
};

struct __attribute__((packed)) device_connected_t {
    DualCommand command = DualCommand::DEVICE_CONNECTED;
    uint16_t vid;
    uint16_t pid;
    uint8_t dev_addr;
    uint8_t interface;
    uint8_t hub_port;
    uint8_t itf_num;
    uint8_t report_descriptor[0];
};

struct __attribute__((packed)) device_disconnected_t {
    DualCommand command = DualCommand::DEVICE_DISCONNECTED;
    uint8_t dev_addr;
    uint8_t interface;
};

struct __attribute__((packed)) report_received_t {
    DualCommand command = DualCommand::REPORT_RECEIVED;
    uint8_t dev_addr;
    uint8_t interface;
    uint8_t report[0];
};

struct __attribute__((packed)) request_b_init_t {
    DualCommand command = DualCommand::REQUEST_B_INIT;
};

struct __attribute__((packed)) b_init_t {
    DualCommand command = DualCommand::B_INIT;
    uint8_t interval_override;
};

struct __attribute__((packed)) restart_t {
    DualCommand command = DualCommand::RESTART;
};

struct __attribute__((packed)) send_out_report_t {
    DualCommand command = DualCommand::SEND_OUT_REPORT;
    uint8_t dev_addr;
    uint8_t interface;
    uint8_t report_id;
    uint8_t report[0];
};

struct __attribute__((packed)) start_of_frame_t {
    DualCommand command = DualCommand::START_OF_FRAME;
};

struct __attribute__((packed)) set_feature_report_t {
    DualCommand command = DualCommand::SET_FEATURE_REPORT;
    uint8_t dev_addr;
    uint8_t interface;
    uint8_t report_id;
    uint8_t report[0];
};

struct __attribute__((packed)) get_feature_report_t {
    DualCommand command = DualCommand::GET_FEATURE_REPORT;
    uint8_t dev_addr;
    uint8_t interface;
    uint8_t report_id;
    uint8_t len;
};

struct __attribute__((packed)) get_feature_response_t {
    DualCommand command = DualCommand::GET_FEATURE_RESPONSE;
    uint8_t dev_addr;
    uint8_t interface;
    uint8_t report_id;
    uint8_t report[0];
};

struct __attribute__((packed)) set_feature_complete_t {
    DualCommand command = DualCommand::SET_FEATURE_COMPLETE;
    uint8_t dev_addr;
    uint8_t interface;
    uint8_t report_id;
};

struct __attribute__((packed)) midi_received_t {
    DualCommand command = DualCommand::MIDI_RECEIVED;
    uint8_t hub_port;
    uint8_t msg[4];
};

// This is a transport record between the two RP2040s, not an input HID
// report.  A wraps its payload in a separate vendor-defined report ID before
// sending it to the PC when HID_HOST_DIAGNOSTICS is enabled.
struct __attribute__((packed)) dual_hid_host_diagnostic_t {
    DualCommand command = DualCommand::HID_HOST_DIAGNOSTIC;
    hid_host_diagnostic_t diagnostic;
};

// Trace records are never sent while the pipeline is running. A requests a
// frozen record/index explicitly, and B returns one asynchronous cache entry.
struct __attribute__((packed)) mouse_pipeline_trace_control_t {
    DualCommand command = DualCommand::MOUSE_PIPELINE_TRACE_CONTROL;
    MousePipelineTraceAction action;
    uint8_t filter_dev_addr;
    uint8_t filter_instance;
};

struct __attribute__((packed)) mouse_pipeline_trace_request_t {
    DualCommand command = DualCommand::MOUSE_PIPELINE_TRACE_REQUEST;
    uint8_t want_info;
    uint16_t chronological_index;
};

struct __attribute__((packed)) mouse_pipeline_trace_response_t {
    DualCommand command = DualCommand::MOUSE_PIPELINE_TRACE_RESPONSE;
    uint8_t want_info;
    uint8_t valid;
    uint8_t reserved;
    union {
        mouse_pipeline_trace_record_t record;
        mouse_pipeline_trace_info_t info;
    } payload;
};

static_assert(sizeof(mouse_pipeline_trace_control_t) == 4, "trace control protocol changed");
static_assert(sizeof(mouse_pipeline_trace_request_t) == 4, "trace request protocol changed");
static_assert(sizeof(mouse_pipeline_trace_response_t) == 28, "trace response protocol changed");

#endif
