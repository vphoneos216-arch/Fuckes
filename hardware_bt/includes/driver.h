#pragma once
#include "release_log.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <cstdio>
#include <dirent.h>
#include <unistd.h>
#include <cctype>
#include <ctime>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <sys/utsname.h>
#include <sys/system_properties.h>
#include <sys/prctl.h>
#include <cstdint>
#include <cerrno>
#include <sys/syscall.h>
#include <sys/reboot.h>
#include <linux/input.h>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include "driver_selection.hpp"
#include "module_maps.hpp"
#include "elf_module_resolver.hpp"
#include "native_memory_ops.hpp"
#include "twt_thread_policy.hpp"
#include "../src/game/Offsets.h"
#include <chrono>
#include "kernel.h"
#include <memory>
#include <array>
#include <algorithm>
#include <cmath>
#include "Wind.h"
#include "twt_breakpoint_abi.hpp"
#include "external/paradise/paradise_api.h"

// Compatibility mirror only. Routing uses each instance's immutable choice.
inline int select_driver_option = 0;

// ==================== BING DRIVER (FT / VIP & RT) ====================
enum OPERATIONS_VIP {
    VIP_OP_INIT_KEY    = 0x100,
    VIP_OP_READ_MEM    = 0x101,
    VIP_OP_WRITE_MEM   = 0x102,
    VIP_OP_MODULE_BASE = 0x103,
};

enum OPERATIONS_OLD {
    OLD_OP_INIT_KEY    = 0x800,
    OLD_OP_READ_MEM    = 0x801,
    OLD_OP_WRITE_MEM   = 0x802,
    OLD_OP_MODULE_BASE = 0x803,
};

static constexpr const char* BING_VIP_KEY = "bingByNULL1337";

// ==================== TWT DRIVER STRUCTURES ====================
using twt_request=twt_bp::Request;

#define TWT_MARK 'T'
#define TWT_GET_PID     _IOW(TWT_MARK, 0, twt_request)
#define TWT_MODULE_BASE _IOW(TWT_MARK, 1, twt_request)
#define TWT_READ_MEM    _IOW(TWT_MARK, 4, twt_request)
#define TWT_WRITE_MEM   _IOW(TWT_MARK, 5, twt_request)

// ==================== TOUCH COORDINATE SYSTEM ====================
struct TouchState {
    uint32_t device_min_x, device_max_x;
    uint32_t device_min_y, device_max_y;
    uint32_t screen_width, screen_height;
    int screen_orientation; // 0=normal, 1=90, 2=180, 3=270
    float device_scale_x, device_scale_y;
    bool initialized;

    TouchState() : device_min_x(0), device_max_x(0), device_min_y(0), device_max_y(0),
                   screen_width(0), screen_height(0), screen_orientation(0),
                   device_scale_x(0.0f), device_scale_y(0.0f), initialized(false) {}
};

inline TouchState g_touch_state;

inline bool is_touch_device(int fd) {
    unsigned long abs_bits[(ABS_MAX + sizeof(long) * 8 - 1) / (sizeof(long) * 8)] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0) return false;

    auto test_bit = [](int bit, const unsigned long *array) -> bool {
        return (array[bit / (sizeof(long) * 8)] >> (bit % (sizeof(long) * 8))) & 1;
    };

    return test_bit(ABS_MT_SLOT, abs_bits) &&
           test_bit(ABS_MT_TRACKING_ID, abs_bits) &&
           test_bit(ABS_MT_POSITION_X, abs_bits) &&
           test_bit(ABS_MT_POSITION_Y, abs_bits);
}

inline bool get_touch_device_range(uint32_t *min_x, uint32_t *max_x, uint32_t *min_y, uint32_t *max_y) {
    DIR *input_dir = opendir("/dev/input/");
    if (!input_dir) return false;

    struct dirent *file_entry;
    bool found = false;

    while ((file_entry = readdir(input_dir)) != nullptr) {
        std::string filename = file_entry->d_name;
        if (filename.find("event") != 0) continue;

        std::string path = "/dev/input/" + filename;
        int fd = open(path.c_str(), O_RDONLY);
        if (fd == -1) continue;

        if (is_touch_device(fd)) {
            struct input_absinfo abs_x, abs_y;
            if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_x) == 0 &&
                ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y) == 0) {
                *min_x = abs_x.minimum;
                *max_x = abs_x.maximum;
                *min_y = abs_y.minimum;
                *max_y = abs_y.maximum;
                found = true;
            }
            close(fd);
            break;
        }
        close(fd);
    }
    closedir(input_dir);
    return found;
}

inline void convert_screen_to_physical(int screen_x, int screen_y, int *phys_x, int *phys_y) {
    int physical_x = 0, physical_y = 0;
    switch (g_touch_state.screen_orientation) {
    case 0:
        physical_x = screen_x;
        physical_y = screen_y;
        break;
    case 1:
        physical_x = g_touch_state.screen_height - screen_y;
        physical_y = screen_x;
        break;
    case 3:
        physical_x = screen_y;
        physical_y = g_touch_state.screen_width - screen_x;
        break;
    default:
        physical_x = g_touch_state.screen_height - screen_y;
        physical_y = g_touch_state.screen_width - screen_x;
        break;
    }
    *phys_x = physical_x;
    *phys_y = physical_y;
}

inline void convert_physical_to_device(int phys_x, int phys_y, uint32_t *dev_x, uint32_t *dev_y) {
    if (g_touch_state.device_scale_x > 0.0f && g_touch_state.device_scale_y > 0.0f) {
        *dev_x = (uint32_t)((float)phys_x / g_touch_state.device_scale_x);
        *dev_y = (uint32_t)((float)phys_y / g_touch_state.device_scale_y);
    } else {
        *dev_x = (uint32_t)phys_x;
        *dev_y = (uint32_t)phys_y;
    }

    if (*dev_x < g_touch_state.device_min_x) *dev_x = g_touch_state.device_min_x;
    if (*dev_x > g_touch_state.device_max_x) *dev_x = g_touch_state.device_max_x;
    if (*dev_y < g_touch_state.device_min_y) *dev_y = g_touch_state.device_min_y;
    if (*dev_y > g_touch_state.device_max_y) *dev_y = g_touch_state.device_max_y;
}

inline bool paradise_touch_init(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (!g_touch_state.initialized) {
        uint32_t min_x, max_x, min_y, max_y;
        if (get_touch_device_range(&min_x, &max_x, &min_y, &max_y)) {
            g_touch_state.device_min_x = min_x;
            g_touch_state.device_max_x = max_x;
            g_touch_state.device_min_y = min_y;
            g_touch_state.device_max_y = max_y;

            if (width > height) {
                g_touch_state.screen_width = width;
                g_touch_state.screen_height = height;
            } else {
                g_touch_state.screen_width = height;
                g_touch_state.screen_height = width;
            }

            if (g_touch_state.device_max_x > g_touch_state.device_min_x) {
                g_touch_state.device_scale_x = (float)g_touch_state.screen_height / (float)g_touch_state.device_max_x;
            }
            if (g_touch_state.device_max_y > g_touch_state.device_min_y) {
                g_touch_state.device_scale_y = (float)g_touch_state.screen_width / (float)g_touch_state.device_max_y;
            }
            g_touch_state.initialized = true;
        }
    }
    return g_touch_state.initialized;
}

// ==================== MAIN DRIVER CLASS ====================
class c_driver {
private:
    const int selected_driver_;
    int bing_fd_vip = -1;    // Bing VIP fd (0x100)
    int bing_fd_old = -1;    // Bing RT/OLD fd (0x800)
    std::unique_ptr<paradise_driver> paradise;
    std::unique_ptr<wind_backend::c_wind> wind;
    std::unique_ptr<nb9::Driver> nb9_driver;
    bool nb9_connected = false;
    bool paradise_connected = false;
    int twt_fd = -1;         // TWT driver fd
    pid_t pid = -1;
    std::vector<pid_t> native_thread_ids() const {
        std::vector<pid_t> result;
        char path[64];std::snprintf(path,sizeof(path),"/proc/%d/task",pid);
        DIR* directory=opendir(path);if(!directory)return result;
        while(auto* entry=readdir(directory)){
            char* end=nullptr;long value=std::strtol(entry->d_name,&end,10);
            if(end!=entry->d_name && !*end && value>0 && value<=2147483647L)result.push_back(static_cast<pid_t>(value));
        }
        closedir(directory);std::sort(result.begin(),result.end());return result;
    }

    std::mutex paradise_lock;
    std::mutex m_driver_mutex;
    std::shared_mutex target_io_mutex;
    std::atomic<int64_t> paradise_last_read_log_ms{0};
    std::atomic<int64_t> paradise_last_write_log_ms{0};
    std::atomic<int64_t> paradise_last_module_log_ms{0};
    std::mutex module_resolver_mutex;
    struct ModuleCache {
        pid_t pid = -1;
        uint64_t process_start = 0;
        std::string name;
        elf_module::Chain chain{};
        uintptr_t base = 0;
        bool attempted = false;
        size_t maps_rows=0,examined=0,validated=0;
        std::string reason;
        uintptr_t vendor_hint=0;
        int vendor_error=0;
        std::chrono::steady_clock::time_point next_attempt{};
    } il2cpp_module_cache;
    std::atomic<int64_t> native_last_error_log_ms{0};
    std::string native_self_test_stage_="Not run";
    bool native_io_result(bool writing,bool ok,uintptr_t address,size_t bytes,int error){
        if(!ok&&!error)error=EIO;
        auto& counter=writing?(ok?write_ok_count:write_fail_count):(ok?read_ok_count:read_fail_count);
        const auto count=counter.fetch_add(1,std::memory_order_relaxed)+1;
        (writing?last_write_result:last_read_result).store(ok?0:-error,std::memory_order_relaxed);
        if(!ok){
            if(!writing)last_fail_read_addr.store(address,std::memory_order_relaxed);
            const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if(count<=3||now-native_last_error_log_ms.load()>=5000){
                native_last_error_log_ms.store(now);
                std::fprintf(stderr,"[%s-%s] pid=%d address=0x%llx bytes=%zu errno=%d (%s)\n",
                    driver_selection::name(selected_driver_),writing?"WRITE":"READ",pid,(unsigned long long)address,bytes,error,strerror(error));
            }
            errno=error;
        }
        return ok;
    }

    int bing_active_fd = -1;
    uint32_t bing_active_read_op = 0;
    uint32_t bing_active_write_op = 0;




    static bool diagnostic_log_due(uint64_t fails, std::atomic<int64_t>& last_ms) {
        if constexpr (!TOUCHBASE_DIAGNOSTICS) return false;
        if (fails <= 5) return true;
        if ((fails & 1023) != 0) return false;
        const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        int64_t previous = last_ms.load(std::memory_order_relaxed);
        return now-previous >= 5000 && last_ms.compare_exchange_strong(previous, now, std::memory_order_relaxed);
    }
    typedef struct _COPY_MEMORY {
        pid_t pid;
        uintptr_t addr;
        void* buffer;
        size_t size;
    } COPY_MEMORY, *PCOPY_MEMORY;

    typedef struct _MODULE_BASE {
        pid_t pid;
        char* name;
        uintptr_t base;
    } MODULE_BASE, *PMODULE_BASE;

public:
    // Live R/W telemetry counters
    std::atomic<uint64_t> read_ok_count{0};
    std::atomic<uint64_t> read_fail_count{0};
    std::atomic<uint64_t> write_ok_count{0};
    std::atomic<uint64_t> write_fail_count{0};
    std::atomic<int32_t>  last_read_result{0};
    std::atomic<int32_t>  last_write_result{0};
    std::atomic<uint64_t> last_fail_read_addr{0};
    std::atomic<uint64_t> last_fail_write_addr{0};

private:
    // ==================== BING DRIVER CONNECTION LOGIC ====================
    char *bing_FT() {
        struct stat sb;
        const char *candidates[] = {
            "/dev/ft_driver",
            "/dev/bing_rw",
            "/dev/Bing_rw",
            "/dev/Bing",
            "/dev/bing",
            "/dev/w1a3n3a7n",
        };

        for (const char *path : candidates) {
            if (stat(path, &sb) != 0) continue;
            if (!S_ISCHR(sb.st_mode) && !S_ISBLK(sb.st_mode)) continue;
            char *result = strdup(path);
            if (result) {
                TB_DEBUG_PRINTF("[c_driver] Bing FT node found: %s\n", path);
                return result;
            }
        }
        return nullptr;
    }

    char *bing_qx10() {
        const char *command = "dir=$(ls -l /proc/*/exe 2>/dev/null | grep -E '/data/[^/]* \\(deleted\\)' | sed 's/ /\\n/g' | grep '/proc' | sed 's/\\/[^/]*$//g');if [[ \"$dir\" ]]; then sbwj=$(head -n 1 \"$dir/comm\");open_file=\"\";for file in \"$dir\"/fd/*; do link=$(readlink \"$file\");if [[ \"$link\" == \"/dev/$sbwj (deleted)\" ]]; then open_file=\"$file\";break;fi;done;if [[ -n \"$open_file\" ]]; then nhjd=$(echo \"$open_file\");sbid=$(ls -L -l \"$nhjd\" | sed 's/\\([^,]*\\).*/\\1/' | sed 's/.*root //');echo \"/dev/$sbwj\";rm -rf \"/dev/$sbwj\";mknod \"/dev/$sbwj\" c \"$sbid\" 0;fi;fi;";
        FILE *file = popen(command, "r");
        if (file == nullptr) return nullptr;
        static char result[512];
        if (fgets(result, sizeof(result), file) == nullptr) {
            pclose(file);
            return nullptr;
        }
        pclose(file);
        size_t len = strlen(result);
        if (len > 0 && (result[len - 1] == '\n' || result[len - 1] == '\r')) {
            result[len - 1] = '\0';
        }
        return result;
    }

    bool open_bing_driver() {
        // 1. Check hidden driver -> bing_fd_old (RT)
        char *dev_path3 = bing_qx10();
        if (dev_path3 != nullptr && dev_path3[0] != '\0') {
            bing_fd_old = open(dev_path3, O_RDWR);
            if (bing_fd_old > 0) {
                TB_DEBUG_PRINTF("[c_driver] Bing hidden driver (RT): %s (fd=%d)\n", dev_path3, bing_fd_old);
                unlink(dev_path3);
            }
        }

        // 2. Check FT() candidate nodes -> open VIP & OLD
        char *dev_path_bing = bing_FT();
        if (dev_path_bing != nullptr) {
            if (bing_fd_vip <= 0) {
                bing_fd_vip = open(dev_path_bing, O_RDWR);
                if (bing_fd_vip > 0) {
                    TB_DEBUG_PRINTF("[c_driver] Bing VIP driver (FT): %s (fd=%d)\n", dev_path_bing, bing_fd_vip);
                    char keybuf[0x100] = {0};
                    strncpy(keybuf, BING_VIP_KEY, sizeof(keybuf) - 1);
                    if (ioctl(bing_fd_vip, VIP_OP_INIT_KEY, keybuf) == 0) {
                        TB_DEBUG_PRINTF("[c_driver] Bing VIP init_key success\n");
                    } else {
                        TB_DEBUG_PRINTF("[c_driver] Bing VIP init_key: %s (continuing)\n", strerror(errno));
                    }
                }
            }
            if (bing_fd_old <= 0 && bing_fd_vip <= 0) {
                bing_fd_old = open(dev_path_bing, O_RDWR);
                if (bing_fd_old > 0) {
                    TB_DEBUG_PRINTF("[c_driver] Bing OLD driver (RT): %s (fd=%d)\n", dev_path_bing, bing_fd_old);
                    bing_active_fd = bing_fd_old;
                    bing_active_read_op = OLD_OP_READ_MEM;
                    bing_active_write_op = OLD_OP_WRITE_MEM;
                }
            }
            free(dev_path_bing);
        }

        if (bing_fd_vip > 0 && bing_active_fd <= 0) {
            bing_active_fd = bing_fd_vip;
            bing_active_read_op = VIP_OP_READ_MEM;
            bing_active_write_op = VIP_OP_WRITE_MEM;
        } else if (bing_fd_old > 0 && bing_active_fd <= 0) {
            bing_active_fd = bing_fd_old;
            bing_active_read_op = OLD_OP_READ_MEM;
            bing_active_write_op = OLD_OP_WRITE_MEM;
        }

        return (bing_fd_vip > 0 || bing_fd_old > 0);
    }

    bool bing_read(uintptr_t addr, void *buffer, size_t size) {
        if (this->pid <= 0 || addr == 0 || buffer == nullptr || size == 0) return false;
        COPY_MEMORY cm;
        cm.pid = this->pid;
        cm.addr = addr;
        cm.buffer = buffer;
        cm.size = size;

        if (bing_active_fd > 0 && bing_active_read_op > 0) {
            if (ioctl(bing_active_fd, bing_active_read_op, &cm) == 0) {
                return true;
            }
            if (errno == EAGAIN || errno == EBUSY) {
                sched_yield();
                if (ioctl(bing_active_fd, bing_active_read_op, &cm) == 0) return true;
            }
        }

        if (bing_fd_old > 0 && bing_active_fd != bing_fd_old) {
            if (ioctl(bing_fd_old, OLD_OP_READ_MEM, &cm) == 0) {
                bing_active_fd = bing_fd_old;
                bing_active_read_op = OLD_OP_READ_MEM;
                bing_active_write_op = OLD_OP_WRITE_MEM;
                return true;
            }
        }

        if (bing_fd_vip > 0 && bing_active_fd != bing_fd_vip) {
            if (ioctl(bing_fd_vip, VIP_OP_READ_MEM, &cm) == 0) {
                bing_active_fd = bing_fd_vip;
                bing_active_read_op = VIP_OP_READ_MEM;
                bing_active_write_op = VIP_OP_WRITE_MEM;
                return true;
            }
        }

        return false;
    }

    bool bing_write(uintptr_t addr, const void *buffer, size_t size) {
        if (this->pid <= 0 || addr == 0 || buffer == nullptr || size == 0) return false;
        COPY_MEMORY cm;
        cm.pid = this->pid;
        cm.addr = addr;
        cm.buffer = const_cast<void*>(buffer);
        cm.size = size;

        if (bing_active_fd > 0 && bing_active_write_op > 0) {
            if (ioctl(bing_active_fd, bing_active_write_op, &cm) == 0) {
                return true;
            }
            if (errno == EAGAIN || errno == EBUSY) {
                sched_yield();
                if (ioctl(bing_active_fd, bing_active_write_op, &cm) == 0) return true;
            }
        }

        if (bing_fd_old > 0 && bing_active_fd != bing_fd_old) {
            if (ioctl(bing_fd_old, OLD_OP_WRITE_MEM, &cm) == 0) {
                bing_active_fd = bing_fd_old;
                bing_active_read_op = OLD_OP_READ_MEM;
                bing_active_write_op = OLD_OP_WRITE_MEM;
                return true;
            }
        }

        if (bing_fd_vip > 0 && bing_active_fd != bing_fd_vip) {
            if (ioctl(bing_fd_vip, VIP_OP_WRITE_MEM, &cm) == 0) {
                bing_active_fd = bing_fd_vip;
                bing_active_read_op = VIP_OP_READ_MEM;
                bing_active_write_op = VIP_OP_WRITE_MEM;
                return true;
            }
        }

        return false;
    }

    uintptr_t bing_get_module_base(const char *name) {
        if (this->pid <= 0) return 0;
        MODULE_BASE mb;
        char buf[0x100] = {0};
        strncpy(buf, name, sizeof(buf) - 1);
        mb.pid = this->pid;
        mb.name = buf;
        mb.base = 0;

        if (bing_active_fd == bing_fd_vip && bing_fd_vip > 0) {
            MODULE_BASE mb_vip = mb;
            char buf_vip[0x100] = {0};
            strncpy(buf_vip, name, sizeof(buf_vip) - 1);
            mb_vip.name = buf_vip;
            if (ioctl(bing_fd_vip, VIP_OP_MODULE_BASE, &mb_vip) == 0 && mb_vip.base > 0) {
                return mb_vip.base;
            }
        } else if (bing_fd_old > 0) {
            MODULE_BASE mb_old = mb;
            char buf_old[0x100] = {0};
            strncpy(buf_old, name, sizeof(buf_old) - 1);
            mb_old.name = buf_old;
            if (ioctl(bing_fd_old, OLD_OP_MODULE_BASE, &mb_old) == 0 && mb_old.base > 0) {
                return mb_old.base;
            }
        }

        if (bing_fd_vip > 0 && bing_active_fd != bing_fd_vip) {
            MODULE_BASE mb_vip = mb;
            char buf_vip[0x100] = {0};
            strncpy(buf_vip, name, sizeof(buf_vip) - 1);
            mb_vip.name = buf_vip;
            if (ioctl(bing_fd_vip, VIP_OP_MODULE_BASE, &mb_vip) == 0 && mb_vip.base > 0) {
                return mb_vip.base;
            }
        }

        return parse_maps_module_base(name);
    }

    // Paradise owns its connection and protocol through the supplied public API.
    void paradise_log_io(bool writing, bool ok, int error, uintptr_t addr, size_t size) {
        if constexpr (!TOUCHBASE_DIAGNOSTICS) return;
        auto& counter = writing ? (ok ? write_ok_count : write_fail_count)
                                : (ok ? read_ok_count : read_fail_count);
        const uint64_t count = counter.fetch_add(1, std::memory_order_relaxed) + 1;
        (writing ? last_write_result : last_read_result).store(ok ? 0 : (error ? -error : -EIO), std::memory_order_relaxed);
        if (!ok) (writing ? last_fail_write_addr : last_fail_read_addr).store(addr, std::memory_order_relaxed);
        auto& last_log = writing ? paradise_last_write_log_ms : paradise_last_read_log_ms;
        if ((ok && count == 1) || (!ok && diagnostic_log_due(count, last_log))) {
            std::fprintf(stderr,
                "[PARADISE-RW] %s %s #%llu target=%d caller_pid=%d tid=%ld addr=0x%llx size=%zu connected=%d errno=%d (%s)%s\n",
                writing ? "WRITE" : "READ", ok ? "OK" : "FAIL", (unsigned long long)count,
                pid, getpid(), (long)syscall(SYS_gettid), (unsigned long long)addr, size,
                paradise_connected, error, error ? strerror(error) : "none",
                !ok && !error ? "; API returned false without setting errno" : "");
            std::fflush(stderr);
        }
    }

    bool paradise_read(uintptr_t addr, void *buffer, size_t size) {
        if (!paradise_connected || !buffer || pid <= 0) return false;
        std::lock_guard<std::mutex> lock(paradise_lock);
        const int saved_errno = errno;
        if constexpr (TOUCHBASE_DIAGNOSTICS) errno = 0;
        const bool ok = paradise->read(addr, buffer, size);
        const int error = errno;
        paradise_log_io(false, ok, error, addr, size);
        errno = TOUCHBASE_DIAGNOSTICS && ok ? saved_errno : error;
        return ok;
    }

    bool paradise_write(uintptr_t addr, const void *buffer, size_t size) {
        if (!paradise_connected || !buffer || pid <= 0) return false;
        std::lock_guard<std::mutex> lock(paradise_lock);
        const int saved_errno = errno;
        if constexpr (TOUCHBASE_DIAGNOSTICS) errno = 0;
        const bool ok = paradise->write(addr, const_cast<void*>(buffer), size);
        const int error = errno;
        paradise_log_io(true, ok, error, addr, size);
        errno = TOUCHBASE_DIAGNOSTICS && ok ? saved_errno : error;
        return ok;
    }

    uintptr_t paradise_get_module_base(const char *name) {
        if (!paradise_connected || pid <= 0 || !name) return 0;
        // The vendor lookup may return zero or a different mapping of the file.
        // Resolve game images from their ELF layout and live module anchors.
        // Do this before taking paradise_lock: validation performs API reads.
        if (strcmp(name, "libil2cpp.so") == 0)
            return resolve_game_image(name);
        std::lock_guard<std::mutex> lock(paradise_lock);
        const int saved_errno = errno;
        if constexpr (TOUCHBASE_DIAGNOSTICS) errno = 0;
        uintptr_t base = paradise->get_module_base(name);
        const int error = errno;
        if constexpr (TOUCHBASE_DIAGNOSTICS) {
            const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (now - paradise_last_module_log_ms.load() >= 5000) {
            paradise_last_module_log_ms.store(now);
            std::fprintf(stderr, "[PARADISE-MOD] target=%d name=%s api_base=0x%llx errno=%d (%s) fallback=%s\n",
                pid, name, (unsigned long long)base, error, error ? strerror(error) : "none", base ? "no" : "maps");
            std::fflush(stderr);
            }
        }
        errno = TOUCHBASE_DIAGNOSTICS && base ? saved_errno : error;
        return base ? base : parse_maps_module_base(name);
    }
    static bool process_name_matches(pid_t target_pid,const char* name){
        char path[64],command[256]{};
        std::snprintf(path,sizeof(path),"/proc/%d/cmdline",target_pid);
        FILE* file=fopen(path,"rb");if(!file)return false;
        const auto bytes=fread(command,1,sizeof(command)-1,file);fclose(file);
        return bytes && std::memchr(command,0,bytes) && std::strcmp(command,name)==0;
    }
    static uint64_t process_start_time(pid_t target_pid) {
        char path[64], line[4096];
        snprintf(path, sizeof(path), "/proc/%d/stat", target_pid);
        FILE* file = fopen(path, "r");
        if (!file) return 0;
        bool got = fgets(line, sizeof(line), file) != nullptr;
        fclose(file);
        if (!got) return 0;
        // comm is parenthesized and may contain spaces or closing parentheses.
        char* cursor = strrchr(line, ')');
        if (!cursor) return 0;
        ++cursor;
        for (int field = 3; field < 22; ++field) {
            while (*cursor == ' ') ++cursor;
            if (!*cursor) return 0;
            while (*cursor && *cursor != ' ') ++cursor;
        }
        char* end = nullptr;
        uint64_t start = strtoull(cursor, &end, 10);
        return end != cursor ? start : 0;
    }
    uint64_t native_thread_start(pid_t tid) const {
        char path[80];std::snprintf(path,sizeof(path),"/proc/%d/task/%d",pid,tid);
        return access(path,F_OK)==0?process_start_time(tid):0;
    }
    int native_thread_rank(pid_t tid) const {
        char path[80],name[64]{};std::snprintf(path,sizeof(path),"/proc/%d/task/%d/comm",pid,tid);
        FILE* file=fopen(path,"r");if(!file)return 0;
        const bool read=fgets(name,sizeof(name),file)!=nullptr;fclose(file);if(!read)return 0;
        name[std::strcspn(name,"\r\n")]=0;
        return !strcmp(name,"UnityMain")?100:0; // sampling priority only; actual hits/context decide writes
    }

    uintptr_t resolve_game_image(const char* name) {
        std::shared_lock<std::shared_mutex> target_guard(target_io_mutex,std::defer_lock);
        if(selected_driver_==4 || selected_driver_==6)target_guard.lock();
        std::lock_guard<std::mutex> lock(module_resolver_mutex);
        const auto now = std::chrono::steady_clock::now();
        const uint64_t start = process_start_time(pid);
        ModuleCache& module_cache = il2cpp_module_cache;
        elf_module::Chain chain{Offsets::GameFacade, Offsets::Staticfield, Offsets::CurrentGame};
        if (module_cache.pid != pid || module_cache.process_start != start || module_cache.name != name ||
            module_cache.chain.facade_rva != chain.facade_rva || module_cache.chain.static_offset != chain.static_offset ||
            module_cache.chain.current_game_offset != chain.current_game_offset) {
            module_cache = {};
            module_cache.pid = pid;
            module_cache.process_start = start;
            module_cache.name = name;
            module_cache.chain = chain;
        }
        const bool native=selected_driver_==4||selected_driver_==6;
        if (module_cache.base && (!native || now<module_cache.next_attempt)) return module_cache.base;
        if(native)module_cache.base=0;
        if (module_cache.attempted && now < module_cache.next_attempt) return 0;
        module_cache.attempted = true;
        module_cache.reason.clear();
        if(native){
            std::lock_guard<std::mutex> io_lock(m_driver_mutex);errno=0;
            module_cache.vendor_hint=selected_driver_==4?twt_get_module_base(name):
                (nb9_driver?nb9_driver->get_module_base(name):0);
            module_cache.vendor_error=errno;
        }
        char path[64];
        snprintf(path, sizeof(path), "/proc/%d/maps", pid);
        FILE* file = fopen(path, "r");
        auto maps = elf_module::read_maps(file);
        if (file) fclose(file);
        if ((selected_driver_ == 3 || selected_driver_==4 || selected_driver_==6) && (!maps.complete || maps.rows.empty())) {
            char command[128];
            snprintf(command, sizeof(command), "su -c 'cat /proc/%d/maps 2>/dev/null'", pid);
            file = popen(command, "r");
            maps = elf_module::read_maps(file);
            const int status = file ? pclose(file) : -1;
            if (status != 0) maps.complete = false;
        }
        long page_size = sysconf(_SC_PAGESIZE);
        auto physical_read = [&](uint64_t address, void* buffer, size_t bytes) {
            return this->read(address, buffer, bytes);
        };
        auto log = [&](uint64_t address, const char* reason) {
            module_cache.reason=reason;
            if constexpr (TOUCHBASE_DIAGNOSTICS) {
                std::fprintf(stderr, "[%s-MOD] %s candidate=0x%llx: %s\n",
                    driver_selection::name(selected_driver_), name, (unsigned long long)address, reason);
                std::fflush(stderr);
            }
        };
        auto result = elf_module::resolve(maps, name, page_size > 0 ? page_size : 0, chain, physical_read, log,native);
        module_cache.maps_rows=maps.rows.size();module_cache.examined=result.examined;module_cache.validated=result.valid;
        if(result.incomplete)module_cache.reason="Maps/candidate validation incomplete; base rejected";
        else if(result.valid>1)module_cache.reason="Multiple valid images; base rejected";
        else if(!result.examined)module_cache.reason="No matching library/APK candidates in maps";
        else if(result.valid==1)module_cache.reason="Unique ELF layout and live anchor validated";
        else if(module_cache.reason.empty())module_cache.reason="No readable ELF header matched image candidates";
        auto finished = std::chrono::steady_clock::now();
        module_cache.next_attempt = finished + std::chrono::seconds(2);
        // A restarted process must not inherit a result from an earlier snapshot.
        if (start && process_start_time(pid) == start) module_cache.base = static_cast<uintptr_t>(result.base);
        else TB_DEBUG_PRINTF("[DRIVER-MOD] process identity unavailable or changed; result not cached\n");
        // Without /proc stat identity, a Paradise result may be used for this
        // lookup but must not be retained across a possible process restart.
        const uintptr_t resolved = module_cache.base ? module_cache.base :
            ((selected_driver_ == 3 || selected_driver_==4 || selected_driver_==6) && !start ? static_cast<uintptr_t>(result.base) : 0);
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(finished-now).count();
        if constexpr (TOUCHBASE_DIAGNOSTICS) {
            std::fprintf(stderr, "[%s-MOD] %s pid=%d maps=%zu candidates=%zu validated=%zu complete=%s elapsed=%.3fms base=0x%lx cached=%d\n",
                driver_selection::name(selected_driver_), name, pid, maps.rows.size(), result.examined,
                result.valid, result.incomplete ? "no" : "yes", elapsed/1000.0, (unsigned long)resolved, module_cache.base != 0);
            std::fflush(stderr);
        }
        return resolved;
    }
    // ==================== MAPS MODULE BASE SCANNER ====================
    uintptr_t parse_maps_module_base(const char *name) {
        if (this->pid <= 0 || !name || !name[0]) return 0;
        char maps_path[64];
        snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", this->pid);
        FILE* maps = fopen(maps_path, "r");
        uintptr_t base = module_maps::find_header_mapping(maps, name);
        if (maps) fclose(maps);

        // Preserve the existing su metadata lookup when direct maps is unavailable.
        // Only accept a library basename here, since it is passed through a shell.
        bool safe_name = true;
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(name); *p; ++p) {
            if (!std::isalnum(*p) && *p != '.' && *p != '_' && *p != '-') safe_name = false;
        }
        if (!base && safe_name) {
            static time_t last_su_time = 0;
            time_t now = time(nullptr);
            if (now - last_su_time >= 3) {
                last_su_time = now;
                char command[128];
                snprintf(command, sizeof(command), "su -c 'cat /proc/%d/maps 2>/dev/null'", this->pid);
                maps = popen(command, "r");
                base = module_maps::find_header_mapping(maps, name);
                if (maps) pclose(maps);
            }
        }
        if (!base) {
            static time_t last_missing_log = 0;
            time_t now = time(nullptr);
            if (now - last_missing_log >= 3) {
                last_missing_log = now;
                TB_DEBUG_PRINTF("[DRIVER-MOD] '%s': no readable file-offset-zero mapping (pid=%d); waiting for a valid image base\n", name, this->pid);
                TB_DEBUG_FLUSH();
            }
            return 0;
        }

        // Header residency is independent of the mapping's load address. A failed
        // physical probe must never move the base to another readable segment.
        uint32_t magic = 0;
        bool readable = this->read(base, &magic, sizeof(magic));
        TB_DEBUG_PRINTF("[DRIVER-MOD] '%s' base=0x%lx (pid=%d, file offset=0) | header=%s\n",
               name, (unsigned long)base, this->pid,
               !readable ? "not physically readable; base retained from maps" :
               magic == 0x464C457F ? "ELF magic confirmed" : "ELF magic mismatch; rejecting mapping");
        TB_DEBUG_FLUSH();
        return readable && magic != 0x464C457F ? 0 : base;
    }
    // ==================== TWT DRIVER CONNECTION & OPS ====================
    int twt_get_fd(const char *str) {
        DIR *dir = opendir("/proc/self/fd");
        if (!dir) return -1;
        struct dirent *entry;
        char path[512], link[512];
        int found_fd = -1;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            snprintf(path, sizeof(path), "/proc/self/fd/%s", entry->d_name);
            ssize_t len = readlink(path, link, sizeof(link) - 1);
            if (len == -1) continue;
            link[len] = '\0';
            if (strstr(link, str) != nullptr && strstr(link, "anon_inode:") != nullptr) {
                found_fd = atoi(entry->d_name);
                break;
            }
        }
        closedir(dir);
        return found_fd;
    }

    int open_twt_driver() {
        twt_fd = -1;
        {
            register long _x0 __asm__("x0") = (long)(0x114514);
            register long _x1 __asm__("x1") = (long)(0x1919810);
            register long _x2 __asm__("x2") = (long)(0x2778);
            register long _x3 __asm__("x3") = (long)(&twt_fd);
            register long _nr __asm__("x8") = __NR_reboot;
            __asm__ __volatile__(
                "svc #0"
                : "=r"(_x0)
                : "r"(_x0), "r"(_x1), "r"(_x2), "r"(_x3), "r"(_nr)
                : "memory", "cc"
            );
        }
        TB_DEBUG_PRINTF("[c_driver] TWT reboot syscall -> twt_fd=%d %s\n", twt_fd, twt_fd >= 0 ? "OK" : "TRYING FALLBACK");
        if (twt_fd < 0) {
            TB_DEBUG_PRINTF("[c_driver] TWT syscall failed, scanning /proc/self/fd for TwT_driver...\n");
            twt_fd = twt_get_fd("TwT_driver");
            TB_DEBUG_PRINTF("[c_driver] TWT fallback getFd -> twt_fd=%d %s\n", twt_fd, twt_fd >= 0 ? "OK" : "FAIL");
        }
        return twt_fd;
    }

    bool twt_read(uintptr_t addr, void *buffer, size_t size) {
        if (twt_fd < 0 || this->pid <= 0 || !buffer || size == 0) return false;
        twt_request req = {};
        req.pid = this->pid;
        req.addr = addr & 0xFFFFFFFFFFFF;
        req.buffer = buffer;
        req.size = size;
        return (ioctl(twt_fd, TWT_READ_MEM, &req) == 0);
    }

    bool twt_write(uintptr_t addr, const void *buffer, size_t size) {
        if (twt_fd < 0 || this->pid <= 0 || !buffer || size == 0) return false;
        twt_request req = {};
        req.pid = this->pid;
        req.addr = addr & 0xFFFFFFFFFFFF;
        req.buffer = const_cast<void*>(buffer);
        req.size = size;
        return (ioctl(twt_fd, TWT_WRITE_MEM, &req) == 0);
    }

    pid_t twt_get_pid(const char* name) {
        if (twt_fd < 0 || !name) return 0;
        twt_request req = {};
        char buf[0x100] = {0};
        snprintf(buf, sizeof(buf), "%s", name);
        req.pid = 0;
        req.buffer = buf;
        if (ioctl(twt_fd, TWT_GET_PID, &req) != 0) {
            return 0;
        }
        return req.pid;
    }

    uintptr_t twt_get_module_base(const char* name) {
        if (twt_fd < 0 || this->pid <= 0 || !name) return 0;
        twt_request req = {};
        char buf[0x100] = {0};
        snprintf(buf, sizeof(buf), "%s", name);
        req.pid = this->pid;
        req.addr = 0;
        req.buffer = buf;
        if (ioctl(twt_fd, TWT_MODULE_BASE, &req) != 0) {
            return 0;
        }
        return req.addr;
    }

public:
    c_driver():selected_driver_(driver_selection::prompt(stdin,stdout)) {
        if(selected_driver_==0){
            std::fprintf(stderr,"No driver selected; exiting without connecting to a backend.\n");
            std::exit(EXIT_FAILURE);
        }
        select_driver_option=selected_driver_; // compatibility display, never dispatch
        if (selected_driver_ == 2) {
            if (open_bing_driver()) {
                TB_DEBUG_PRINTF("\033[42m\033[37m[+] Bing Driver ready (VIP fd=%d, OLD fd=%d)\033[0m\n", bing_fd_vip, bing_fd_old);
            } else {
                std::fprintf(stderr, "\033[41m\033[37m[-] Failed to open Bing driver!\033[0m\n");
                exit(0);
            }
        } else if (selected_driver_ == 3) {
            if constexpr (TOUCHBASE_DIAGNOSTICS) errno = 0;
            paradise = std::make_unique<paradise_driver>();
            const int constructor_error = errno;
            int alive = 0;
            if constexpr (TOUCHBASE_DIAGNOSTICS) errno = 0;
            const bool alive_ok = paradise->is_process_alive(getpid(), &alive);
            const int alive_error = errno;
            paradise_connected = alive_ok && alive == 1;
            if constexpr (TOUCHBASE_DIAGNOSTICS) {
                std::fprintf(stderr, "[PARADISE-CONNECT] caller_pid=%d uid=%u euid=%u ctor_errno=%d alive_ok=%d alive=%d alive_errno=%d (%s)\n",
                    getpid(), (unsigned)getuid(), (unsigned)geteuid(), constructor_error,
                    alive_ok, alive, alive_error, alive_error ? strerror(alive_error) : "none");
                if (paradise_connected) {
                    // Separate transport/read-protocol problems from target addresses.
                    uint64_t expected = 0x5041524144495345ULL, actual = 0;
                    paradise->initialize(getpid());
                    errno = 0;
                    const bool self_ok = paradise->read(reinterpret_cast<uintptr_t>(&expected), &actual, sizeof(actual));
                    const int self_error = errno;
                    std::fprintf(stderr, "[PARADISE-SELF] read_ok=%d value_matches=%d errno=%d (%s)\n",
                        self_ok, self_ok && actual == expected, self_error, self_error ? strerror(self_error) : "none");
                    paradise->initialize(-1);
                }
                std::fflush(stderr);
            }
            if (!paradise_connected) {
                std::fprintf(stderr, "Failed to connect to Paradise driver. Ensure its kernel module is loaded.\n");
                std::exit(EXIT_FAILURE);
            }
            TB_DEBUG_PRINTF("[+] Paradise API connected\n");
        } else if (selected_driver_ == 4) {
            if (open_twt_driver() >= 0) {
                this->pid=getpid();
                volatile uint64_t scratch=0x54575453454c4631ULL;
                uint64_t actual=0;const uint64_t replacement=scratch^0x1234ULL;
                errno=0;
                const bool read_ok=twt_read(reinterpret_cast<uintptr_t>(&scratch),&actual,sizeof(actual))&&actual==scratch;
                const bool write_ok=read_ok&&twt_write(reinterpret_cast<uintptr_t>(&scratch),&replacement,sizeof(replacement))&&scratch==replacement;
                const int error=errno;this->pid=-1;
                std::fprintf(stderr,"TWT self-test: read=%d write=%d errno=%d (%s)\n",read_ok,write_ok,error,strerror(error));
                if(!read_ok||!write_ok){close(twt_fd);twt_fd=-1;std::exit(EXIT_FAILURE);}
                this->pid=getpid();std::string stage;errno=0;
                const bool pages_ok=native_memory::self_check(static_cast<size_t>(sysconf(_SC_PAGESIZE)),
                    [&](uintptr_t at,void* out,size_t bytes){return twt_read(at,out,bytes);},
                    [&](uintptr_t at,void* in,size_t bytes){return twt_write(at,in,bytes);},stage);
                const int pages_error=errno;this->pid=-1;
                native_self_test_stage_=stage;
                std::fprintf(stderr,"TWT self-test: %s | ok=%d errno=%d\n",stage.c_str(),pages_ok,pages_error);
                if(!pages_ok){close(twt_fd);twt_fd=-1;std::exit(EXIT_FAILURE);}
                TB_DEBUG_PRINTF("\033[42m\033[37m[+] TWT Driver connected successfully (fd=%d)\033[0m\n", twt_fd);
                TB_DEBUG_PRINTF("\033[32m[+] TWT ioctl protocol ready (READ/WRITE/PID/MODULE)\033[0m\n");
            } else {
                std::fprintf(stderr, "\033[41m\033[37m[!] Failed to connect to TWT driver! Make sure TWT kernel module is loaded.\033[0m\n");
                exit(0);
            }
        } else if (selected_driver_ == 5) {
            wind = std::make_unique<wind_backend::c_wind>(0);
            if (!wind->ok()) {
                std::fprintf(stderr, "WIND has no open Netlink transport yet; actual-target discovery will also try device transports.\n");
                wind->print_transport_errors();
            }
            TB_DEBUG_PRINTF("[+] WIND transport opened (netlink unit=%d); target binding follows PID discovery\n", wind->netlink_unit());
        } else if (selected_driver_ == 6) {
            nb9_driver = std::make_unique<nb9::Driver>();
            nb9_driver->initialize(getpid());
            volatile uint64_t scratch = 0x4e423953454c4631ULL;
            uint64_t actual = 0;
            const uint64_t replacement = scratch ^ 0x1234ULL;
            errno = 0;
            const bool read_ok = nb9_driver->read(reinterpret_cast<uintptr_t>(&scratch), &actual, sizeof(actual)) && actual == scratch;
            const bool write_ok = read_ok && nb9_driver->write(reinterpret_cast<uintptr_t>(&scratch), &replacement, sizeof(replacement)) && scratch == replacement;
            nb9_connected = read_ok && write_ok;
            const int error = errno;
            nb9_driver->initialize(-1);
            std::fprintf(stderr, "NB9 self-test: read=%d write=%d errno=%d (%s)\n", read_ok, write_ok, error, strerror(error));
            if (!nb9_connected) std::exit(EXIT_FAILURE);
            nb9_driver->initialize(getpid());std::string stage;errno=0;
            nb9_connected=native_memory::self_check(static_cast<size_t>(sysconf(_SC_PAGESIZE)),
                [&](uintptr_t at,void* out,size_t bytes){return nb9_driver->read(at,out,bytes);},
                [&](uintptr_t at,void* in,size_t bytes){return nb9_driver->write(at,in,bytes);},stage);
            const int pages_error=errno;nb9_driver->initialize(-1);
            native_self_test_stage_=stage;
            std::fprintf(stderr,"NB9 self-test: %s | ok=%d errno=%d\n",stage.c_str(),nb9_connected,pages_error);
            if(!nb9_connected)std::exit(EXIT_FAILURE);
        } else {
            std::fprintf(stderr, "\033[41m\033[37mInvalid option!\033[0m\n");
            exit(0);
        }
        system("clear");
        std::printf("Selected driver: %s\n",driver_selection::name(selected_driver_));
        TB_DEBUG_FLUSH();
    }

    ~c_driver() {
        if(!release_breakpoints())std::fprintf(stderr,"Driver shutdown: breakpoint cleanup was not acknowledged.\n");
        if (bing_fd_vip > 0) close(bing_fd_vip);
        if (bing_fd_old > 0) close(bing_fd_old);

        if (twt_fd >= 0) close(twt_fd);
    }

    int selected_backend() const {return selected_driver_;}
    std::string connection_diagnostics(){
        std::lock_guard<std::mutex> lock(module_resolver_mutex);
        char text[2048];
        std::snprintf(text,sizeof(text),"Backend=%s PID=%d\nNative RW page requests: read_ok=%llu read_fail=%llu write_ok=%llu write_fail=%llu last_read=%d last_write=%d last_failed_read=0x%llx\nIL2CPP: vendor_hint=0x%llx errno=%d validated_base=0x%llx maps=%zu candidates=%zu validated=%zu reason=%s\n",
            driver_selection::name(selected_driver_),pid,(unsigned long long)read_ok_count.load(),(unsigned long long)read_fail_count.load(),
            (unsigned long long)write_ok_count.load(),(unsigned long long)write_fail_count.load(),last_read_result.load(),last_write_result.load(),(unsigned long long)last_fail_read_addr.load(),
            (unsigned long long)il2cpp_module_cache.vendor_hint,il2cpp_module_cache.vendor_error,(unsigned long long)il2cpp_module_cache.base,il2cpp_module_cache.maps_rows,il2cpp_module_cache.examined,il2cpp_module_cache.validated,il2cpp_module_cache.reason.c_str());
        return std::string(text)+"Startup self-test: "+native_self_test_stage_+"\n";
    }
    bool is_connected() const {
        switch(selected_driver_){
            case 2:return bing_fd_vip>=0||bing_fd_old>=0;
            case 3:return paradise_connected;
            case 4:return twt_fd>=0;
            case 5:return wind && wind->ok();
            case 6:return nb9_connected;
            default:return false;
        }
    }

    void initialize(pid_t target_pid) {
        if(selected_driver_==4 || selected_driver_==6){
            std::unique_lock<std::shared_mutex> target_guard(target_io_mutex);
            // Same lock order as memory operations; native BP calls only take
            // paradise_lock. Cleanup and target publication form one operation.
            std::lock_guard<std::mutex> io_lock(m_driver_mutex);
            std::lock_guard<std::mutex> bp_lock(paradise_lock);
            if(this->pid!=target_pid && !native_remove_locked()){
                std::fprintf(stderr,"Cannot change target: native HWBP cleanup failed.\n");return;
            }
            if(selected_driver_==6 && nb9_driver)nb9_driver->initialize(target_pid);
            this->pid=target_pid;return;
        }
        if (this->pid != target_pid && !release_breakpoints()) {
            std::fprintf(stderr, "Cannot change target: failed to remove active breakpoints.\n");
            return;
        }
        if (selected_driver_ == 3 && paradise_connected) {
            std::lock_guard<std::mutex> lock(paradise_lock);
            paradise->initialize(target_pid);
            if constexpr (TOUCHBASE_DIAGNOSTICS) {
                if (this->pid != target_pid) {
                    read_ok_count.store(0); read_fail_count.store(0);
                    write_ok_count.store(0); write_fail_count.store(0);
                    paradise_last_read_log_ms.store(0); paradise_last_write_log_ms.store(0);
                int alive = 0;
                errno = 0;
                const bool ok = paradise->is_process_alive(target_pid, &alive);
                const int error = errno;
                std::fprintf(stderr, "[PARADISE-TARGET] previous=%d target=%d initialize=called (void API; no acknowledgement) alive_ok=%d alive=%d errno=%d (%s)\n",
                    this->pid, target_pid, ok, alive, error, error ? strerror(error) : "none");
                std::fflush(stderr);
                errno = error;
                }
            }
        } else if (selected_driver_ == 5) {
            if (target_pid <= 0 || !wind || !wind->initialize(target_pid)) {
                this->pid = -1;
                if (target_pid <= 0 || !wind)
                    std::fprintf(stderr, "WIND target unavailable: PID %d\n", target_pid);
                return;
            }
        }
        this->pid = target_pid;
    }

    pid_t current_pid() const {
        return this->pid;
    }

    bool init_key(const char* key) {
        if (bing_fd_vip <= 0) return false;
        char buf[0x100] = {0};
        strncpy(buf, key, sizeof(buf) - 1);
        return ioctl(bing_fd_vip, VIP_OP_INIT_KEY, buf) == 0;
    }

    bool raw_read(uintptr_t addr, void *buffer, size_t size) {
        if (this->pid <= 0 || addr == 0 || buffer == nullptr || size == 0) return false;
        if(selected_driver_==4||selected_driver_==6){uintptr_t normalized=0;if(!native_memory::range(addr,size,normalized)){errno=EINVAL;return false;}addr=normalized;}

        if (selected_driver_ == 2) { // Bing Driver
            return bing_read(addr, buffer, size);
        } else if (selected_driver_ == 3) { // Paradise Driver
            return paradise_read(addr, buffer, size);
        } else if (selected_driver_ == 4) { // TWT Driver
            errno=0;const bool ok=twt_read(addr,buffer,size);const int error=errno;
            return native_io_result(false,ok,addr,size,error);
        } else if (selected_driver_ == 5) {
            return wind && wind->is_bound() && wind->read(addr, buffer, size);
        } else if (selected_driver_ == 6) {
            errno=0;const bool ok=nb9_connected && nb9_driver && nb9_driver->read(addr,buffer,size);const int error=errno;
            return native_io_result(false,ok,addr,size,error);
        }
        return false;
    }

    bool raw_write(uintptr_t addr, const void *buffer, size_t size) {
        if (this->pid <= 0 || addr == 0 || buffer == nullptr || size == 0) return false;
        if(selected_driver_==4||selected_driver_==6){uintptr_t normalized=0;if(!native_memory::range(addr,size,normalized)){errno=EINVAL;return false;}addr=normalized;}

        if (selected_driver_ == 2) { // Bing Driver
            return bing_write(addr, buffer, size);
        } else if (selected_driver_ == 3) { // Paradise Driver
            return paradise_write(addr, buffer, size);
        } else if (selected_driver_ == 4) { // TWT Driver
            errno=0;const bool ok=twt_write(addr,buffer,size);const int error=errno;
            return native_io_result(true,ok,addr,size,error);
        } else if (selected_driver_ == 5) {
            return wind && wind->is_bound() && wind->write(addr, buffer, size);
        } else if (selected_driver_ == 6) {
            errno=0;const bool ok=nb9_connected && nb9_driver && nb9_driver->write(addr,buffer,size);const int error=errno;
            return native_io_result(true,ok,addr,size,error);
        }
        return false;
    }

    bool read(uintptr_t addr, void *buffer, size_t size) {

        std::lock_guard<std::mutex> lock(this->m_driver_mutex);
        if (this->pid <= 0 || addr < 0x1000 || buffer == nullptr || size == 0) return false;

        if(selected_driver_==4 || selected_driver_==6){
            const long page=sysconf(_SC_PAGESIZE);
            return native_memory::pages(addr,buffer,size,page>0?size_t(page):4096,false,
                [&](uintptr_t at,void* out,size_t bytes){return raw_read(at,out,bytes);});
        }

        uint8_t *out = reinterpret_cast<uint8_t*>(buffer);
        size_t done = 0;
        while (done < size) {
            uintptr_t cur_addr = addr + done;
            size_t page_offset = cur_addr & 0xFFF;
            size_t page_remaining = 0x1000 - page_offset;
            size_t chunk = (size - done < page_remaining) ? (size - done) : page_remaining;

            if (!raw_read(cur_addr, out + done, chunk)) {
                return false;
            }
            done += chunk;
        }
        return true;
    }

    bool Read(uintptr_t addr, void *buffer, size_t size) {
        return this->read(addr, buffer, size);
    }

    bool write(uintptr_t addr, const void *buffer, size_t size) {

        std::lock_guard<std::mutex> lock(this->m_driver_mutex);
        if (this->pid <= 0 || addr < 0x1000 || buffer == nullptr || size == 0) return false;

        if(selected_driver_==4 || selected_driver_==6){
            const long page=sysconf(_SC_PAGESIZE);
            return native_memory::pages(addr,const_cast<void*>(buffer),size,page>0?size_t(page):4096,true,
                [&](uintptr_t at,void* in,size_t bytes){return raw_write(at,in,bytes);});
        }

        const uint8_t *in = reinterpret_cast<const uint8_t*>(buffer);
        size_t done = 0;
        while (done < size) {
            uintptr_t cur_addr = addr + done;
            size_t page_offset = cur_addr & 0xFFF;
            size_t page_remaining = 0x1000 - page_offset;
            size_t chunk = (size - done < page_remaining) ? (size - done) : page_remaining;

            if (!raw_write(cur_addr, in + done, chunk)) {
                return false;
            }
            done += chunk;
        }
        return true;
    }

    bool read_safe(uintptr_t addr, void *buffer, size_t size) {
        return this->read(addr, buffer, size);
    }

    bool write_safe(uintptr_t addr, const void *buffer, size_t size) {
        return this->write(addr, buffer, size);
    }

    template <typename T>
    T read(uintptr_t addr) {
        T res{};
        this->read(addr, &res, sizeof(T));
        return res;
    }

    template <typename T>
    T Read(uintptr_t addr) {
        return this->read<T>(addr);
    }

    template <typename T>
    T read_safe(uintptr_t addr) {
        return this->read<T>(addr);
    }

    template <typename T>
    bool write(uintptr_t addr, T value) {
        return this->write(addr, &value, sizeof(T));
    }

    template <typename T>
    bool write_safe(uintptr_t addr, T value) {
        return this->write_safe(addr, &value, sizeof(T));
    }

    // ─── Module & Process Discovery ──────────────────────────────────────────
    uintptr_t getModuleBase(const char* name) {
        if (this->pid <= 0 || !name || !*name) return 0;
        if((selected_driver_==4 || selected_driver_==6) &&
           !strcmp(name,"libil2cpp.so"))return resolve_game_image(name);

        if (selected_driver_ == 2) { // Bing Driver
            return bing_get_module_base(name);
        } else if (selected_driver_ == 3) { // Paradise Driver
            return paradise_get_module_base(name);
        } else if (selected_driver_ == 4) { // TWT Driver
            return parse_maps_module_base(name);
        }
        if (selected_driver_ == 5 && wind && wind->is_bound()) {
            return wind->get_module_base(name);
        }
        if (selected_driver_ == 6 && nb9_connected && nb9_driver) {
            return parse_maps_module_base(name);
        }
        return 0;
    }

    uintptr_t get_module_base(const char* name) {
        return this->getModuleBase(name);
    }

    pid_t get_pid(const char *name) {
        if(!name||!*name)return 0;
        if (selected_driver_ == 3 && paradise_connected) {
            std::lock_guard<std::mutex> lock(paradise_lock);
            pid_t found = paradise->get_pid(name);
            if (found > 0) return found;
        } else if (selected_driver_ == 4) {
            pid_t p = twt_get_pid(name);
            if (p > 0 && process_name_matches(p,name)) return p;
        }

        // Generic procfs fallback
        DIR *dir = opendir("/proc");
        if (!dir) return 0;
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
            int p = atoi(entry->d_name);
            if (p <= 0) continue;
            char cmdline_path[64];
            snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", p);
            FILE *fp = fopen(cmdline_path, "r");
            if (fp) {
                char cmdline[256] = {0};
                if (fgets(cmdline, sizeof(cmdline), fp)) {
                    if ((selected_driver_==4||selected_driver_==6)?strcmp(cmdline,name)==0:strstr(cmdline,name)!=nullptr) {
                        fclose(fp);
                        closedir(dir);
                        return p;
                    }
                }
                fclose(fp);
            }
        }
        closedir(dir);
        return 0;
    }

    bool gyro_update(float x, float y, uint32_t type_mask = PARADISE_GYRO_MASK_ALL, bool enable = true) {
        if (selected_driver_ != 3 || !paradise_connected) return false;
        std::lock_guard<std::mutex> lock(paradise_lock);
        return paradise->gyro_update(x, y, type_mask, enable);
    }

    bool touch_init(int width, int height) {
        if (selected_driver_ != 3 || !paradise_connected) return false;
        return paradise_touch_init(width, height);
    }

    bool paradise_touch_event(uint32_t command, int slot, int x, int y) {
        if (selected_driver_ != 3 || !paradise_connected || slot < 0 || slot >= 10) return false;
        uint32_t dev_x = x, dev_y = y;
        if (command != 4 && g_touch_state.initialized) {
            int phys_x, phys_y;
            convert_screen_to_physical(x, y, &phys_x, &phys_y);
            convert_physical_to_device(phys_x, phys_y, &dev_x, &dev_y);
        }
        std::lock_guard<std::mutex> lock(paradise_lock);
        return paradise->touch_event(command, dev_x, dev_y, static_cast<uint16_t>(1u << slot));
    }

    bool touch_down(int slot, int x, int y) {
        return paradise_touch_event(2, slot, x, y);
    }

    bool touch_move(int slot, int x, int y) {
        return paradise_touch_event(3, slot, x, y);
    }

    bool touch_up(int slot) {
        return paradise_touch_event(4, slot, 0, 0);
    }

    // The new Paradise API does not expose legacy touch mode selection.
    bool touch_set_mode(int) { return false; }
    void hide_process() {
    }

    int get_fd() const { return -1; }
    int get_bing_vip_fd() const { return bing_fd_vip; }
    int get_bing_old_fd() const { return bing_fd_old; }
    int get_paradise_fd() const { return -1; } // Connection owned by Paradise API
    int get_twt_fd() const { return twt_fd; }

    #include "driver_breakpoints.inl"
    #include "driver_twt_breakpoints.inl"
    #include "driver_native_breakpoints.inl"

    // Legacy aim/hook entry points stay disabled. New HWBP X/Y uses the owned
    // native adapter above, so legacy code cannot bypass its cleanup tracking.
    bool AddHwBp(nb9::HW_BP_INFO *info) { (void)info; return false; }
    bool UpdateAndEnableHwBp(nb9::HW_BP_INFO *info) { (void)info; return false; }
    bool DisableHwBp(pid_t target_pid, uintptr_t addr) { (void)target_pid; (void)addr; return false; }
    bool ClearHwBp() { return false; }
    bool GetHwBpHits(nb9::HWBP_HIT_ARGS *args) { (void)args; return false; }
    bool UpdateTrackingData(bool active, uintptr_t bp_addr, float x, float y, float z) { (void)active; (void)bp_addr; (void)x; (void)y; (void)z; return false; }
    bool HookAttach(pid_t target_pid, uintptr_t addr) { (void)target_pid; (void)addr; return false; }
    bool HookDetach() { return false; }
    bool HookSetRot(const nb9::HOOK_SET_ROT *config) { (void)config; return false; }
    bool HookGetStatus(nb9::HOOK_STATUS *status) { (void)status; return false; }
    bool PteUpdateFloat3(float x, float y, float z) { (void)x; (void)y; (void)z; return false; }
};

// Global unified driver instance
inline c_driver *driver = nullptr;

// Alias for compatibility
inline c_driver *&AR = driver;
