#ifndef _HID_HOST_UTILS_H_
#define _HID_HOST_UTILS_H_

#include <stdint.h>

// An HID interface may contain only Output/Feature reports and therefore have
// no interrupt IN endpoint. TinyUSB represents a missing IN endpoint as zero,
// which is also the control endpoint address, so callers must not queue an
// interrupt receive for such an interface.
static inline bool hid_report_descriptor_has_input(const uint8_t* descriptor, uint16_t len) {
    if (descriptor == nullptr) {
        return false;
    }

    uint16_t offset = 0;
    while (offset < len) {
        uint8_t prefix = descriptor[offset++];

        if (prefix == 0xFE) {  // Long item: size byte, tag byte, then data.
            if ((len - offset) < 2) {
                return false;
            }
            uint8_t item_size = descriptor[offset];
            offset += 2;
            if (item_size > (len - offset)) {
                return false;
            }
            offset += item_size;
            continue;
        }

        uint8_t item_size = prefix & 0x03;
        if (item_size == 3) {
            item_size = 4;
        }

        if (item_size > (len - offset)) {
            return false;
        }

        if ((prefix & 0xFC) == 0x80) {  // Main Input item.
            return true;
        }

        offset += item_size;
    }

    return false;
}

#endif
