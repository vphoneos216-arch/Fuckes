// =====================================================================
// collider_stub.so — injected into com.dts.freefiremax by the ptrace
// injector (Scarecrow/ix_inject.h), per the explicit request to move
// just the collider aim-lock HWBP arm/disarm in-process. Everything
// else (ESP, menu, targeting/enemy selection, position reads) stays in
// the external inx_standalone/optimum_bar process exactly as before —
// this file's only job is: once loaded inside the game's own PID,
// own a SEPARATE paradise_driver instance initialized against its own
// getpid(), and arm/disarm the AimCollider write-breakpoint using
// whatever {colliderField, headColliderVal, active} the external
// process last wrote into the shared IxColliderIpc region.
//
// Not built or tested in-game — this is new code, delivered as asked,
// not reported working. The injector that loads this .so is equally
// new and equally unverified; both need a real device run before
// either is trusted.
// =====================================================================

#include "Paradise/paradise_api.h"
#include "collider_ipc.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <unistd.h>

namespace {

std::atomic<bool> g_stop{false};
std::thread*      g_pollThread = nullptr;

void PollLoop() {
    paradise_driver pd;               // connects to the Paradise driver
    pd.initialize(getpid());          // target = this process (we ARE the game now)

    IxColliderIpc* ipc = ix_collider_ipc_open(/*creator=*/false);
    if (!ipc) {
        // External process hasn't created the IPC region yet (or the
        // path is unreachable from in here) — nothing to poll, exit
        // the thread rather than spin on a null pointer.
        return;
    }
    ipc->stubAlive.store(1, std::memory_order_release);

    uint64_t lastApplied = UINT64_MAX;
    bool     armed       = false;

    while (!g_stop.load(std::memory_order_relaxed)) {
        uint64_t gen = ipc->generation.load(std::memory_order_acquire);
        if (gen != lastApplied) {
            lastApplied = gen;

            if (ipc->active) {
                paradise_bp_point_config pt{};
                pt.address  = (uint64_t)ipc->colliderField;
                pt.type     = PARADISE_HWBP_WRITE;
                pt.length   = 8; // pointer = 8 bytes, same as the external path
                pt.scope    = PARADISE_HWBP_ALL_THREADS;
                pt.reserved = 0;

                if (pd.hwbp_set(&pt, 1)) {
                    paradise_bp_record rec{};
                    rec.x0 = (uint64_t)ipc->headColliderVal;
                    pd.hwbp_set_record(0, 0, &rec);
                    armed = true;
                } else {
                    armed = false;
                }
            } else if (armed) {
                pd.hwbp_remove();
                armed = false;
            }
        }
        // ~90Hz poll — matches the aim-lock thread's own update rate
        // closely enough without burning a core spinning tighter.
        std::this_thread::sleep_for(std::chrono::milliseconds(11));
    }

    if (armed) pd.hwbp_remove();
    ipc->stubAlive.store(0, std::memory_order_release);
}

} // namespace

// Exported entry point — this is the symbol the ptrace injector resolves
// via a remote dlsym() call after remote-dlopen'ing this .so, then calls
// directly (no arguments, no return value it needs to inspect: success
// is "stubAlive became 1 in the IPC region", checked from the external
// side, not from this call's return).
extern "C" __attribute__((visibility("default"))) void ix_collider_stub_init() {
    if (g_pollThread) return; // already running — injector must have been called twice
    g_stop.store(false, std::memory_order_relaxed);
    g_pollThread = new std::thread(PollLoop);
    g_pollThread->detach();
}
