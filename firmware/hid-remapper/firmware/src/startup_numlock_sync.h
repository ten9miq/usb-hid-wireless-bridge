#ifndef STARTUP_NUMLOCK_SYNC_H
#define STARTUP_NUMLOCK_SYNC_H

#include <cstdint>

// One initialization sequence per firmware boot. USB remounts or keypad
// reconnects after the sequence has started must not trigger it again.
class StartupNumLockSync {
public:
    bool should_start(uint64_t now, bool host_ready, bool keypad_present,
                      bool keyboard_idle, bool queues_idle) {
        if (started_) {
            return false;
        }
        if (!host_ready || !keypad_present) {
            ready_observed_ = false;
            idle_observed_ = false;
            return false;
        }
        if (!ready_observed_) {
            ready_observed_ = true;
            ready_since_ = now;
        }
        if (!keyboard_idle) {
            idle_observed_ = false;
            return false;
        }
        if (!idle_observed_) {
            idle_observed_ = true;
            idle_since_ = now;
        }
        if (!queues_idle || now - ready_since_ < 2000000 ||
            now - idle_since_ < 200000) {
            return false;
        }
        started_ = true;
        return true;
    }

private:
    bool started_ = false;
    bool ready_observed_ = false;
    bool idle_observed_ = false;
    uint64_t ready_since_ = 0;
    uint64_t idle_since_ = 0;
};

#endif
