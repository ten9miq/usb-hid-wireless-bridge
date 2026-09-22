#ifndef _MOUSE_PIPELINE_TRACE_H_
#define _MOUSE_PIPELINE_TRACE_H_

#include <stdint.h>

// The trace wire format is deliberately independent of the HID descriptor.
// Keep this packed 24-byte record identical on both RP2040s and in the host
// reader.  timestamp_us is the low 32 bits of the local monotonic clock.
enum class MousePipelineTraceEvent : uint16_t {
    B_USB_CALLBACK = 1,
    B_UART_QUEUED = 2,
    B_UART_PENDING = 3,
    A_UART_RECEIVED = 4,
    A_DECODED_XY = 5,
    A_MAPPING_OUTPUT = 6,
    A_QUEUE_NEW = 7,
    A_QUEUE_AGGREGATE = 8,
    A_USB_SEND_BUSY = 9,
    A_USB_SEND_OK = 10,
    MARK = 11,
    B_USB_CALLBACK_CONT = 12,
    A_UART_RECEIVED_CONT = 13,
};

enum class MousePipelineTraceAction : uint8_t {
    CLEAR = 1,
    ARM = 2,
    MARK = 3,
    FREEZE = 4,
};

enum class MousePipelineTraceSource : uint8_t {
    A = 0,
    B = 1,
};

struct __attribute__((packed)) mouse_pipeline_trace_record_t {
    uint32_t sequence;
    uint32_t timestamp_us;
    MousePipelineTraceEvent event;
    uint8_t source;
    uint8_t flags;
    int16_t x;
    int16_t y;
    uint16_t report_length;
    uint16_t queue_depth;
    uint32_t value;
};

struct __attribute__((packed)) mouse_pipeline_trace_info_t {
    uint32_t magic;
    uint16_t protocol_version;
    uint8_t source;
    uint8_t flags;
    uint16_t capacity;
    uint16_t count;
    uint32_t next_sequence;
    uint8_t filter_dev_addr;
    uint8_t filter_instance;
    uint16_t reserved;
};

static_assert(sizeof(mouse_pipeline_trace_record_t) == 24, "trace record wire size changed");
static_assert(sizeof(mouse_pipeline_trace_info_t) == 20, "trace info wire size changed");

#ifdef MOUSE_PIPELINE_TRACE
void mouse_pipeline_trace_control(MousePipelineTraceAction action, uint8_t filter_dev_addr, uint8_t filter_instance);
void mouse_pipeline_trace_event(MousePipelineTraceEvent event, int16_t x = 0, int16_t y = 0,
                                uint16_t report_length = 0, uint16_t queue_depth = 0, uint32_t value = 0);
// Stores the first twelve report bytes in the x/y/report_length/queue_depth/
// value payload region. For a longer report it immediately follows with one
// four-byte continuation record. flags is the captured length, not an event
// flag.
void mouse_pipeline_trace_raw_event(MousePipelineTraceEvent event, const uint8_t* data, uint16_t len);
bool mouse_pipeline_trace_get_record(uint16_t chronological_index, mouse_pipeline_trace_record_t* record);
void mouse_pipeline_trace_get_info(mouse_pipeline_trace_info_t* info);
bool mouse_pipeline_trace_matches_endpoint(uint8_t dev_addr, uint8_t instance);
#endif

#endif
