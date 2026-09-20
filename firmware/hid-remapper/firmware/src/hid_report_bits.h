#ifndef _HID_REPORT_BITS_H_
#define _HID_REPORT_BITS_H_

#include <stdint.h>

inline int32_t sign_extend_hid_value(uint32_t value, uint8_t size) {
    if ((size == 0) || (size >= 32)) {
        return static_cast<int32_t>(value);
    }

    uint32_t sign_bit = 1u << (size - 1);
    if (value & sign_bit) {
        value |= ~0u << size;
    }
    return static_cast<int32_t>(value);
}

#endif
