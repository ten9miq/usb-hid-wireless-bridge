#include <cassert>
#include <cstdio>
#include "../firmware/hid-remapper/firmware/src/startup_numlock_sync.h"

int main() {
    StartupNumLockSync sync;
    assert(!sync.should_start(0, false, false, true, true));
    assert(!sync.should_start(10000000, true, false, true, true));
    assert(!sync.should_start(10000001, true, true, true, true));
    assert(!sync.should_start(12000000, true, true, true, true));
    assert(sync.should_start(12000001, true, true, true, true));
    assert(!sync.should_start(13000000, true, true, true, true));
    assert(!sync.should_start(14000000, false, false, true, true));
    assert(!sync.should_start(18000000, true, true, true, true));

    // USB loss before initialization restarts the settling delay.
    StartupNumLockSync reconnect;
    assert(!reconnect.should_start(0, true, true, true, true));
    assert(!reconnect.should_start(1900000, false, true, true, true));
    assert(!reconnect.should_start(2000000, true, true, true, true));
    assert(!reconnect.should_start(3999999, true, true, true, true));
    assert(reconnect.should_start(4000000, true, true, true, true));

    // Held keys and busy macro/output queues postpone, rather than consume,
    // the once-only initialization. Empty endpoint slots alone are insufficient.
    StartupNumLockSync busy;
    assert(!busy.should_start(0, true, true, false, true));
    assert(!busy.should_start(3000000, true, true, false, true));
    assert(!busy.should_start(4000000, true, true, true, false));
    assert(!busy.should_start(4199999, true, true, true, true));
    assert(!busy.should_start(4200000, true, true, true, false));
    assert(busy.should_start(4200001, true, true, true, true));
    assert(!busy.should_start(6000000, true, true, true, true));

    // A keypad that disappears during the delay cannot trigger initialization.
    StartupNumLockSync unplug;
    assert(!unplug.should_start(0, true, true, true, true));
    assert(!unplug.should_start(2100000, true, false, true, true));
    assert(!unplug.should_start(3000000, true, true, true, true));
    assert(unplug.should_start(5000000, true, true, true, true));
    puts("PASS: startup delay, held-key/queue deferral, disconnect and once-per-boot scheduling");
}
