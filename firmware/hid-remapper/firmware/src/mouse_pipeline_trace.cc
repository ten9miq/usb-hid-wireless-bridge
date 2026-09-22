#include "mouse_pipeline_trace.h"

#ifdef MOUSE_PIPELINE_TRACE

#include <cstring>

#include "pico/time.h"

#ifndef MOUSE_PIPELINE_TRACE_SOURCE
#error "MOUSE_PIPELINE_TRACE_SOURCE must identify the A or B trace build"
#endif
#ifndef MOUSE_PIPELINE_TRACE_CAPACITY
#error "MOUSE_PIPELINE_TRACE_CAPACITY must identify the trace ring capacity"
#endif

static mouse_pipeline_trace_record_t trace_ring[MOUSE_PIPELINE_TRACE_CAPACITY];
static uint16_t trace_head = 0;
static uint16_t trace_count = 0;
static uint32_t trace_sequence = 0;
static bool trace_armed = false;
static bool trace_frozen = false;
// FF/FF means no endpoint restriction.  The host can arm a specific B-side
// dev_addr/instance once it has identified the RollerMouse interface.
static uint8_t trace_filter_dev_addr = 0xFF;
static uint8_t trace_filter_instance = 0xFF;

void mouse_pipeline_trace_control(MousePipelineTraceAction action, uint8_t filter_dev_addr, uint8_t filter_instance) {
    switch (action) {
        case MousePipelineTraceAction::CLEAR:
            trace_head = 0;
            trace_count = 0;
            trace_sequence = 0;
            trace_frozen = false;
            break;
        case MousePipelineTraceAction::ARM:
            trace_armed = true;
            trace_frozen = false;
            trace_filter_dev_addr = filter_dev_addr;
            trace_filter_instance = filter_instance;
            break;
        case MousePipelineTraceAction::MARK:
            mouse_pipeline_trace_event(MousePipelineTraceEvent::MARK);
            break;
        case MousePipelineTraceAction::FREEZE:
            trace_frozen = true;
            trace_armed = false;
            break;
    }
}

bool mouse_pipeline_trace_matches_endpoint(uint8_t dev_addr, uint8_t instance) {
    return ((trace_filter_dev_addr == 0xFF) || (trace_filter_dev_addr == dev_addr)) &&
        ((trace_filter_instance == 0xFF) || (trace_filter_instance == instance));
}

void mouse_pipeline_trace_event(MousePipelineTraceEvent event, int16_t x, int16_t y,
                                uint16_t report_length, uint16_t queue_depth, uint32_t value) {
    if (!trace_armed || trace_frozen) {
        return;
    }

    mouse_pipeline_trace_record_t& record = trace_ring[trace_head];
    record = {
        .sequence = trace_sequence++,
        .timestamp_us = (uint32_t) time_us_64(),
        .event = event,
        .source = MOUSE_PIPELINE_TRACE_SOURCE,
        .flags = 0,
        .x = x,
        .y = y,
        .report_length = report_length,
        .queue_depth = queue_depth,
        .value = value,
    };
    trace_head = (trace_head + 1) % MOUSE_PIPELINE_TRACE_CAPACITY;
    if (trace_count < MOUSE_PIPELINE_TRACE_CAPACITY) {
        trace_count++;
    }
}

static void mouse_pipeline_trace_raw_event_at(
    MousePipelineTraceEvent event, const uint8_t* data, uint8_t captured, uint32_t timestamp_us) {
    mouse_pipeline_trace_record_t& record = trace_ring[trace_head];
    record = {
        .sequence = trace_sequence++,
        .timestamp_us = timestamp_us,
        .event = event,
        .source = MOUSE_PIPELINE_TRACE_SOURCE,
        .flags = captured,
        .x = 0,
        .y = 0,
        .report_length = 0,
        .queue_depth = 0,
        .value = 0,
    };
    // x through value are exactly twelve contiguous bytes in the packed ABI.
    memcpy(&record.x, data, captured);
    trace_head = (trace_head + 1) % MOUSE_PIPELINE_TRACE_CAPACITY;
    if (trace_count < MOUSE_PIPELINE_TRACE_CAPACITY) {
        trace_count++;
    }
}

void mouse_pipeline_trace_raw_event(MousePipelineTraceEvent event, const uint8_t* data, uint16_t len) {
    if (!trace_armed || trace_frozen || (data == nullptr)) {
        return;
    }

    const uint32_t timestamp_us = (uint32_t) time_us_64();
    const uint8_t captured = (len > 12) ? 12 : (uint8_t) len;
    mouse_pipeline_trace_raw_event_at(event, data, captured, timestamp_us);
    if (len <= 12) {
        return;
    }

    MousePipelineTraceEvent continuation = MousePipelineTraceEvent::A_UART_RECEIVED_CONT;
    if (event == MousePipelineTraceEvent::B_USB_CALLBACK) {
        continuation = MousePipelineTraceEvent::B_USB_CALLBACK_CONT;
    }
    const uint8_t continuation_length = (len - 12 > 4) ? 4 : (uint8_t) (len - 12);
    mouse_pipeline_trace_raw_event_at(continuation, data + 12, continuation_length, timestamp_us);
}

bool mouse_pipeline_trace_get_record(uint16_t chronological_index, mouse_pipeline_trace_record_t* record) {
    if ((record == nullptr) || (chronological_index >= trace_count)) {
        return false;
    }
    uint16_t oldest = (trace_head + MOUSE_PIPELINE_TRACE_CAPACITY - trace_count) % MOUSE_PIPELINE_TRACE_CAPACITY;
    *record = trace_ring[(oldest + chronological_index) % MOUSE_PIPELINE_TRACE_CAPACITY];
    return true;
}

void mouse_pipeline_trace_get_info(mouse_pipeline_trace_info_t* info) {
    if (info == nullptr) {
        return;
    }
    *info = {
        .magic = 0x3154504Du,  // MPT1
        .protocol_version = 1,
        .source = MOUSE_PIPELINE_TRACE_SOURCE,
        .flags = (uint8_t) ((trace_armed ? 1 : 0) | (trace_frozen ? 2 : 0)),
        .capacity = MOUSE_PIPELINE_TRACE_CAPACITY,
        .count = trace_count,
        .next_sequence = trace_sequence,
        .filter_dev_addr = trace_filter_dev_addr,
        .filter_instance = trace_filter_instance,
        .reserved = 0,
    };
}

#endif
