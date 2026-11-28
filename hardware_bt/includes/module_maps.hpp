#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>

namespace module_maps {
// Android shared-library RVAs are relative to the ELF image's header mapping.
// ELF-looking bytes in a later segment do not identify that image's base.
inline bool header_mapping(const char* line, const char* name, uintptr_t& base) {
    if (!line || !name || !*name) return false;
    unsigned long long start = 0, end = 0, offset = 0, inode = 0;
    unsigned major = 0, minor = 0;
    char perms[5]{};
    int consumed = 0;
    if (std::sscanf(line, "%llx-%llx %4s %llx %x:%x %llu %n",
                    &start, &end, perms, &offset, &major, &minor, &inode, &consumed) != 7 ||
        consumed <= 0 || !start || end <= start || offset != 0 || !inode || perms[0] != 'r' ||
        start > std::numeric_limits<uintptr_t>::max()) return false;

    const char* path = line + consumed;
    size_t length = std::strcspn(path, "\r\n");
    constexpr char deleted[] = " (deleted)";
    if (length >= sizeof(deleted) - 1 &&
        std::memcmp(path + length - (sizeof(deleted) - 1), deleted, sizeof(deleted) - 1) == 0)
        length -= sizeof(deleted) - 1;
    const char* leaf = path;
    if (!std::strchr(name, '/')) {
        for (size_t i = 0; i < length; ++i) if (path[i] == '/') leaf = path + i + 1;
    }
    size_t leaf_length = length - static_cast<size_t>(leaf - path);
    if (std::strlen(name) != leaf_length || std::memcmp(leaf, name, leaf_length) != 0) return false;
    base = static_cast<uintptr_t>(start);
    return true;
}

inline uintptr_t find_header_mapping(FILE* maps, const char* name) {
    if (!maps) return 0;
    char line[4096];
    while (std::fgets(line, sizeof(line), maps)) {
        // Discard truncated rows instead of matching a partial pathname.
        if (!std::strchr(line, '\n') && !std::feof(maps)) {
            int c;
            while ((c = std::fgetc(maps)) != '\n' && c != EOF) {}
            continue;
        }
        uintptr_t base = 0;
        if (header_mapping(line, name, base)) return base;
    }
    return 0;
}
}