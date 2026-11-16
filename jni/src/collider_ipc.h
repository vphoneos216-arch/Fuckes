#ifndef IX_COLLIDER_IPC_H
#define IX_COLLIDER_IPC_H

// =====================================================================
// Shared-memory channel between the external process (inx_standalone /
// optimum_bar — owns targeting: which enemy, which collider field, what
// head-collider value to force) and the injected in-process stub
// (collider_stub.so — owns the actual Paradise HWBP arm/disarm call,
// made from inside the game's own PID per the "internal collider lock"
// request).
//
// Why shared memory and not a socket: this updates every frame (up to
// ~70-90 Hz from the aim-lock thread) and only ever carries three
// numbers + a generation counter — a lock-free mmap'd struct is the
// simplest thing that is still correct here, no listen/accept/connect
// lifecycle to manage on either side, and no fd to pass across the
// ptrace injection boundary.
//
// Both sides open the SAME backing file and mmap it MAP_SHARED. Not a
// POSIX named shared memory object (Android's bionic does not ship
// shm_open) — a plain file under a world-readable-by-root, app-
// unreachable path works the same way via mmap(..., MAP_SHARED, fd, 0).
// =====================================================================

#include <atomic>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct IxColliderIpc {
    // Bumped by the writer (external process) every time colliderField /
    // headColliderVal / active change. The stub only re-arms the HWBP
    // when it sees a generation it hasn't applied yet — this is the
    // entire synchronization primitive, no mutex needed for three
    // plain-old-data fields this small.
    std::atomic<uint64_t> generation{0};
    uint64_t colliderField = 0;
    uint64_t headColliderVal = 0;
    // 1 = arm/update the breakpoint with the fields above.
    // 0 = disarm (no active lock this frame).
    uint8_t  active = 0;
    // Set once by collider_stub.so right after it successfully attaches
    // and starts polling, so the external side can tell "stub injected
    // and alive" from "injection never happened / stub crashed" and fall
    // back to the direct in-process call instead of writing into a
    // channel nobody is reading.
    std::atomic<uint8_t> stubAlive{0};
};

inline const char* ix_collider_ipc_path() {
    return "/data/local/tmp/.inx_collider_ipc";
}

// Opens (creating if needed) and maps the shared struct. Returns nullptr
// on any failure — caller treats that as "IPC unavailable, use the
// direct in-process path instead", never as a reason to crash.
inline IxColliderIpc* ix_collider_ipc_open(bool creator) {
    const char* path = ix_collider_ipc_path();
    int flags = creator ? (O_RDWR | O_CREAT) : O_RDWR;
    int fd = open(path, flags, 0600);
    if (fd < 0) return nullptr;

    if (ftruncate(fd, (off_t)sizeof(IxColliderIpc)) != 0) {
        close(fd);
        return nullptr;
    }

    void* mem = mmap(nullptr, sizeof(IxColliderIpc), PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    close(fd); // mapping holds the file open; fd itself not needed after mmap
    if (mem == MAP_FAILED) return nullptr;

    IxColliderIpc* ipc = reinterpret_cast<IxColliderIpc*>(mem);
    if (creator) {
        // Fresh region each run — a stale generation/active flag left
        // over from a previous crashed run must never auto-apply.
        ipc->generation.store(0, std::memory_order_relaxed);
        ipc->colliderField = 0;
        ipc->headColliderVal = 0;
        ipc->active = 0;
        ipc->stubAlive.store(0, std::memory_order_relaxed);
    }
    return ipc;
}

#endif // IX_COLLIDER_IPC_H
