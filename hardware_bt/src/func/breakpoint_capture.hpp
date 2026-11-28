#pragma once
#include "breakpoint_targets.hpp"
#include "breakpoint_rewrite.hpp"
#include "silent.hpp"
#include "driver.h"
#include "../other/memory.hpp"
#include "../game/data.hpp"
#include "../ui/cfg.hpp"
#include <algorithm>
#include <cstdarg>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <limits>
#include <string>
#include <vector>
#include <unistd.h>

namespace bp_capture {
struct State {
    bool active = false, armed = false;
    int selection = 0, component = 0, mode = 3, backend = 0, pid = 0;
    uint64_t base = 0, address = 0, hits = 0;
    uint32_t record_count = 0, wind_baseline = 0;
    bool hits_available = false, truncated = false, wind_baseline_known = false;
    double next = 0;
    unsigned generation = 0;
    std::string status = "Stopped";
    std::string export_status;
    std::time_t last_sample = 0;
    std::vector<std::string> rows, events;
    bool hw_enabled = false, hw_applied = false;
    bool fire_check = false, local_firing = false, fire_read_ok = false;
    double hw_next = 0;
    uint64_t hw_target = 0, hw_updates = 0;
    std::array<float,4> hw_values{};
    std::string hw_status = "OFF";

    RewriteSpec rewrite() const {
        return target().kind == AddressKind::Code ? rewrite_spec(target().offset) : rewrite_spec(0);
    }
    void suspend_hw(const char* reason) {
        hw_target = 0;
        // Removing the breakpoint clears every native write mask, including
        // records that may have changed since our last read. Capture can re-arm.
        if (hw_applied && !remove()) {
            hw_status = "Write-mask cleanup failed; retry HW-OFF / Stop";
            return;
        }
        hw_status = reason;
    }
    void set_hw(bool enabled) {
        if (!enabled) {
            hw_enabled = false;
            suspend_hw("OFF");
            event("HW-ON disabled; removing register write masks");
            return;
        }
        if (!driver || driver->selected_backend() != 3 || rewrite().kind == RewriteKind::None) {
            hw_enabled = false;
            hw_status = "Register rewrite unavailable for this driver / selected target";
            return;
        }
        hw_enabled = true; hw_next = 0;
        if (!active) start();
        if (!active) { hw_enabled = false; hw_status = status; return; }
        hw_status = "Waiting for target and first breakpoint record";
        event("HW-ON enabled: " + std::string(rewrite().label));
    }
    void set_fire_check(bool enabled) {
        fire_check = enabled;
        local_firing = false; fire_read_ok = false; hw_next = 0;
        if (enabled) suspend_hw("Fire Check: waiting for confirmed firing");
        event(enabled ? "Fire Check enabled" : "Fire Check disabled");
    }
    bool firing_allows_write() {
        if (!fire_check) return true;
        uint8_t firing = 0;
        const uint64_t local = data::localPlayerAddr.load();
        // This is a boolean field: reading neighboring bytes as an int can
        // falsely report firing when an adjacent flag is nonzero.
        fire_read_ok = driver && valid_pointer(local) && Offsets::isFiring &&
            driver->read(local + Offsets::isFiring, &firing, sizeof(firing));
        local_firing = fire_read_ok && firing != 0;
        if (!local_firing) {
            suspend_hw(fire_read_ok ? "Fire Check: waiting for firing" :
                "Fire Check: firing state unreadable; writes suspended");
        }
        return local_firing;
    }
    static float scalar(const paradise_bp_record& r, unsigned index) {
        float value;
        std::memcpy(&value, reinterpret_cast<const char*>(&r) + offsetof(paradise_bp_record,q0) + index*16, 4);
        return value;
    }
    bool aim_origin(Vector3& origin) const {
        const uint64_t local = data::localPlayerAddr.load();
        if (!valid_pointer(local)) return false;
        uint64_t camera = 0;
        if (driver->read(local + 0x3F0, &camera, sizeof(camera)) && valid_pointer(camera)) {
            origin = GetPosition(camera);
            if (origin != Vector3::Zero() && std::isfinite(origin.x) &&
                std::isfinite(origin.y) && std::isfinite(origin.z)) return true;
        }
        uint64_t info = 0;
        return driver->read(local + 0xE58, &info, sizeof(info)) && valid_pointer(info) &&
            driver->read(info + 0x4C, &origin, sizeof(origin)) && std::isfinite(origin.x) &&
            std::isfinite(origin.y) && std::isfinite(origin.z);
    }
    void update_hw(double now) {
        if (!hw_enabled) {
            if (hw_applied) suspend_hw("OFF");
            return;
        }
        if (!active || !driver || driver->selected_backend() != 3 || proc::pid != pid || proc::lib != base) {
            suspend_hw("Waiting for active Paradise capture"); return;
        }
        // Run before the 30 Hz template throttle so release clears a pending
        // native write template on the next rendered frame.
        if (!firing_allows_write()) return;
        if (now < hw_next) return;
        hw_next = now + 1.0/30.0;
        if (!armed) { hw_status = "Waiting for armed breakpoint"; return; }
        const auto spec = rewrite();
        if (spec.kind == RewriteKind::None) { suspend_hw("Capture-only target"); return; }
        Vector3 world{}, screen{};
        float distance = 0, fov = 0;
        const uint64_t enemy = hwbp::select_target(world, screen, distance, fov);
        if (!enemy || !valid_pointer(data::localPlayerAddr.load())) {
            suspend_hw("Waiting for a visible target inside FOV"); return;
        }
        paradise_bp_record r{};
        uint32_t count = 0;
        if (!driver->hwbp_get_records(0, 0, &r, 1, &count)) {
            suspend_hw("Record read failed; register writes suspended"); return;
        }
        if (count != 1 || r.pc != address) {
            suspend_hw("Waiting for first record at selected execution point"); return;
        }
        Vector3 origin{};
        if (spec.kind == RewriteKind::Direction) {
            // At output-store PCs the record's origin has not been written yet.
            // Use the live source registers of the subsequent origin stores.
            const auto regs = ray_origin_registers(target().offset);
            origin = Vector3(scalar(r,regs[0]), scalar(r,regs[1]), scalar(r,regs[2]));
        } else if (target().offset == 0x72788BC) {
            if (!valid_pointer(r.x0)) { suspend_hw("Camera Transform unavailable"); return; }
            origin = GetPosition(r.x0);
            if (origin == Vector3::Zero()) { suspend_hw("Camera position unavailable"); return; }
        } else if (!aim_origin(origin)) {
            suspend_hw("Aim origin unavailable"); return;
        }
        world.y += cfg::aim::head_bias;
        std::array<float,3> direction{};
        if (!direction_between({origin.x,origin.y,origin.z}, {world.x,world.y,world.z}, direction)) {
            suspend_hw("Target direction invalid"); return;
        }
        std::array<float,4> values{direction[0],direction[1],direction[2],0};
        unsigned components = 3;
        if (spec.kind == RewriteKind::Quaternion) {
            const auto q = Quaternion::Normalize(Quaternion::LookRotation(Vector3(direction[0],direction[1],direction[2])));
            values = {q.x,q.y,q.z,q.w}; components = 4;
        }
        for (unsigned i=0; i<components; ++i) {
            if (!std::isfinite(values[i])) { suspend_hw("Invalid register payload"); return; }
        }
        // All unselected registers remain read-only. Scalar writes use their
        // containing Q register, with upper lanes zeroed as for an ARM64 S write.
        for (int reg=0; reg<PARADISE_REG_COUNT; ++reg)
            PARADISE_BP_SET_MASK(&r, reg, PARADISE_BP_OP_READ);
        for (unsigned i=0; i<components; ++i) {
            const unsigned index = spec.s[i];
            __uint128_t qvalue = 0;
            std::memcpy(&qvalue, &values[i], sizeof(float));
            std::memcpy(reinterpret_cast<char*>(&r) + offsetof(paradise_bp_record,q0)+index*16, &qvalue, sizeof(qvalue));
            PARADISE_BP_SET_MASK(&r, PARADISE_REG_Q0+index, PARADISE_BP_OP_WRITE);
        }
        // Recheck after target selection and memory reads, immediately before
        // enabling the native write template.
        if (!firing_allows_write()) return;
        if (!driver->hwbp_set_record(0,0,r)) {
            // A failed response can still mean the kernel applied the request.
            hw_applied = true;
            hw_enabled = false;
            suspend_hw("Register write rejected; HW-ON disabled"); return;
        }
        hw_applied = true; hw_target = enemy; hw_values = values; ++hw_updates;
        hw_status = "Write template acknowledged (refreshed up to 30 Hz)";
    }

    const Target& target() const { return targets[selection]; }
    static std::string format(const char* fmt, ...) {
        char buffer[512];
        va_list args; va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args); va_end(args);
        return buffer;
    }
    void event(const std::string& text) {
        if (events.size() == 64) events.erase(events.begin());
        events.emplace_back(text);
    }
    void reset_view() {
        if (active) return;
        hw_enabled = false; hw_status = "OFF"; hw_target = 0;
        address = 0; hits = 0; record_count = 0; generation = 0;
        hits_available = false; truncated = false;
        rows.clear(); events.clear();
        last_sample = 0;
        status = "Stopped; press Start Capture";
    }
    uint64_t resolve_address() const {
        if (!driver) return 0;
        return resolve(target(), component, base, data::localPlayerAddr.load(),
            [](uint64_t p, void* out, size_t size) { return driver->read(p, out, size); });
    }
    bool remove() {
        if (!armed) return true;
        if (!driver || driver->selected_backend() != backend) {
            // The old driver's lifecycle owns cleanup when its object is replaced.
            armed = false; active = false;
            hw_applied = false;
            status = "Driver changed; capture stopped";
            event(status);
            return false;
        }
        if (!driver->hwbp_remove()) {
            status = "Removal failed; retry Stop";
            event(status);
            return false;
        }
        armed = false;
        hw_applied = false;
        return true;
    }
    void stop() {
        if (!active) return;
        if (!remove()) return;
        active = false;
        hw_enabled = false; hw_status = "OFF"; hw_target = 0;
        status = "Stopped (last records retained)";
        event(status);
    }
    bool arm(uint64_t resolved) {
        bool ok = false;
        if (backend == 5) {
            const uint32_t types[] = {wind_backend::BP_TYPE_EXEC, wind_backend::BP_TYPE_READ,
                wind_backend::BP_TYPE_WRITE, wind_backend::BP_TYPE_RW};
            wind_backend::HWBP_POINT_CONFIG p{resolved, types[mode], 4};
            ok = driver->hwbp_set(&p, 1);
        } else {
            const uint32_t types[] = {PARADISE_BP_EXECUTE, PARADISE_BP_READ,
                PARADISE_BP_WRITE, PARADISE_BP_READ_WRITE};
            paradise_bp_point_config p{resolved, types[mode], 4, PARADISE_BP_ALL_THREADS, 0};
            ok = driver->hwbp_set(&p, 1);
        }
        if (!ok) { status = "Installation failed; retrying"; return false; }
        armed = true; address = resolved; ++generation;
        rows.clear(); record_count = 0; hits = 0; hits_available = false; truncated = false;
        last_sample = 0;
        event(format("Arm #%u: %s | address=0x%llx | component=%d mode=%d length=4",
            generation, target().label, (unsigned long long)address, component, mode));
        const bool cleared = driver->hwbp_clear_records(0);
        status = cleared ? "Capturing" : "Capturing; driver could not clear old records";
        if (!cleared) event(status);
        wind_backend::WIND_BP_DIAG diag{};
        wind_baseline_known = backend == 5 && driver->bp_diag(&diag);
        wind_baseline = wind_baseline_known ? diag.hwbp_hits : 0;
        return true;
    }
    void start() {
        if (active || !driver) return;
        backend = driver->selected_backend();
        if (backend != 3 && backend != 5) {
            status = 
                "Selected driver does not support capture";
            return;
        }
        if (proc::pid <= 0 || !proc::lib) { status = "Waiting for target / IL2CPP base"; return; }
        if (cfg::aim::visible_enabled) { status = "Disable HWBP Aim before capture"; return; }
        pid = proc::pid; base = proc::lib;
        active = true; next = 0; generation = 0; address = 0;
        rows.clear(); events.clear(); hits = 0; record_count = 0;
        last_sample = 0;
        hits_available = false; truncated = false;
        event(format("Start: PID=%d IL2CPP=0x%llx", pid, (unsigned long long)base));
    }
    static std::string snapshot(const paradise_bp_record& r, uint32_t index, bool paradise_metadata=true, bool syscall_metadata=true) {
        std::string text = format("#%u hits=%llu PC=0x%llx LR/X30=0x%llx SP=0x%llx\nPSTATE=%llx",
            index, (unsigned long long)r.hit_count, (unsigned long long)r.pc,
            (unsigned long long)r.lr, (unsigned long long)r.sp, (unsigned long long)r.pstate);
        if(syscall_metadata)text+=format(" orig_X0=%llx syscall=%d",(unsigned long long)r.orig_x0,r.syscallno);
        if(paradise_metadata)text+=format(" FPSR=%x FPCR=%x",r.fpsr,r.fpcr);
        text += "\n";
        if(paradise_metadata){
            text += "Driver register masks (raw): ";
            for (uint8_t mask : r.mask) text += format("%02x", unsigned(mask));
            text += "\n";
        }
        static_assert(offsetof(paradise_bp_record, x29) - offsetof(paradise_bp_record, x0) == 29*8,
            "Unexpected GPR layout");
        static_assert(offsetof(paradise_bp_record, q31) - offsetof(paradise_bp_record, q0) == 31*16,
            "Unexpected SIMD layout");
        uint64_t gpr[30];
        std::memcpy(gpr, reinterpret_cast<const char*>(&r) + offsetof(paradise_bp_record, x0), sizeof(gpr));
        for (unsigned i = 0; i < 30; i += 3)
            text += format("X%u=%016llx X%u=%016llx X%u=%016llx\n", i,
                (unsigned long long)gpr[i], i+1, (unsigned long long)gpr[i+1], i+2, (unsigned long long)gpr[i+2]);
        text += "S registers (low 32 bits of the corresponding Q register):\n";
        for (unsigned i = 0; i < 32; ++i) {
            uint32_t raw; float value;
            const char* q = reinterpret_cast<const char*>(&r) + offsetof(paradise_bp_record, q0) + i*16;
            std::memcpy(&raw, q, sizeof(raw)); std::memcpy(&value, q, sizeof(value));
            text += format("S%u=0x%08x | f32=%.9g\n", i, unsigned(raw), value);
        }
        for (unsigned i = 0; i < 32; ++i) {
            uint64_t raw[2]; float lanes[4];
            const char* q = reinterpret_cast<const char*>(&r) + offsetof(paradise_bp_record, q0) + i*16;
            std::memcpy(raw, q, sizeof(raw)); std::memcpy(lanes, q, sizeof(lanes));
            text += format("Q%u=%016llx%016llx | f32=(%.7g, %.7g, %.7g, %.7g)\n", i,
                (unsigned long long)raw[1], (unsigned long long)raw[0], lanes[0], lanes[1], lanes[2], lanes[3]);
        }
        return text;
    }
    std::string report() const {
        const std::time_t now = std::time(nullptr);
        std::tm utc{};
        char stamp[40] = "unknown";
        if (gmtime_r(&now, &utc)) std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S UTC", &utc);
        const char* modes[] = {"Execute", "Read", "Write", "Read / Write"};
        const char* components[] = {"X", "Y", "Z", "W"};
        std::string text = "HWBP capture report\nExported: ";
        text += stamp;
        text += format("\nProfile: %s | version: %s\nDriver: %s\nTarget: %s\nComponent: %s | access filter: %s | length: 4 bytes\n",
            versions::selected->label, versions::selected->version, driver_selection::name(backend), target().label,
            target().kind == AddressKind::Code ? "instruction" : components[component], modes[mode]);
        text += format("PID: %d\nIL2CPP base: 0x%llx\nLast armed address: 0x%llx\nArm generation: %u\nStatus: %s\nLast successful sample (Unix seconds): %lld\n",
            pid, (unsigned long long)base, (unsigned long long)address, generation, status.c_str(), (long long)last_sample);
        text += format("Driver records: %u | records included: %u | driver capacity reached: %s\n",
            record_count, unsigned(rows.size()), truncated ? "yes" : "no");
        text += format("HW-ON requested: %s | write template installed: %s | updates acknowledged: %llu\n",
            hw_enabled ? "yes" : "no", hw_applied ? "yes" : "no", (unsigned long long)hw_updates);
        text += format("Fire Check: %s | firing confirmed: %s | firing read succeeded: %s\n",
            fire_check ? "on" : "off", local_firing ? "yes" : "no", fire_read_ok ? "yes" : "no");
        text += "Fire Check is evaluated by the render loop, not atomically inside the driver's breakpoint handler.\n";
        text += "Register mapping: " + std::string(rewrite().label) + "\nHW status: " + hw_status + "\n";
        text += format("Target player: 0x%llx | payload: (%.9g, %.9g, %.9g, %.9g)\n",
            (unsigned long long)hw_target, hw_values[0],hw_values[1],hw_values[2],hw_values[3]);
        text += "Native templates apply to every hit at the chosen PC; no per-hit object condition is available. Upper Q lanes are zeroed for selected scalar-register writes.\n";
        if (hits_available) text += format("%s: %llu\n", backend == 5 ? "Diagnostic hit delta for this arm" :
            "Hit count summed over retained records for this arm", (unsigned long long)hits);
        else text += "Hit count: unavailable\n";
        if (backend == 5) text += "Wind: latest up to 128 fetched access records included. Raw op_mask retained. CPU/SIMD/S register snapshots unavailable in current API.\n";
        else if (backend == 3) text += "Paradise: aggregated driver snapshots, including X0-X29, LR/X30, PC, SP, PSTATE, S0-S31 and Q0-Q31. S[n] is the low 32 bits of Q[n]. No per-hit Read/Write classification.\n";
        else text += "Passive capture unavailable with this backend.\n";
        text += "Records are the latest fetched snapshot for the last arm; earlier arms are not archived. Offsets apply to the analyzed binary.\n\nCAPTURE EVENTS\n";
        for (const auto& line : events) { text += line; text += '\n'; }
        text += "\nCAPTURED RECORDS\n";
        if (rows.empty()) text += "No captured records\n";
        for (const auto& row : rows) { text += row; text += "\n\n"; }
        return text;
    }
    void save_report() { save_report_text(report()); }
    void save_report_text(const std::string& text) {
        // Exclusive creation preserves earlier captures, even within the same second.
        static unsigned serial = 0;
        const char* directories[] = {"/sdcard/Download", "/storage/emulated/0/Download", "/data/local/tmp"};
        std::string errors;
        for (const char* directory : directories) {
            const std::string path = format("%s/hwbp_%lld_%d_%u.txt", directory,
                (long long)std::time(nullptr), int(getpid()), ++serial);
            const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
            if (fd < 0) { errors += format("%s: %s; ", directory, std::strerror(errno)); continue; }
            size_t done = 0;
            int error = 0;
            while (done < text.size()) {
                const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) { error = n < 0 ? errno : EIO; break; }
                done += size_t(n);
            }
            if (!error && ::fsync(fd) != 0) error = errno;
            if (::close(fd) != 0 && !error) error = errno;
            if (error) {
                ::unlink(path.c_str()); // Only the new, incomplete report created above.
                errors += format("%s: %s; ", directory, std::strerror(error));
                continue;
            }
            export_status = "Saved: " + path;
            return;
        }
        export_status = "Save failed: " + errors;
    }
    void poll(double now) {
        if (!active) return;
        if (!driver || driver->selected_backend() != backend || proc::pid != pid || proc::lib != base || cfg::aim::visible_enabled) {
            stop(); return;
        }
        if (now < next) return;
        next = now + 0.5;
        const uint64_t resolved = resolve_address();
        if (!resolved) {
            if (!remove()) return;
            if (status != "Waiting for local player / aiming record") {
                status = "Waiting for local player / aiming record"; event(status);
            }
            return;
        }
        if (!armed || address != resolved) {
            if (!remove() || !arm(resolved)) return;
        }
        std::vector<std::string> updated;
        uint32_t count = 0;
        if (backend == 5) {
            std::vector<wind_backend::HWBP_RECORD> records(WIND_BP_RECORDS_MAX);
            if (!driver->hwbp_get_records(0, 0, records.data(), WIND_BP_RECORDS_MAX, &count)) {
                status = "Record fetch failed; retrying"; return;
            }
            count = std::min(count, uint32_t(WIND_BP_RECORDS_MAX));
            record_count = count; truncated = count == WIND_BP_RECORDS_MAX;
            const uint32_t start = count > 128 ? count - 128 : 0;
            for (uint32_t i = start; i < count; ++i) {
                const auto& r = records[i];
                updated.emplace_back(format("#%u PC=0x%llx address=0x%llx access=%s op_mask=0x%x",
                    i, (unsigned long long)r.trigger_pc, (unsigned long long)r.hit_addr,
                    mode == 0 ? "Execute" : mode == 1 ? "Read" : mode == 2 ? "Write" : "Read/Write", r.op_mask));
            }
            wind_backend::WIND_BP_DIAG diag{};
            hits_available = driver->bp_diag(&diag);
            if (hits_available) {
                if (!wind_baseline_known) {
                    wind_baseline = diag.hwbp_hits;
                    wind_baseline_known = true;
                } else if (diag.hwbp_hits < wind_baseline) {
                    wind_baseline = 0;
                    event("Wind diagnostic hit counter reset");
                }
                hits = diag.hwbp_hits - wind_baseline;
            }
        } else {
            std::vector<paradise_bp_record> records(PARADISE_BP_MAX_RECORDS);
            if (!driver->hwbp_get_records(0, 0, records.data(), PARADISE_BP_MAX_RECORDS, &count)) {
                status = "Record fetch failed; retrying"; return;
            }
            count = std::min(count, uint32_t(PARADISE_BP_MAX_RECORDS));
            record_count = count; truncated = count == PARADISE_BP_MAX_RECORDS;
            hits = 0; hits_available = true;
            for (uint32_t i = 0; i < count; ++i) {
                const auto& r = records[i];
                const auto room = std::numeric_limits<uint64_t>::max() - hits;
                hits += std::min(room, r.hit_count);
                updated.emplace_back(snapshot(r, i));
            }
        }
        rows.swap(updated);
        last_sample = std::time(nullptr);
        status = "Capturing";
    }
};
}
