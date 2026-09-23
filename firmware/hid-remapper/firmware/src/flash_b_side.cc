#include <hardware/regs/psm.h>
#include <hardware/sync.h>
#include <hardware/watchdog.h>
#include <cstring>

#include "dual_b_binary.h"
#include "hid_host_diagnostics.h"

extern "C" {
#include "adi.h"
#include "flash.h"
#include "swd.h"
}

void watchdog_reboot_target() {
    // We're using our macros so this will only work if debugger and target are the same platform.

    // hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
    mem_write32(WATCHDOG_BASE + WATCHDOG_CTRL_OFFSET + REG_ALIAS_CLR_BITS, WATCHDOG_CTRL_ENABLE_BITS);

    // watchdog_hw->scratch[4] = 0;
    mem_write32(WATCHDOG_BASE + WATCHDOG_SCRATCH4_OFFSET, 0);

    // hw_set_bits(&psm_hw->wdsel, PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS));
    mem_write32(PSM_BASE + PSM_WDSEL_OFFSET + REG_ALIAS_SET_BITS, PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS));

    // hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_PAUSE_JTAG_BITS | WATCHDOG_CTRL_PAUSE_DBG0_BITS | WATCHDOG_CTRL_PAUSE_DBG1_BITS);
    mem_write32(WATCHDOG_BASE + WATCHDOG_CTRL_OFFSET + REG_ALIAS_CLR_BITS, WATCHDOG_CTRL_PAUSE_JTAG_BITS | WATCHDOG_CTRL_PAUSE_DBG0_BITS | WATCHDOG_CTRL_PAUSE_DBG1_BITS);

    // hw_set_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_TRIGGER_BITS);
    mem_write32(WATCHDOG_BASE + WATCHDOG_CTRL_OFFSET + REG_ALIAS_SET_BITS, WATCHDOG_CTRL_TRIGGER_BITS);
}

static void record_loader_phase(uint32_t phase, uint32_t detail = 0) {
    watchdog_hw->scratch[1] = phase;
    watchdog_hw->scratch[2] = detail;
}

static void flash_and_verify_b_side() {
    // Phase 2: SWD setup, 3: programming, 4: readback verification,
    // 5: every byte verified. Phase 1 means the loader started but did not
    // reach SWD setup. scratch[2] is an SWD error or mismatch byte offset.
    record_loader_phase(2);
    int rc = swd_init();
    if (rc != SWD_OK) { record_loader_phase(2, rc); return; }
    rc = dp_init();
    if (rc != SWD_OK) { record_loader_phase(2, rc); return; }

    rc = core_select(0);
    if (rc != SWD_OK) { record_loader_phase(2, rc); return; }
    rc = core_reset_halt();
    if (rc != SWD_OK) { record_loader_phase(2, rc); return; }
    rc = core_select(1);
    if (rc != SWD_OK) { record_loader_phase(2, rc); return; }
    rc = core_reset_halt();
    if (rc != SWD_OK) { record_loader_phase(2, rc); return; }
    rc = core_select(0);
    if (rc != SWD_OK) { record_loader_phase(2, rc); return; }

    record_loader_phase(3);
    rc = rp2040_add_flash_bit(0, dual_b_binary, dual_b_binary_length);
    if (rc == SWD_OK) rc = rp2040_add_flash_bit(0xffffffff, NULL, 0);
    if (rc != SWD_OK) { record_loader_phase(3, rc); return; }

    record_loader_phase(4);
    uint8_t readback[256];
    for (uint32_t offset = 0; offset < dual_b_binary_length; offset += sizeof(readback)) {
        uint32_t count = dual_b_binary_length - offset;
        if (count > sizeof(readback)) count = sizeof(readback);
        rc = mem_read_block(0x10000000u + offset, count, readback);
        if (rc != SWD_OK) { record_loader_phase(4, 0x80000000u | (uint32_t) rc); return; }
        for (uint32_t i = 0; i < count; i++) {
            if (readback[i] != dual_b_binary[offset + i]) {
                record_loader_phase(4, offset + i);
                return;
            }
        }
    }
    record_loader_phase(5);
}

int main() {
    watchdog_hw->scratch[0] = B_FLASH_LOADER_STATUS_MAGIC;
    watchdog_hw->scratch[3] = dual_b_binary_length;
    record_loader_phase(1);
    flash_and_verify_b_side();

    watchdog_reboot_target();

    watchdog_reboot(0, 0, 0);
    while (true) {
        __wfi();
    }

    return 0;
}
