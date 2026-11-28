#include <vector>
#include <cstring>
#include "src/Vectors/Vector2.h"
#include "src/Vectors/Vector3.h"
#include "src/Vectors/Quaternion.h"
#include "src/Scarecrow/MemoryTools.h"
#include "src/Scarecrow/oxorany_include.h"
// Kernel driver interface (wanbai) — must come after global defs
// Actual include is in main.cpp BEFORE bool.h is pulled in via hook.h
// Here we only need the forward declarations (already injected into Read/Write block)


using namespace std;
using namespace ImGui;


// r-xp (xa) || r--p (cd)
#define PERM_EXEC "r--p"

#define targetaLibName "libil2cpp.so"
#define targetaLibName2 "libunity.so"

#define SYS_process_vm_readv 270
#define SYS_process_vm_writev 271

uint64_t lib_base = 0;
uint64_t lib_base2 = 0;

char packageName[] = "com.dts.freefiremax";

bool main_thread_flag = true;
int abs_ScreenX = 0;
int abs_ScreenY = 0;
int game_pid = -1;

int getProcessID(const char *packageName) {
  int id = -1;
  DIR *dir;
  FILE *fp;
  char filename[64];
  char cmdline[64];
  struct dirent *entry;

  dir = opendir("/proc");
  if (!dir) return -1;

  while ((entry = readdir(dir)) != NULL) {
    id = atoi(entry->d_name);
    if (id != 0) {
      sprintf(filename, "/proc/%d/cmdline", id);
      fp = fopen(filename, "r");
      if (fp) {
        fgets(cmdline, sizeof(cmdline), fp);
        fclose(fp);
        if (strcmp(packageName, cmdline) == 0) {
          closedir(dir);
          return id;
        }
      }
    }
  }
  closedir(dir);
  return -1;
}

typedef struct matrix {
  float m11,
  m12,
  m13,
  m14;
  float m21,
  m22,
  m23,
  m24;
  float m31,
  m32,
  m33,
  m34;
  float m41,
  m42,
  m43,
  m44;
};





uint64_t get_module_base(const char* module_name, const char* perms) {
  uint64_t returned = 0;
  char path[64],
  line[1024];
  snprintf(path, sizeof(path), "/proc/%d/maps", game_pid);
  FILE* maps = fopen(path, "rt");
  if (!maps) return 0;

  while (fgets(line, sizeof(line), maps)) {
    if (strstr(line, module_name) && strstr(line, perms)) {
      uint64_t start = 0,
      end = 0;
      sscanf(line, "%lx-%lx", &start, &end);
      if ((end - start) < 0x5100000) {
        returned = start;
        break;
      }
    }
  }
  fclose(maps);
  return returned;
}





// Write bytes to memory
bool write_bytes_to_memory(int pid, uint64_t address, const uint8_t* bytes, size_t len) {
  if (pid <= 0 || !bytes || len == 0) {
    return false;
  }

  char mem_file[64];
  snprintf(mem_file, sizeof(mem_file), "/proc/%d/mem", pid);
  int fd = open(mem_file, O_RDWR);
  if (fd < 0) {
    return false;
  }

  ssize_t written = pwrite64(fd, bytes, len, address);
  close(fd);

  return (written == (ssize_t)len);
}

std::vector < uint8_t > hex_string_to_bytes(const std::string& hex_str) {
  std::vector < uint8_t > bytes;

  if (hex_str.empty()) {
    return bytes;
  }

  std::stringstream ss(hex_str);
  std::string token;

  while (ss >> token) {
    // SKIP wildcards - they don't generate bytes
    if (token == "??" || token == "?") {
      // Add a placeholder byte for wildcards (will be ignored by mask)
      bytes.push_back(0x00);
      continue;
    }

    // Only process tokens that are exactly 2 characters (hex bytes)
    if (token.length() == 2) {
      // Check if valid hex characters
      bool is_hex = true;
      for (char c: token) {
        if (!isxdigit(c)) {
          is_hex = false;
          break;
        }
      }

      if (is_hex) {
        uint8_t byte = (uint8_t)strtol(token.c_str(), nullptr, 16);
        bytes.push_back(byte);
      } else {
        printf("[hex_string_to_bytes] Skipping invalid token: %s\n", token.c_str());
      }
    }
  }

  if (bytes.empty()) {
    fprintf(stderr, "Error: No valid bytes found in pattern\n");
    return bytes;
  }

  printf("Byte Size: %zu bytes\n", bytes.size());
  return bytes;
}

void read_game_bytes(uint64_t address, int pid, uint8_t* buf, size_t len) {
  char mem_file[64];
  snprintf(mem_file, sizeof(mem_file), "/proc/%d/mem", pid);
  int fd = open(mem_file, O_RDONLY);
  if (fd < 0) {
    memset(buf, 0, len);
    return;
  }
  pread64(fd, buf, len, address);
  close(fd);
}

bool PatchOffsetWithHex(uint64_t libbase, uint64_t offset, const char* replace_hex) {
  if (!replace_hex) {
    printf("[Patch] Error: NULL replace pattern");
    return false;
  }

  if (game_pid <= 0) {
    printf("[Patch] Error: Invalid game PID");
    return false;
  }

  if (libbase == 0) {
    printf("[Patch] Error: Library base not initialized");
    return false;
  }

  uint64_t target_address = libbase + offset;
  std::vector < uint8_t > replace_bytes = hex_string_to_bytes(std::string(replace_hex));

  if (replace_bytes.empty()) {
    printf("[Patch] Error: Invalid replace hex pattern");
    return false;
  }

  uint8_t original_bytes[256];
  size_t read_len = std::min((size_t)256, replace_bytes.size());
  read_game_bytes(target_address, game_pid, original_bytes, read_len);

  bool success = write_bytes_to_memory(game_pid, target_address,
    replace_bytes.data(), replace_bytes.size());

  if (success) {
    printf("[Patch] Success!");
  } else {
    printf("[Patch] Failed to write memory");
  }

  return success;
}


// ============================================================
// DUAL-MODE MEMORY ACCESS
// Kernel driver mode  → g_driver->read/write() (wanbai.h)
// Normal mode         → process_vm_readv/writev (syscall)
// g_driver is nullptr in normal mode — set in main.cpp
// ============================================================

// Forward-declare so bool.h can reference it before wanbai.h full include
class c_driver;
extern c_driver *g_driver;

// -- Normal-mode helpers (always available as fallback) ------
template<class T>
static inline T _vm_read(uintptr_t address) {
    T buf{};
    struct iovec local, remote;
    local.iov_base  = &buf;
    local.iov_len   = sizeof(T);
    remote.iov_base = reinterpret_cast<void*>(address);
    remote.iov_len  = sizeof(T);
    ssize_t bytes = syscall(SYS_process_vm_readv, game_pid, &local, 1, &remote, 1, 0);
    if (bytes != (ssize_t)sizeof(T)) memset(&buf, 0, sizeof(T));
    return buf;
}

template<typename T>
static inline bool _vm_write(uintptr_t address, T value) {
    struct iovec local, remote;
    local.iov_base  = &value;
    local.iov_len   = sizeof(T);
    remote.iov_base = reinterpret_cast<void*>(address);
    remote.iov_len  = sizeof(T);
    ssize_t bytes = syscall(SYS_process_vm_writev, game_pid, &local, 1, &remote, 1, 0);
    return (bytes == (ssize_t)sizeof(T));
}

// -- Unified Read<T>: kernel driver if available, else normal --
template<class T>
T Read(uintptr_t address) {
    if (g_driver != nullptr) {
        T buf{};
        g_driver->read(address, &buf, sizeof(T));
        return buf;
    }
    return _vm_read<T>(address);
}

// -- String specialisation (always uses process_vm path) ------
template<>
std::string Read<std::string>(uintptr_t address) {
    const size_t max_len = 4096;
    char buffer[4096] = {0};
    struct iovec local, remote;
    local.iov_base  = buffer;
    local.iov_len   = max_len - 1;
    remote.iov_base = reinterpret_cast<void*>(address);
    remote.iov_len  = max_len - 1;
    ssize_t bytes = syscall(SYS_process_vm_readv, game_pid, &local, 1, &remote, 1, 0);
    if (bytes > 0) buffer[bytes < (ssize_t)(max_len) ? bytes : (ssize_t)(max_len - 1)] = '\0';
    return std::string(buffer);
}

// -- Unified Write<T>: kernel driver if available, else normal -
template<typename T>
bool Write(uintptr_t address, T value) {
    if (g_driver != nullptr)
        return g_driver->write(address, &value, sizeof(T));
    return _vm_write<T>(address, value);
}

uintptr_t getMemoryAddr(uintptr_t address) {
    return Read<uintptr_t>(address);
}

uintptr_t ReadPtr(uintptr_t addr) {
    return Read<uintptr_t>(addr);
}

uintptr_t ReadChain(uintptr_t base, std::initializer_list<uintptr_t> offsets) {
    uintptr_t addr = base;
    for (auto off : offsets) {
        addr = ReadPtr(addr + off);
        if (!addr) return 0;
    }
    return addr;
}

struct D3DMatrix {
    float _11, _12, _13, _14;
    float _21, _22, _23, _24;
    float _31, _32, _33, _34;
    float _41, _42, _43, _44;
};

struct Vector4 {
    float X;
    float Y;
    float Z;
    float W;
};

class MonoDictionary {
public:
    uintptr_t getValues() {
        return Read<uintptr_t>(reinterpret_cast<uintptr_t>(this) + 0x28) + 0x20;
    }
    
    int getNumValues() {
        return Read<int>(reinterpret_cast<uintptr_t>(this) + 0x38);
    }
};

uintptr_t string2Offset(const char *c) {
    int base = 16;
    
    // Safe conversion for all architectures
    std::string str(c);
    if (str.empty()) return 0;
    
    // Remove "0x" prefix if present
    if (str.size() > 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str = str.substr(2);
    }
    
    uintptr_t result = 0;
    std::stringstream ss;
    ss << std::hex << str;
    ss >> result;
    
    return result;
}

Quaternion GetRotationToTheLocation(Vector3 Target, float Height, Vector3 MyEnemy) {
    return Quaternion::LookRotation((Target + Vector3(0, Height, 0)) - MyEnemy, Vector3(0, 1, 0));
}

void getUTF8(char* dst, uintptr_t addr) {
    if (addr < 0x10000000) {
        dst[0] = '\0';
        return;
    }

    int stringLen = Read<int>(addr + 0x10);
    if (stringLen <= 0 || stringLen > 64) {
        stringLen = 32; 
    }

    unsigned char raw[128]; 
    for (int i = 0; i < (stringLen * 2); i++) {
        raw[i] = Read<unsigned char>(addr + 0x14 + i);
    }

    int j = 0;
    for (int i = 0; i < (stringLen * 2); i += 2) {
        unsigned short unicode = raw[i] | (raw[i + 1] << 8);
        if (unicode == 0) break;
        if (unicode < 0x80) {
            dst[j++] = (char)unicode;
        } else if (unicode < 0x800) {
            dst[j++] = (char)((unicode >> 6) | 0xC0);
            dst[j++] = (char)((unicode & 0x3F) | 0x80);
        } else if (unicode >= 0xD800 && unicode <= 0xDBFF) { 
            i += 2;
            unsigned short low = raw[i] | (raw[i + 1] << 8);
            unsigned int codepoint = 0x10000 + ((unicode - 0xD800) << 10) + (low - 0xDC00);
            dst[j++] = (char)((codepoint >> 18) | 0xF0);
            dst[j++] = (char)(((codepoint >> 12) & 0x3F) | 0x80);
            dst[j++] = (char)(((codepoint >> 6) & 0x3F) | 0x80);
            dst[j++] = (char)((codepoint & 0x3F) | 0x80);
        } else {
            dst[j++] = (char)((unicode >> 12) | 0xE0);
            dst[j++] = (char)(((unicode >> 6) & 0x3F) | 0x80);
            dst[j++] = (char)((unicode & 0x3F) | 0x80);
        }
        if (j >= 60) break; 
    }
    dst[j] = '\0';
}

void DrawRoundedBox(float x, float y, float width, float height, ImU32 color, float thickness) {
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    float cornerRadius = 7.0f;
    
    // --- 1. Draw the four sides ---
    
    // Top side
    draw_list->AddLine(ImVec2(x + cornerRadius, y), ImVec2(x + width - cornerRadius, y), color, thickness);
    
    // Right side
    draw_list->AddLine(ImVec2(x + width, y + cornerRadius), ImVec2(x + width, y + height - cornerRadius), color, thickness);
    
    // Bottom side
    draw_list->AddLine(ImVec2(x + cornerRadius, y + height), ImVec2(x + width - cornerRadius, y + height), color, thickness);
    
    // Left side
    draw_list->AddLine(ImVec2(x, y + cornerRadius), ImVec2(x, y + height - cornerRadius), color, thickness);

    // --- 2. Draw quarter circles for rounded corners ---
    
    int segments = 16; 
    float segmentAngle = 90.0f / (segments / 4); // Protite corner er jonno 90 degree
    const float PI = 3.1415926535f;

    // Top-left corner (180-270 degrees)
    for (int i = 0; i < segments / 4; i++) {
        float angle1 = (180.0f + (i * segmentAngle)) * (PI / 180.0f);
        float angle2 = (180.0f + ((i + 1) * segmentAngle)) * (PI / 180.0f);
        
        ImVec2 p1 = ImVec2(x + cornerRadius + cosf(angle1) * cornerRadius, y + cornerRadius + sinf(angle1) * cornerRadius);
        ImVec2 p2 = ImVec2(x + cornerRadius + cosf(angle2) * cornerRadius, y + cornerRadius + sinf(angle2) * cornerRadius);
        draw_list->AddLine(p1, p2, color, thickness);
    }

    // Top-right corner (270-360 degrees)
    for (int i = 0; i < segments / 4; i++) {
        float angle1 = (270.0f + (i * segmentAngle)) * (PI / 180.0f);
        float angle2 = (270.0f + ((i + 1) * segmentAngle)) * (PI / 180.0f);
        
        ImVec2 p1 = ImVec2(x + width - cornerRadius + cosf(angle1) * cornerRadius, y + cornerRadius + sinf(angle1) * cornerRadius);
        ImVec2 p2 = ImVec2(x + width - cornerRadius + cosf(angle2) * cornerRadius, y + cornerRadius + sinf(angle2) * cornerRadius);
        draw_list->AddLine(p1, p2, color, thickness);
    }

    // Bottom-right corner (0-90 degrees)
    for (int i = 0; i < segments / 4; i++) {
        float angle1 = (0.0f + (i * segmentAngle)) * (PI / 180.0f);
        float angle2 = (0.0f + ((i + 1) * segmentAngle)) * (PI / 180.0f);
        
        ImVec2 p1 = ImVec2(x + width - cornerRadius + cosf(angle1) * cornerRadius, y + height - cornerRadius + sinf(angle1) * cornerRadius);
        ImVec2 p2 = ImVec2(x + width - cornerRadius + cosf(angle2) * cornerRadius, y + height - cornerRadius + sinf(angle2) * cornerRadius);
        draw_list->AddLine(p1, p2, color, thickness);
    }

    // Bottom-left corner (90-180 degrees)
    for (int i = 0; i < segments / 4; i++) {
        float angle1 = (90.0f + (i * segmentAngle)) * (PI / 180.0f);
        float angle2 = (90.0f + ((i + 1) * segmentAngle)) * (PI / 180.0f);
        
        ImVec2 p1 = ImVec2(x + cornerRadius + cosf(angle1) * cornerRadius, y + height - cornerRadius + sinf(angle1) * cornerRadius);
        ImVec2 p2 = ImVec2(x + cornerRadius + cosf(angle2) * cornerRadius, y + height - cornerRadius + sinf(angle2) * cornerRadius);
        draw_list->AddLine(p1, p2, color, thickness);
    }
}

void DrawHealthBar(float x, float y, float width, float height, int health) {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    if (health < 0) health = 0;
    if (health > 200) health = 200;

    float hpPercent = health / 200.0f;
    float barHeight = height * hpPercent;

    ImU32 hpColor;

    // 🔥 Color logic
    if (hpPercent > 0.6f) {
        // Green
        hpColor = IM_COL32(0, 255, 0, 255);
    }
    else if (hpPercent > 0.3f) {
        // Yellow
        hpColor = IM_COL32(255, 255, 0, 255);
    }
    else {
        // Red
        hpColor = IM_COL32(255, 0, 0, 255);
    }

    // Background
    draw->AddRectFilled(
        ImVec2(x, y),
        ImVec2(x + width, y + height),
        IM_COL32(0, 0, 0, 180)
    );

    // HP fill
    draw->AddRectFilled(
        ImVec2(x, y + (height - barHeight)),
        ImVec2(x + width, y + height),
        hpColor
    );

    // Border
    draw->AddRect(
        ImVec2(x, y),
        ImVec2(x + width, y + height),
        IM_COL32(0, 0, 0, 255)
    );
}

