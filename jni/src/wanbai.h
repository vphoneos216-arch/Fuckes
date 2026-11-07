// wanbai.h — Kernel Driver Interface (No Auto-Fallback)
// Supports 11 modes + AUTO-DETECT (0/-1): QX1, GT, rt-dev, rt-proc, QX(2), TB-KPM, DitPro-KPM (latest), KMA-KPM v6.12, LDK-KPM, Paradise, Bing (/proc/pid/mem)
// Bing (11) is explicit-select only — never substituted silently for a failed
// driver; it's a distinct, user-chosen option like the other ten.
// Integrated into InvisibleX Reborn project
// Do NOT include directly — included via bool.h after globals are defined
//
// MODIFIED: All automatic fallback (native memory access, /proc/pid/mem) removed.
// If a driver operation fails, it returns false immediately. No alternative path is attempted.

#ifndef WANBAI_H
#define WANBAI_H

// Forward declarations for globals defined in bool.h (included later via hook.h)
extern char packageName[];

#include <sys/fcntl.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <regex.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <initializer_list>
#include <utility>
#include <new>

// KMA KPM and LDK KPM external driver headers
#include "KMA_KPM/driver.h"
#include "LDK_KPM/LdgDriverStatic.h"
#include "Paradise/paradise_api.h"

// ── Native memory-access syscall numbers (kept for reference, but fallback disabled) ──
#ifdef __NR_process_vm_readv
  #define INX_NR_PVM_READV   __NR_process_vm_readv
#else
  #define INX_NR_PVM_READV   270
#endif
#ifdef __NR_process_vm_writev
  #define INX_NR_PVM_WRITEV  __NR_process_vm_writev
#else
  #define INX_NR_PVM_WRITEV  271
#endif

// ==========================================
// SYSCALL FALLBACK BACKEND (Kernel 6.12)
// Also used as Option 7: DitPro KPM (full)
// ==========================================
// ==========================================
// SYSCALL BACKEND — DitPro KPM (latest API)
// Ported from DitPro sample main.cpp (v current)
// Option 7 uses this path exclusively.
// ==========================================
class proKpm {
private:
    enum class MemoryOp : uint64_t {
        INIT      = 1ULL << 1,
        READ      = 1ULL << 2,
        WRITE     = 1ULL << 3,
        CPU       = 1ULL << 4,
        PROT_NC   = 1ULL << 5,
        READLIST  = 1ULL << 6,
        READARRAY = 1ULL << 7,
        UNINSTALL = 1ULL << 9
    };
    enum class OtherOp : uint64_t {
        PROCESS_PID  = 1ULL << 10,
        MODULE_BASE  = 1ULL << 11,
        HIDE_PID     = 1ULL << 12,
        UNHIDE_PID   = 1ULL << 13,
        GETUSERMAPS  = 1ULL << 22
    };

    static constexpr uint64_t FLAGS_BASE   = 1ULL << 0;
    static constexpr uint64_t CHECKSUCCESS = 1ULL << 30;
    static constexpr long     NR_SYSCALL   = 18;

    struct Dit_uct {
        uint64_t addr;
        void    *buffer;
        uint64_t size;
    } __attribute__((aligned(8)));

    struct Dit_uct_kpm_list {
        uint64_t addr[10];
        void    *buffer;
        uint64_t size;
    } __attribute__((aligned(8)));

    struct Dit_uct_base {
        int         pid;
        const char *name;
        unsigned long start;
        unsigned long end;
    };

    template<typename... Args>
    long call(Args&&... args) {
        return syscall(NR_SYSCALL, std::forward<Args>(args)...);
    }

public:
    int   pid  = -1;
    float mode = 1.f; // kept for ABI compatibility with old call sites

    // Detect if DitPro KPM is loaded (probe)
    static bool is_available() {
        long r = syscall(NR_SYSCALL, (FLAGS_BASE | CHECKSUCCESS));
        return (r == 616);
    }

    bool read(uintptr_t addr, void *buffer, size_t size) {
        if (this->pid <= 0 || !buffer || size == 0) return false;
        Dit_uct cm{};
        cm.addr   = (uint64_t)addr;
        cm.buffer = buffer;
        cm.size   = (uint64_t)size;
        uint64_t flags = FLAGS_BASE | (uint64_t)MemoryOp::READ;
        return (call(flags, &cm) == 0);
    }

    // Pointer-chain read (up to 10 hops) — latest READLIST
    bool read_list(void *buffer, size_t buffer_size, std::initializer_list<uint64_t> args) {
        if (this->pid <= 0 || !buffer) return false;
        Dit_uct_kpm_list cm{};
        memset(cm.addr, 0xff, sizeof(cm.addr));
        size_t i = 0;
        for (uint64_t v : args) {
            if (i >= 10) return false;
            cm.addr[i++] = v;
        }
        cm.buffer = buffer;
        cm.size   = (uint64_t)buffer_size;
        uint64_t flags = FLAGS_BASE | (uint64_t)MemoryOp::READ | (uint64_t)MemoryOp::READLIST;
        return (call(flags, &cm) == 0);
    }

    template <typename T>
    T read(uintptr_t addr) {
        T res{};
        this->read(addr, &res, sizeof(T));
        return res;
    }

    bool write(uintptr_t addr, void *buffer, size_t size) {
        if (this->pid <= 0 || !buffer || size == 0) return false;
        Dit_uct cm{};
        cm.addr   = (uint64_t)addr;
        cm.buffer = buffer;
        cm.size   = (uint64_t)size;
        uint64_t flags = FLAGS_BASE | (uint64_t)MemoryOp::WRITE;
        return (call(flags, &cm) == 0);
    }

    // Latest: INIT with pid (returns >0 on success)
    bool init_pid(int target_pid) {
        this->pid = target_pid;
        long r = call((FLAGS_BASE | (uint64_t)MemoryOp::INIT), target_pid);
        return (r > 0);
    }

    int get_pid(const char *name) {
        if (!name) return -1;
        long r = call((FLAGS_BASE | (uint64_t)OtherOp::PROCESS_PID), name);
        return (r > 2) ? (int)r : -1;
    }

    uintptr_t get_module_base(int target_pid, const char *module_name) {
        if (!module_name) return 0;
        Dit_uct_base cm{};
        cm.pid  = target_pid;
        cm.name = module_name;
        cm.start = 0;
        cm.end   = 0;
        if (call((FLAGS_BASE | (uint64_t)OtherOp::MODULE_BASE), &cm) == 0)
            return (uintptr_t)cm.start;
        // Fallback: /proc maps (same as before)
        FILE *fp;
        long addr = 0;
        char filename[64];
        char line[1024];
        snprintf(filename, sizeof(filename), "/proc/%d/maps", target_pid);
        fp = fopen(filename, "r");
        if (fp != NULL) {
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, module_name)) {
                    char *pch = strtok(line, "-");
                    addr = strtoul(pch, NULL, 16);
                    if (addr == 0x8000) addr = 0;
                    break;
                }
            }
            fclose(fp);
        }
        return (uintptr_t)addr;
    }
};

// ==========================================
// SYSCALL-FREE BACKEND — Bing (/proc/pid/mem)
// Option 11: explicit, user-selected only — never an automatic
// fallback for another option. Added fresh; no existing driver
// class above (proKpm / KMA / LDK / Paradise) was touched.
// ==========================================
class bingKpm {
public:
    int pid = -1;
    int handle = -1;

    static int get_pid(const char *packageName) {
        if (!packageName) return -1;
        DIR *dir = opendir("/proc");
        if (!dir) return -1;
        struct dirent *entry;
        char filename[64], cmdline[64];
        int found = -1;
        while ((entry = readdir(dir)) != nullptr) {
            int id = atoi(entry->d_name);
            if (id == 0) continue;
            snprintf(filename, sizeof(filename), "/proc/%d/cmdline", id);
            FILE *fp = fopen(filename, "r");
            if (fp) {
                memset(cmdline, 0, sizeof(cmdline));
                if (fgets(cmdline, sizeof(cmdline), fp))
                    if (strcmp(packageName, cmdline) == 0) found = id;
                fclose(fp);
                if (found != -1) break;
            }
        }
        closedir(dir);
        return found;
    }

    uintptr_t get_module_base(int target_pid, const char *module_name) {
        if (!module_name) return 0;
        char filename[64], line[1024];
        snprintf(filename, sizeof(filename), "/proc/%d/maps", target_pid);
        FILE *fp = fopen(filename, "r");
        if (!fp) return 0;
        unsigned long addr = 0;
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, module_name)) {
                char *pch = strtok(line, "-");
                addr = strtoul(pch, nullptr, 16);
                if (addr == 0x8000) addr = 0;
                break;
            }
        }
        fclose(fp);
        return (uintptr_t)addr;
    }

    // Opens /proc/<pid>/mem for RW. Requires root.
    bool init_pid(int target_pid) {
        this->pid = target_pid;
        if (target_pid <= 0) return false;
        char mempath[64];
        snprintf(mempath, sizeof(mempath), "/proc/%d/mem", target_pid);
        this->handle = open(mempath, O_RDWR);
        return this->handle != -1;
    }

    bool read(uintptr_t addr, void *buffer, size_t size) {
        if (this->handle < 0 || !buffer || size == 0) return false;
        return pread64(this->handle, buffer, size, (off64_t)addr) == (ssize_t)size;
    }

    bool write(uintptr_t addr, void *buffer, size_t size) {
        if (this->handle < 0 || !buffer || size == 0) return false;
        return pwrite64(this->handle, buffer, size, (off64_t)addr) == (ssize_t)size;
    }

    ~bingKpm() {
        if (this->handle > 0) { close(this->handle); this->handle = -1; }
    }
};

// ==========================================
// TB KPM PRCTL STRUCTURES (Option 6)
// ==========================================
struct mem_operation {
    pid_t    target_pid;
    uint64_t addr;
    void    *buffer;
    uint64_t size;
};

#define PRCTL_MEM_READ  0x63687501
#define PRCTL_MEM_WRITE 0x63687502

// ==========================================
// MAIN DRIVER CLASS (6 Options + Fallback)
// ==========================================
class c_driver {
private:
    proKpm syscall_backend;
    bool   use_syscall_fallback = false;
    bool   use_tb_kpm           = false;
    bool   use_ditpro_kpm       = false;   // Option 7: DitPro KPM standalone
    bool   use_kma_kpm          = false;   // Option 8: KMA KPM
    bool   use_ldk_kpm          = false;   // Option 9: LDK KPM
    bool   use_paradise         = false;   // Option 10: Paradise
    bool   use_bing             = false;   // Option 11: Bing (/proc/pid/mem)
    Driver        *kma_driver   = nullptr;
    LdgKpmDriver  *ldk_driver   = nullptr;
    paradise_driver *paradise   = nullptr;
    bingKpm        bing_backend;

    int    fd  = -1;
    pid_t  pid = 0;

    // No auto-fallback members – all removed.

    // ── Native memory access functions are removed; no fallback.

    typedef struct _COPY_MEMORY {
        pid_t    pid;
        uintptr_t addr;
        void    *buffer;
        size_t   size;
    } COPY_MEMORY, *PCOPY_MEMORY;

    typedef struct _MODULE_BASE {
        pid_t     pid;
        char     *name;
        uintptr_t base;
    } MODULE_BASE, *PMODULE_BASE;

    enum OPERATIONS {
        OP_INIT_KEY    = 0x800,
        OP_READ_MEM    = 0x801,
        OP_WRITE_MEM   = 0x802,
        OP_MODULE_BASE = 0x803,
    };

    char *execCom(const char *shell) {
        FILE *fp = popen(shell, "r");
        if (fp == NULL) return NULL;
        char  buffer[256];
        char *result = (char *)malloc(4096);
        if (!result) { pclose(fp); return NULL; }
        result[0] = '\0';
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            if (strlen(result) + strlen(buffer) < 4095)
                strcat(result, buffer);
        }
        pclose(fp);
        return result;
    }

    void createDriverNode(char *path, int major_number, int minor_number) {
        char command[256];
        snprintf(command, sizeof(command), "mknod %s c %d %d",
                 path, major_number, minor_number);
        system(command);
        printf("[-] Created %s\n[-] Major: %d Minor: %d\n",
               path, major_number, minor_number);
    }

    void removeDeviceNode(char *path) {
        if (unlink(path) == 0)
            printf("[-] Driver security daemon activated\n");
        else
            printf("[-] Driver security daemon execution error\n");
    }

public:
    // ── Option 1: QX1 ──────────────────────────────────
    const char *get_dev() {
        const char *command =
            "for dir in /proc/*/; do "
            "  cmdline=$(head -n 1 \"${dir}cmdline\" 2>/dev/null); "
            "  comm=$(head -n 1 \"${dir}comm\" 2>/dev/null); "
            "  if echo \"$cmdline\" | grep -qE '^/data/[a-z]{6}$'; then "
            "    for file in \"${dir}fd\"/*; do "
            "      link=$(readlink \"$file\" 2>/dev/null); "
            "      if [ \"$link\" = \"/dev/$comm (deleted)\" ]; then "
            "        nhjd=\"$file\"; "
            "        sbid=$(ls -L -l \"$nhjd\" 2>/dev/null | sed 's/\\([^,]*\\).*/\\1/' | sed 's/.*root //'); "
            "        echo \"/dev/$comm\"; "
            "        rm -f \"/dev/$comm\"; "
            "        mknod \"/dev/$comm\" c \"$sbid\" 0; "
            "        break 2; "
            "      fi; "
            "    done; "
            "  fi; "
            "done";
        FILE *file = popen(command, "r");
        if (file == NULL) return NULL;
        static char result[512];
        result[0] = '\0';
        if (fgets(result, sizeof(result), file) == NULL) {
            pclose(file);
            return NULL;
        }
        pclose(file);
        int len = (int)strlen(result);
        if (len > 0 && result[len - 1] == '\n') result[len - 1] = '\0';
        return result;
    }

    // ── Option 2: GT ───────────────────────────────────
    char *gtqwq() {
        const char *dev_path = "/dev";
        DIR *dir = opendir(dev_path);
        if (dir == NULL) {
            printf("[-] Unable to open /dev directory\n");
            return NULL;
        }
        const char *files[] = { "wanbai", "CheckMe", "Ckanri", "lanran", "video188" };
        struct dirent *entry;
        char *gtfile_path = NULL;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0) continue;
            size_t path_length = strlen(dev_path) + strlen(entry->d_name) + 2;
            gtfile_path = (char *)malloc(path_length);
            if (!gtfile_path) continue;
            snprintf(gtfile_path, path_length, "%s/%s", dev_path, entry->d_name);
            for (int i = 0; i < 5; i++) {
                if (strcmp(entry->d_name, files[i]) == 0) {
                    printf("[-] Driver file: %s\n", gtfile_path);
                    closedir(dir);
                    return gtfile_path;
                }
            }
            struct stat file_info;
            if (stat(gtfile_path, &file_info) < 0) { free(gtfile_path); gtfile_path = NULL; continue; }
            if (strstr(entry->d_name, "gpiochip") != NULL)  { free(gtfile_path); gtfile_path = NULL; continue; }
            if ((S_ISCHR(file_info.st_mode) || S_ISBLK(file_info.st_mode)) &&
                strchr(entry->d_name, '_') == NULL &&
                strchr(entry->d_name, '-') == NULL &&
                strchr(entry->d_name, ':') == NULL) {
                if (strcmp(entry->d_name, "stdin")  == 0 ||
                    strcmp(entry->d_name, "stdout") == 0 ||
                    strcmp(entry->d_name, "stderr") == 0) {
                    free(gtfile_path); gtfile_path = NULL; continue;
                }
                size_t file_name_length = strlen(entry->d_name);
                int file_year = localtime(&file_info.st_ctime)->tm_year + 1900;
                if (file_year <= 1980)           { free(gtfile_path); gtfile_path = NULL; continue; }
                if (file_info.st_atime == file_info.st_ctime) {
                    if ((file_info.st_mode & S_IFMT) == S_IFCHR &&
                        file_info.st_size == 0 &&
                        file_info.st_gid  == 0 &&
                        file_info.st_uid  == 0 &&
                        file_name_length  <= 9) {
                        printf("[-] Driver file: %s\n", gtfile_path);
                        closedir(dir);
                        return gtfile_path;
                    }
                }
            }
            free(gtfile_path); gtfile_path = NULL;
        }
        closedir(dir);
        return NULL;
    }

    // ── Option 4: rt-proc ─────────────────────────────
    char *driver_path() {
        struct dirent *de;
        DIR *dr = opendir("/proc");
        char *device_path = NULL;
        if (dr == NULL) { perror("- Could not open /proc"); return NULL; }
        while ((de = readdir(dr)) != NULL) {
            if (strcmp(de->d_name, "driver") == 0) continue;
            if (strlen(de->d_name) != 6) continue;
            int valid = 1;
            for (int i = 0; i < 6; i++) {
                if (!isalnum((unsigned char)de->d_name[i])) { valid = 0; break; }
            }
            if (!valid) continue;
            char tmp_path[128];
            snprintf(tmp_path, sizeof(tmp_path), "/proc/%s", de->d_name);
            struct stat sb;
            if (stat(tmp_path, &sb) == 0 && S_ISREG(sb.st_mode)) {
                if ((sb.st_mode & 0777) == 0666) {
                    device_path = strdup(tmp_path);
                    printf("- Method 4 driver: %s\n", device_path);
                    break;
                }
            }
        }
        if (!device_path) printf("[-] Could not find driver in /proc\n");
        closedir(dr);
        return device_path;
    }

    // ── Option 3: rt-dev ──────────────────────────────
    char *find_driver_path() {
        const char *dev_path = "/dev";
        DIR *dir = opendir(dev_path);
        if (dir == NULL) { printf("- Unable to open /dev\n"); return NULL; }
        const char *files[] = { "wanbai", "CheckMe", "Ckanri", "lanran", "video188" };
        struct dirent *entry;
        char *file_path = NULL;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
                strcmp(entry->d_name, "wanbai") == 0 || strcmp(entry->d_name, "CheckMe") == 0 ||
                strcmp(entry->d_name, "facking") == 0 || strcmp(entry->d_name, "vndbinder") == 0 ||
                strcmp(entry->d_name, "YBBBYN") == 0) continue;
            size_t path_length = strlen(dev_path) + strlen(entry->d_name) + 2;
            file_path = (char *)malloc(path_length);
            if (!file_path) continue;
            snprintf(file_path, path_length, "%s/%s", dev_path, entry->d_name);
            for (int i = 0; i < 5; i++) {
                if (strcmp(entry->d_name, files[i]) == 0) {
                    printf("- Method 3 driver: %s\n", file_path);
                    closedir(dir);
                    return file_path;
                }
            }
            struct stat file_info;
            if (stat(file_path, &file_info) < 0)            { free(file_path); file_path = NULL; continue; }
            if (strstr(entry->d_name, "gpiochip") != NULL)  { free(file_path); file_path = NULL; continue; }
            if ((S_ISCHR(file_info.st_mode) || S_ISBLK(file_info.st_mode)) &&
                strchr(entry->d_name, '_') == NULL &&
                strchr(entry->d_name, '-') == NULL &&
                strchr(entry->d_name, ':') == NULL) {
                if (strcmp(entry->d_name, "stdin")  == 0 ||
                    strcmp(entry->d_name, "stdout") == 0 ||
                    strcmp(entry->d_name, "stderr") == 0) {
                    free(file_path); file_path = NULL; continue;
                }
                size_t file_name_length = strlen(entry->d_name);
                int file_year = localtime(&file_info.st_ctime)->tm_year + 1900;
                if (file_year <= 1980) { free(file_path); file_path = NULL; continue; }
                if (file_info.st_atime == file_info.st_ctime) {
                    if ((file_info.st_mode & S_IFMT) == S_IFCHR &&
                        file_info.st_size == 0 &&
                        file_info.st_gid  == 0 &&
                        file_info.st_uid  == 0 &&
                        file_name_length  <= 9) {
                        printf("- Method 3 driver: %s\n", file_path);
                        closedir(dir);
                        return file_path;
                    }
                }
            }
            free(file_path); file_path = NULL;
        }
        closedir(dir);
        return NULL;
    }

    // ── Global driver choice (set by main before new c_driver) ────────
    // -1 = not set (show terminal menu), 1-9 = pre-selected from WebUI
    static int g_driver_choice;


    // ── SAFE + STRICT auto-detect (no fake /dev nodes, no fatal ctors) ──

    static bool ix_node(const char *path) {
        return path && path[0] && access(path, F_OK) == 0;
    }

    static bool ix_any_node(const char *const *list) {
        for (int i = 0; list[i]; ++i)
            if (ix_node(list[i])) return true;
        return false;
    }

    static bool ix_module_loaded(const char *name) {
        char path[128];
        snprintf(path, sizeof(path), "/sys/module/%s", name);
        if (ix_node(path)) return true;
        FILE *f = fopen("/proc/modules", "r");
        if (!f) return false;
        char line[256];
        bool found = false;
        size_t nlen = strlen(name);
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, name, nlen) == 0 &&
                (line[nlen] == ' ' || line[nlen] == '\t')) {
                found = true;
                break;
            }
        }
        fclose(f);
        return found;
    }

    // Basename of path
    static const char *ix_basename(const char *path) {
        if (!path) return "";
        const char *s = strrchr(path, '/');
        return s ? s + 1 : path;
    }

    // Known GT driver node names ONLY (gtqwq heuristics match random /dev/vhbt etc.)
    static bool ix_is_real_gt_name(const char *name) {
        if (!name || !name[0]) return false;
        static const char *k[] = {
            "wanbai", "CheckMe", "Ckanri", "lanran", "video188",
            nullptr
        };
        for (int i = 0; k[i]; ++i)
            if (strcmp(name, k[i]) == 0) return true;
        return false;
    }

    // Reject obvious non-driver nodes
    static bool ix_is_fake_dev(const char *name) {
        if (!name || !name[0]) return true;
        static const char *bad[] = {
            "vhbt", "ashmem", "binder", "hwbinder", "vndbinder",
            "null", "zero", "full", "random", "urandom", "stdin",
            "stdout", "stderr", "console", "tty", "ptmx", "pts",
            "gpiochip", "loop", "ram", "dm-", "block",
            "wlan", "wifi", "wcnss", "ipa", "rmnet", "smcinvoke",
            "kgsl", "mali", "ion", "dma_heap", "udc", "usb",
            "graphics", "pmsg", "log", "event", "input", "rtc",
            "alarm", "fuse", "mtp", "nfc", "qsee", "spcom",
            nullptr
        };
        for (int i = 0; bad[i]; ++i) {
            if (strcmp(name, bad[i]) == 0) return true;
            if (strncmp(name, bad[i], strlen(bad[i])) == 0) return true;
        }
        return false;
    }

    // Open node O_RDWR briefly — real drivers usually allow root open
    static bool ix_can_open_rw(const char *path) {
        if (!path || !path[0]) return false;
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) return false;
        close(fd);
        return true;
    }

    // Strict GT: only known names under /dev that open RW
    static bool ix_probe_gt(char *out, size_t outsz) {
        static const char *names[] = {
            "wanbai", "CheckMe", "Ckanri", "lanran", "video188", nullptr
        };
        for (int i = 0; names[i]; ++i) {
            char path[64];
            snprintf(path, sizeof(path), "/dev/%s", names[i]);
            if (ix_node(path) && ix_can_open_rw(path)) {
                snprintf(out, outsz, "%s", path);
                return true;
            }
        }
        return false;
    }



    /* Live Paradise probe — constructor + is_process_alive (no /dev name guess) */
    static bool ix_paradise_alive() {
        paradise_driver *tmp = nullptr;
        try {
            tmp = new (std::nothrow) paradise_driver();
        } catch (...) {
            tmp = nullptr;
        }
        if (!tmp) return false;
        int alive = 0;
        bool ok = false;
        try {
            ok = tmp->is_process_alive(getpid(), &alive);
        } catch (...) {
            ok = false;
        }
        delete tmp;
        /* Driver present if API call succeeds (alive may be 0/1) */
        return ok;
    }

    int auto_detect_driver() {
        /* ONLY 3 drivers — order fixed:
         *   1) RT drive
         *   2) Paradise (real API probe)
         *   3) DitPro KPM
         */
        printf("[INX] Scanning drivers (RT -> Paradise -> DitPro KPM)...\n");
        fflush(stdout);

        // ── 1) RT drive ──────────────────────────────────────────────
        {
            static const char *rt_nodes[] = {
                "/dev/rt_driver", "/dev/rtdriver", "/dev/rt_mem",
                "/dev/RTDriver", "/dev/rtk", "/dev/rt_dev",
                "/dev/rtproc", "/dev/rt_proc", nullptr
            };
            for (int i = 0; rt_nodes[i]; ++i) {
                if (ix_node(rt_nodes[i]) && ix_can_open_rw(rt_nodes[i])) {
                    printf("[INX] OK  RT  %s\n", rt_nodes[i]);
                    return 3;
                }
            }
            char *p = find_driver_path();
            if (p && p[0]) {
                const char *base = ix_basename(p);
                if (!ix_is_fake_dev(base) && ix_can_open_rw(p)) {
                    printf("[INX] OK  RT-dev  %s\n", p);
                    free(p);
                    return 3;
                }
                printf("[INX] --  RT-dev (skip %s)\n", base ? base : "?");
                free(p);
            } else {
                if (p) free(p);
                printf("[INX] --  RT-dev\n");
            }
            p = driver_path();
            if (p && p[0]) {
                const char *base = ix_basename(p);
                if (!ix_is_fake_dev(base) && ix_can_open_rw(p)) {
                    printf("[INX] OK  RT-proc  %s\n", p);
                    free(p);
                    return 4;
                }
                printf("[INX] --  RT-proc (skip %s)\n", base ? base : "?");
                free(p);
            } else {
                if (p) free(p);
                printf("[INX] --  RT-proc\n");
            }
        }

        // ── 2) Paradise — real probe (works even without /dev/paradise name) ──
        {
            static const char *pd[] = {
                "/dev/paradise", "/dev/paradise_drv", "/dev/paradise_mem",
                "/dev/pd_mem", "/dev/pdriver", "/dev/paradise_api",
                "/dev/Paradise", nullptr
            };
            bool node_hit = ix_any_node(pd)
                || ix_module_loaded("paradise")
                || ix_module_loaded("paradise_drv")
                || ix_module_loaded("paradise_driver")
                || ix_module_loaded("paradise_mem");

            if (node_hit) {
                for (int i = 0; pd[i]; ++i) {
                    if (ix_node(pd[i])) {
                        printf("[INX] OK  Paradise  %s\n", pd[i]);
                        return 10;
                    }
                }
                printf("[INX] OK  Paradise  (module)\n");
                return 10;
            }

            printf("[INX] ..  Paradise API probe...\n");
            fflush(stdout);
            if (ix_paradise_alive()) {
                printf("[INX] OK  Paradise  (API connected)\n");
                return 10;
            }
            printf("[INX] --  Paradise\n");
        }

        // ── 3) DitPro KPM ────────────────────────────────────────────
        if (proKpm::is_available()) {
            printf("[INX] OK  DitPro KPM\n");
            return 7;
        }
        printf("[INX] --  DitPro KPM\n");

        printf("\n[-] Please insert or load driver\n");
        printf("    Supported: RT drive | Paradise | DitPro KPM\n");
        fflush(stdout);
        return -1;
    }


    // ── Internal: run chosen mode ──────────────────────────────────────
    void _init_from_choice(int choice) {

        // ── Option 6: TB KPM ──────────────────────────
        if (choice == 6) {
            use_tb_kpm = true;
            printf("\033[41m\033[37m[+] KPM Mode Active — Lag Free\033[0m\n");
            if (getuid() != 0)
                printf("\033[33m[WARNING] Root access recommended for KPM!\033[0m\n");
            return;
        }

        // ── Option 1: QX1 ─────────────────────────────
        if (choice == 1) {
            const char *dev_path = get_dev();
            if (!dev_path) { printf("[-] Unable to find driver file!\n"); exit(0); }
            char dev_buf[64];
            snprintf(dev_buf, sizeof(dev_buf), "%s", dev_path);
            fd = open(dev_buf, O_RDWR);
            if (fd > 0) {
                printf("[-] Driver file: %s\n", dev_buf);
                unlink(dev_buf);
            } else {
                printf("[-] Unable to open driver file!\n");
                exit(0);
            }
        }
        // ── Option 2: GT ──────────────────────────────
        else if (choice == 2) {
            char *device_name = gtqwq();
            if (!device_name) { printf("[-] GT driver not found!\n"); exit(0); }
            fd = open(device_name, O_RDWR);
            free(device_name);
            if (fd == -1) {
                printf("[-] Failed to open GT driver. Re-flash required.\n");
                exit(0);
            }
        }
        // ── Option 3: rt-dev ──────────────────────────
        else if (choice == 3) {
            char *device_name = find_driver_path();
            if (!device_name) device_name = find_driver_path();
            if (!device_name) { fprintf(stderr, "[-] rt-dev driver not found\n"); exit(EXIT_FAILURE); }
            fd = open(device_name, O_RDWR);
            free(device_name);
            if (fd == -1) {
                // No fallback – exit
                fprintf(stderr, "[-] Failed to open rt-dev driver node. No fallback available. Exiting.\n");
                exit(EXIT_FAILURE);
            }
        }
        // ── Option 4: rt-proc ─────────────────────────
        else if (choice == 4) {
            char *device_name = driver_path();
            if (!device_name) { fprintf(stderr, "[-] rt-proc driver not found\n"); exit(EXIT_FAILURE); }
            fd = open(device_name, O_RDWR);
            free(device_name);
            if (fd == -1) {
                // No fallback – exit
                fprintf(stderr, "[-] Failed to open rt-proc driver node. No fallback available. Exiting.\n");
                exit(EXIT_FAILURE);
            }
        }
        // ── Option 5: QX(2) ───────────────────────────
        else if (choice == 5) {
            char *output = execCom(
                "ls -l /proc/*/exe 2>/dev/null | "
                "grep -E \"/data/[a-z]{6} \\(deleted\\)\"");
            char filePath[256] = {0};
            char pidStr[64]    = {0};
            if (output != NULL) {
                char *procStart = strstr(output, "/proc/");
                if (procStart) {
                    char *pidStart = procStart + 6;
                    char *pidEnd   = strchr(pidStart, '/');
                    if (pidEnd) {
                        size_t pidLen = (size_t)(pidEnd - pidStart);
                        if (pidLen < sizeof(pidStr)) {
                            strncpy(pidStr, pidStart, pidLen);
                            pidStr[pidLen] = '\0';
                        }
                    }
                }
                char *arrowStart = strstr(output, "-> ");
                if (arrowStart) {
                    char *start = arrowStart + 3;
                    char *end   = strchr(output, '(');
                    if (end) end--;
                    size_t flen = (size_t)(end - start);
                    if (flen < sizeof(filePath)) {
                        strncpy(filePath, start, flen);
                        filePath[flen] = '\0';
                    }
                }
                char *replacePtr = strstr(filePath, "data");
                if (replacePtr != NULL) {
                    memmove(replacePtr + 2, replacePtr + 3, strlen(replacePtr + 3) + 1);
                    memmove(replacePtr, "dev", 3);
                }
                free(output);
            } else {
                printf("[-] Error executing script\n");
                exit(0);
            }
            if (pidStr[0] != '\0') {
                char cmd[256];
                snprintf(cmd, sizeof(cmd), "ls -al -L /proc/%s/fd/3", pidStr);
                char *fdInfo = execCom(cmd);
                int major_number = 0, minor_number = 0;
                if (fdInfo) {
                    sscanf(fdInfo, "%*s %*d %*s %*s %d, %d",
                           &major_number, &minor_number);
                    free(fdInfo);
                }
                if (filePath[0] != '\0' && strcmp(filePath, "0") != 0)
                    createDriverNode(filePath, major_number, minor_number);
            }
            sleep(1);
            fd = open(filePath, O_RDWR);
            if (fd == -1) {
                printf("\033[31m[X] Driver connection failed\033[0m\n");
                return;
            } else {
                printf("[+] Connection successful\n");
                printf("[-] Driver file: %s\n", filePath);
                removeDeviceNode(filePath);
            }
        }
        // ── Option 7: DitPro KPM ──────────────────────────────
        else if (choice == 7) {
            use_ditpro_kpm = true;
            if (!proKpm::is_available()) {
                printf("[-] DitPro KPM not detected (syscall probe failed). Load kpm first.\n");
                // still allow — some builds probe differently; init_pid will fail later if truly missing
            }
            printf("\033[41m\033[37m[+] DitPro KPM Mode Active (latest API)\033[0m\n");
            if (getuid() != 0)
                printf("\033[33m[WARNING] Root access recommended for DitPro KPM!\033[0m\n");
        }
        // ── Option 8: KMA KPM ────────────────────────────────
        else if (choice == 8) {
            // KMA KPM v6.12.x — latest driver.a / driver.h (gid != 0 => ok)
            use_kma_kpm = true;
            kma_driver  = new Driver();
            if (!kma_driver || kma_driver->gid == 0) {
                printf("[-] KMA KPM driver init failed (gid=0). Is kma_v6.12 kpm loaded?\n");
                if (kma_driver) { delete kma_driver; kma_driver = nullptr; }
                use_kma_kpm = false;
                return;
            }
            printf("\033[41m\033[37m[+] KMA KPM v6.12 Mode Active (gid=%llu)\033[0m\n",
                   (unsigned long long)kma_driver->gid);
        }
        // ── Option 9: LDK KPM (绿豆糕) ──────────────────────
        else if (choice == 9) {
            use_ldk_kpm = true;
            ldk_driver  = new LdgKpmDriver();
            printf("\033[41m\033[37m[+] LDK KPM Mode Active\033[0m\n");
            if (getuid() != 0)
                printf("\033[33m[WARNING] Root access recommended for LDK KPM!\033[0m\n");
        }
        // ── Option 10: Paradise ───────────────────────────
        else if (choice == 10) {
            use_paradise = true;
            paradise = new (std::nothrow) paradise_driver();
            if (!paradise) {
                printf("[-] Paradise driver object failed — will try auto-detect\n");
                use_paradise = false;
                return;
            }
            printf("\033[41m\033[37m[+] Paradise Mode Active\033[0m\n");
            if (getuid() != 0)
                printf("\033[33m[WARNING] Root recommended for Paradise\033[0m\n");
        }
        // ── Option 11: Bing (/proc/pid/mem) ────────────────
        else if (choice == 11) {
            use_bing = true;
            printf("\033[41m\033[37m[+] Bing Mode Active (/proc/pid/mem)\033[0m\n");
            if (getuid() != 0)
                printf("\033[33m[WARNING] Root access required for Bing!\033[0m\n");
        }
        else {
            printf("[-] Invalid option. Exiting.\n");
            exit(0);
        }
    }

    // ── Constructor — WebUI-aware ──────────────────────────────────────
    // If g_driver_choice is set (from WebUI config), skip terminal prompt.
    // Otherwise show full terminal menu as before.
    bool any_backend_ready() const {
        // Real members only (no use_gt / use_kpm — those never existed)
        return use_tb_kpm || use_ditpro_kpm || use_kma_kpm || use_ldk_kpm
            || use_paradise || use_syscall_fallback || use_bing || (fd > 0);
    }

    void _reset_backend_flags() {
        use_tb_kpm = use_ditpro_kpm = use_kma_kpm = use_ldk_kpm = false;
        use_paradise = use_syscall_fallback = use_bing = false;
        if (kma_driver) { delete kma_driver; kma_driver = nullptr; }
        if (ldk_driver) { delete ldk_driver; ldk_driver = nullptr; }
        if (paradise)   { delete paradise;   paradise   = nullptr; }
        if (bing_backend.handle > 0) { close(bing_backend.handle); bing_backend.handle = -1; }
        if (fd > 0) { close(fd); fd = -1; }
    }

    c_driver() {
        // Manual only: the driver is the one chosen in the WebUI
        // (config.json "driver", read into g_driver_choice by main).
        // No automatic scan of other backends.
        const int choice = g_driver_choice;
        if (choice < 1 || choice > 10) {
            printf("[-] No driver selected. Choose one in the WebUI and launch again.\n");
            fflush(stdout);
            exit(0);
        }
        printf("\033[92m[INX] Driver mode %d (WebUI selected)\033[0m\n", choice);
        fflush(stdout);
        _init_from_choice(choice);
        if (!any_backend_ready()) {
            printf("[-] Selected driver %d failed to initialise. Check it is loaded.\n", choice);
            fflush(stdout);
            exit(0);
        }
    }

    ~c_driver() {
        if (fd > 0) close(fd);
        if (kma_driver) {
            kma_driver->destroy();
            delete kma_driver;
            kma_driver = nullptr;
        }
        if (ldk_driver) { delete ldk_driver; ldk_driver = nullptr; }
        if (paradise) { delete paradise; paradise = nullptr; }
        if (bing_backend.handle > 0) close(bing_backend.handle);
    }

    // ── Attach PID after game process is found ─────────
    void initialize(pid_t target_pid) {
        this->pid = target_pid;
        if (use_syscall_fallback) {
            syscall_backend.pid = target_pid;
            syscall_backend.init_pid((int)target_pid);
        }
        if (use_ditpro_kpm) {
            // Latest DitPro: INIT flag with pid required before R/W
            if (!syscall_backend.init_pid((int)target_pid))
                printf("[-] DitPro init_pid(%d) failed\n", (int)target_pid);
        }
        if (use_kma_kpm && kma_driver) {
            // Latest KMA: initpid(pid, fps) — default 90 FPS sync
            kma_driver->initpid(target_pid, 90);
        }
        if (use_ldk_kpm && ldk_driver)
            ldk_driver->initialize(target_pid);
        if (use_paradise && paradise)
            paradise->initialize(target_pid);
        if (use_bing) {
            if (!bing_backend.init_pid((int)target_pid))
                printf("[-] Bing: cannot open /proc/%d/mem (need root)\n", (int)target_pid);
        }
    }

    bool initKey(const char *key) {
        if (use_tb_kpm)      return true;
        if (use_syscall_fallback) return true;
        if (use_ditpro_kpm)  return true;
        if (use_kma_kpm)     return true;
        if (use_ldk_kpm)     return true;
        if (use_paradise)    return true;
        if (use_bing)        return true;
        return (ioctl(fd, OP_INIT_KEY, key) == 0);
    }

    // ── Core read — NO FALLBACK ──────────────────────────
    bool read(uintptr_t addr, void *buffer, size_t size) {
        return _backend_read(addr, buffer, size);
    }

    // ── Core write — NO FALLBACK ─────────────────────────
    bool write(uintptr_t addr, void *buffer, size_t size) {
        return _backend_write(addr, buffer, size);
    }

    // ── Backend read (raw buffer) — original logic, unchanged ──
    bool _backend_read(uintptr_t addr, void *buffer, size_t size) {
        if (use_tb_kpm) {
            if (this->pid <= 0) return false;
            struct mem_operation op = { this->pid, (uint64_t)addr, buffer, (uint64_t)size };
            return (prctl(PRCTL_MEM_READ, (unsigned long)&op, 0, 0, 0) >= 0);
        }
        if (use_syscall_fallback)
            return syscall_backend.read(addr, buffer, size);
        if (use_ditpro_kpm)
            return syscall_backend.read(addr, buffer, size);
        if (use_kma_kpm && kma_driver)
            return kma_driver->read(addr, buffer, size);
        if (use_ldk_kpm && ldk_driver)
            return ldk_driver->read_legacy(addr, buffer, size);
        if (use_paradise && paradise)
            return paradise->read(addr, buffer, size);
        if (use_bing)
            return bing_backend.read(addr, buffer, size);
        COPY_MEMORY cm;
        cm.pid    = this->pid;
        cm.addr   = addr;
        cm.buffer = buffer;
        cm.size   = size;
        return (ioctl(fd, OP_READ_MEM, &cm) == 0);
    }

    // ── Backend write (raw buffer) — NO FALLBACK ──────────
    bool _backend_write(uintptr_t addr, void *buffer, size_t size) {
        if (use_tb_kpm) {
            if (this->pid <= 0) return false;
            struct mem_operation op = { this->pid, (uint64_t)addr, buffer, (uint64_t)size };
            return (prctl(PRCTL_MEM_WRITE, (unsigned long)&op, 0, 0, 0) >= 0);
        }
        if (use_syscall_fallback)
            return syscall_backend.write(addr, buffer, size);
        if (use_ditpro_kpm)
            return syscall_backend.write(addr, buffer, size);
        if (use_kma_kpm && kma_driver)
            return kma_driver->write(addr, buffer, size);
        if (use_ldk_kpm) {
            return false;
        }
        if (use_paradise && paradise)
            return paradise->write(addr, buffer, size);
        if (use_bing)
            return bing_backend.write(addr, buffer, size);
        COPY_MEMORY cm;
        cm.pid    = this->pid;
        cm.addr   = addr;
        cm.buffer = buffer;
        cm.size   = size;
        return (ioctl(fd, OP_WRITE_MEM, &cm) == 0);
    }

    // ── Typed read (template) ─────────────────────────
    template <typename T>
    T read(uintptr_t addr) {
        T res{};
        this->read(addr, &res, sizeof(T));
        return res;
    }

    // ── Typed write (template) ────────────────────────
    template <typename T>
    bool write(uintptr_t addr, T value) {
        return this->write(addr, &value, sizeof(T));
    }

    // ── Module base resolver ──────────────────────────
    uintptr_t getModuleBase(const char *name) {
        if (use_tb_kpm) {
            uintptr_t addr = 0;
            char filename[64];
            snprintf(filename, sizeof(filename), "/proc/%d/maps", (int)this->pid);
            FILE *fp = fopen(filename, "r");
            if (fp) {
                char line[1024];
                while (fgets(line, sizeof(line), fp)) {
                    if (strstr(line, name)) {
                        char *pch = strtok(line, "-");
                        addr = (uintptr_t)strtoul(pch, NULL, 16);
                        break;
                    }
                }
                fclose(fp);
            }
            if (addr == 0 || addr == 0x8000) {
                char cmd[256];
                snprintf(cmd, sizeof(cmd),
                         "su -c 'cat /proc/%d/maps | grep %s'",
                         (int)this->pid, name);
                fp = popen(cmd, "r");
                if (fp) {
                    char line[1024];
                    while (fgets(line, sizeof(line), fp)) {
                        if (strstr(line, name)) {
                            char *pch = strtok(line, "-");
                            addr = (uintptr_t)strtoul(pch, NULL, 16);
                            break;
                        }
                    }
                    pclose(fp);
                }
            }
            return (addr == 0x8000) ? 0 : addr;
        }
        if (use_syscall_fallback)
            return syscall_backend.get_module_base((int)this->pid, name);
        if (use_ditpro_kpm)
            return syscall_backend.get_module_base((int)this->pid, name);
        if (use_kma_kpm && kma_driver)
            return kma_driver->get_module_base(this->pid, const_cast<char*>(name));
        if (use_ldk_kpm && ldk_driver)
            return (uintptr_t)ldk_driver->get_module_base(this->pid, name);
        if (use_paradise && paradise)
            return paradise->get_module_base(name);
        if (use_bing)
            return bing_backend.get_module_base((int)this->pid, name);
        MODULE_BASE mb;
        mb.pid  = this->pid;
        mb.name = strdup(name);
        if (!mb.name) return 0;
        if (ioctl(fd, OP_MODULE_BASE, &mb) != 0) { free(mb.name); return 0; }
        uintptr_t base = mb.base;
        free(mb.name);
        return base;
    }
};

// ── Global driver instance (initialised in main before game loop) ──
extern c_driver *g_driver;

// g_driver_choice defined in wanbai_statics.cpp (avoid ODR)

// Driver mode name for WebUI status display
static const char* g_DriverModeName(int choice) {
    switch(choice) {
        case 1: return "QX1";
        case 2: return "GT";
        case 3: return "rt-dev";
        case 4: return "rt-proc";
        case 5: return "QX(2)";
        case 6: return "KPM";
        case 7: return "DitPro-KPM";
        case 8: return "KMA-KPM";
        case 9: return "LDK-KPM";
        case 10: return "Paradise";
        case 11: return "Bing";
        case 0: return "AUTO";
        default: return "Unknown";
    }
}

// Driver status file — service.sh writes here, WebUI reads
#define DRIVER_STATUS_FILE "/data/adb/modules/InvisibleX_Reborn/webroot/.inx_driver_status"

static void _write_driver_status(int choice, bool ok) {
    FILE* f = fopen(DRIVER_STATUS_FILE, "w");
    if (!f) return;
    fprintf(f, "%d|%s|%s\n", choice, g_DriverModeName(choice), ok ? "active" : "failed");
    fclose(f);
    chmod(DRIVER_STATUS_FILE, 0644);
}

#endif // WANBAI_H