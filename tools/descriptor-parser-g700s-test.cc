#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <unordered_map>

#include "descriptor_parser.h"
#include "hid_report_bits.h"
#include "platform.h"
#include "quirks.h"

void my_mutex_enter(MutexId) {}
void my_mutex_exit(MutexId) {}
void apply_quirks(uint16_t, uint16_t,
                  std::unordered_map<uint8_t, std::unordered_map<uint32_t, usage_def_t>>&,
                  const uint8_t*, int, uint8_t) {}

static int fail(const char* message) {
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

int main() {
    const uint8_t descriptor[] = {
        0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
        0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01,
        0x75, 0x01, 0x95, 0x10, 0x81, 0x02, 0x05, 0x01, 0x16, 0x01,
        0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x02, 0x09, 0x30,
        0x09, 0x31, 0x81, 0x06, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08,
        0x95, 0x01, 0x09, 0x38, 0x81, 0x06, 0x05, 0x0C, 0x0A, 0x38,
        0x02, 0x95, 0x01, 0x81, 0x06, 0xC0, 0xC0,
    };

    std::unordered_map<uint8_t, std::unordered_map<uint32_t, usage_def_t>> inputs;
    std::unordered_map<uint8_t, std::unordered_map<uint32_t, usage_def_t>> outputs;
    std::unordered_map<uint8_t, std::unordered_map<uint32_t, usage_def_t>> features;
    bool has_report_id = false;
    auto sizes = parse_descriptor(inputs, outputs, features, has_report_id, descriptor, sizeof(descriptor));

    if (has_report_id) return fail("unexpected report ID");
    if (sizes[ReportType::INPUT][0] != 8) return fail("unexpected input report size");
    for (uint32_t usage : { 0x00010030u, 0x00010031u }) {
        auto found = inputs[0].find(usage);
        if (found == inputs[0].end()) return fail("missing X/Y usage");
        auto const& def = found->second;
        if ((def.size != 16) || !def.is_relative || def.is_array ||
            (def.logical_minimum != -32767) || (def.logical_maximum != 32767)) {
            return fail("incorrect X/Y definition");
        }
    }

    const uint8_t report[] = { 0x00, 0x00, 0x02, 0x00, 0xFE, 0xFF, 0x00, 0x00 };
    auto read_bits = [](const uint8_t* data, uint16_t bitpos, uint8_t size) {
        uint32_t value = 0;
        for (uint8_t i = 0; i < size; i++) {
            value |= ((data[(bitpos + i) / 8] >> ((bitpos + i) % 8)) & 1u) << i;
        }
        return value;
    };
    int32_t x = sign_extend_hid_value(read_bits(report, inputs[0][0x00010030].bitpos, 16), 16);
    int32_t y = sign_extend_hid_value(read_bits(report, inputs[0][0x00010031].bitpos, 16), 16);
    if ((x != 2) || (y != -2)) return fail("incorrect signed 16-bit X/Y values");
    if ((sign_extend_hid_value(0x7Fu, 8) != 127) ||
        (sign_extend_hid_value(0x80u, 8) != -128)) {
        return fail("8-bit mouse sign extension regression");
    }

    std::cout << "G700s descriptor parser test passed\n";
    return EXIT_SUCCESS;
}
