#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace elf_module {
constexpr size_t max_rows = 8192, max_candidates = 256, max_phdrs = 64;
struct Mapping {
    uint64_t start{}, end{}, offset{}, inode{};
    unsigned major{}, minor{};
    bool readable{}, executable{};
    std::string path;
};
struct Snapshot { std::vector<Mapping> rows; bool complete = true; };
inline Snapshot read_maps(FILE* file) {
    Snapshot snapshot;
    if (!file) { snapshot.complete = false; return snapshot; }
    char line[4096];
    while (std::fgets(line, sizeof(line), file)) {
        if (!std::strchr(line, '\n') && !std::feof(file)) {
            snapshot.complete = false;
            break;
        }
        if (snapshot.rows.size() == max_rows) { snapshot.complete = false; break; }
        unsigned long long start, end, offset, inode;
        unsigned major, minor; char perms[5]{}; int used = 0;
        if (std::sscanf(line, "%llx-%llx %4s %llx %x:%x %llu %n", &start, &end,
                        perms, &offset, &major, &minor, &inode, &used) != 7 || used <= 0 || end <= start) continue;
        Mapping row{start, end, offset, inode, major, minor, perms[0] == 'r', perms[2] == 'x', {}};
        row.path.assign(line + used, std::strcspn(line + used, "\r\n"));
        constexpr char suffix[] = " (deleted)";
        if (row.path.size() >= sizeof(suffix)-1 &&
            row.path.compare(row.path.size()-(sizeof(suffix)-1), sizeof(suffix)-1, suffix) == 0)
            row.path.resize(row.path.size()-(sizeof(suffix)-1));
        snapshot.rows.push_back(std::move(row));
    }
    if (std::ferror(file)) snapshot.complete = false;
    return snapshot;
}
inline bool matches(const Mapping& row, const char* name) {
    if (!name || !*name || !row.inode) return false;
    if (std::strchr(name, '/')) return row.path == name;
    auto slash = row.path.find_last_of('/');
    return row.path.substr(slash == std::string::npos ? 0 : slash+1) == name;
}
inline const Mapping* containing(const Snapshot& maps, uint64_t address) {
    auto it = std::upper_bound(maps.rows.begin(), maps.rows.end(), address,
        [](uint64_t value, const Mapping& row) { return value < row.start; });
    if (it == maps.rows.begin()) return nullptr;
    --it;
    return address < it->end ? &*it : nullptr;
}
inline bool user_range(uint64_t address, uint64_t size) {
    return address >= 0x10000 && size && address < (1ULL << 48) && size <= (1ULL << 48)-address;
}
inline bool readable(const Snapshot& maps, uint64_t address, uint64_t size) {
    if (!user_range(address, size)) return false;
    uint64_t end = address + size;
    while (address < end) {
        auto row = containing(maps, address);
        if (!row || !row->readable) return false;
        address = std::min(end, row->end);
    }
    return true;
}

// ELF64 wire layout, independent of host SDK headers for desktop regression tests.
struct Header {
    unsigned char ident[16]; uint16_t type, machine; uint32_t version;
    uint64_t entry, phoff, shoff; uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct Program {
    uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align;
};
struct Dynamic { int64_t tag;uint64_t value; };
static_assert(sizeof(Header) == 64 && sizeof(Program) == 56, "ELF64 wire layout");
struct Chain { uint64_t facade_rva{}, static_offset{}, current_game_offset{}; };
struct Result {
    uint64_t base{}; size_t examined{}, valid{}; bool incomplete{};
};
inline bool apk_mapping(const Mapping& row){
    return row.inode && row.path.size()>4 && row.path.compare(row.path.size()-4,4,".apk")==0;
}
template<class Read>
bool soname_matches(const Snapshot& maps,Read& read,uint64_t bias,
                    const std::vector<Program>& programs,const char* name){
    uint64_t table=0,strings=0,soname=UINT64_MAX;
    for(const auto& p:programs){
        if(p.type!=2)continue;
        const uint64_t bytes=std::min<uint64_t>(p.filesz,256*sizeof(Dynamic));
        if(bytes<sizeof(Dynamic)||p.vaddr>UINT64_MAX-bias||!readable(maps,bias+p.vaddr,bytes))return false;
        Dynamic entries[256]{};
        if(!read(bias+p.vaddr,entries,static_cast<size_t>(bytes)))return false;
        for(size_t i=0;i<bytes/sizeof(Dynamic);++i){
            const auto& d=entries[i];if(!d.tag)break;
            if(d.tag==5)table=d.value;
            if(d.tag==10)strings=d.value;
            if(d.tag==14)soname=d.value;
        }
        break;
    }
    if(!table||!strings||soname>=strings)return false;
    auto in_image=[&](uint64_t at,uint64_t bytes){
        for(const auto& p:programs)if(p.type==1 && (p.flags&4) && p.vaddr<=UINT64_MAX-bias){
            const uint64_t begin=bias+p.vaddr;
            if(at>=begin && at-begin<p.memsz && bytes<=p.memsz-(at-begin) && readable(maps,at,bytes))return true;
        }
        return false;
    };
    // Android dynamic pointers may be image-relative or already relocated.
    if(!in_image(table,strings)){
        if(table>UINT64_MAX-bias)return false;
        table+=bias;if(!in_image(table,strings))return false;
    }
    if(soname>UINT64_MAX-table)return false;
    const size_t bytes=static_cast<size_t>(std::min<uint64_t>(strings-soname,256));
    char text[256]{};
    if(!in_image(table+soname,bytes)||!read(table+soname,text,bytes)||!std::memchr(text,0,bytes))return false;
    const char* leaf=std::strrchr(name,'/');leaf=leaf?leaf+1:name;
    return std::strcmp(text,leaf)==0;
}

template<class Read>
bool chain_valid(const Snapshot& maps, Read& read, uint64_t slot, const Chain& chain) {
    uint64_t facade = 0;
    if (!readable(maps, slot, 8) || !read(slot, &facade, 8) || !user_range(facade, 8) || (facade & 7)) return false;
    auto check_class = [&](uint64_t klass) {
        if (!user_range(klass, chain.static_offset + 8)) return false;
        uint64_t fields = 0, game = 0;
        if (!readable(maps, klass + chain.static_offset, 8) ||
            !read(klass + chain.static_offset, &fields, 8) || !user_range(fields, 8) || (fields & 7) ||
            !user_range(fields, chain.current_game_offset + 8) ||
            !readable(maps, fields + chain.current_game_offset, 8) ||
            !read(fields + chain.current_game_offset, &game, 8)) return false;
        // A lobby may legitimately have no current game. Do not require a match/player.
        return game == 0 || (!(game & 7) && readable(maps, game, 8));
    };
    if (check_class(facade)) return true;
    uint64_t indirect = 0;
    return readable(maps, facade, 8) && read(facade, &indirect, 8) &&
           user_range(indirect, 8) && !(indirect & 7) && check_class(indirect);
}

template<class Read, class Log, class Validate>
Result resolve_validated(const Snapshot& maps, const char* name, uint64_t page_size,
                         const Chain& chain, Read read, Log log, Validate validate,bool allow_apk=false) {
    Result result;
    if (!maps.complete || !page_size || (page_size & (page_size-1)) || !chain.facade_rva ||
        chain.facade_rva >= (1ULL << 40) || chain.static_offset > 0x10000 || chain.current_game_offset > 0x10000) {
        result.incomplete = true; return result;
    }
    uint64_t mask = page_size-1;
    std::vector<uint64_t> accepted;
    for (const auto& candidate : maps.rows) {
        const bool named=matches(candidate,name);
        if (!candidate.readable || (!named && !(allow_apk&&apk_mapping(candidate)))) continue;
        if (result.examined == max_candidates) { result.incomplete = true; break; }
        ++result.examined;
        Header header{};
        if (!readable(maps, candidate.start, sizeof(header)) || !read(candidate.start, &header, sizeof(header)) ||
            std::memcmp(header.ident, "\177ELF", 4) != 0) continue;
        auto reject = [&](const char* reason) { log(candidate.start, reason); };
        if (header.ident[4] != 2 || header.ident[5] != 1 || header.ident[6] != 1 ||
            header.type != 3 || header.machine != 183 || header.version != 1 ||
            header.ehsize != sizeof(Header) || header.phentsize != sizeof(Program) ||
            !header.phnum || header.phnum > max_phdrs || header.phoff < sizeof(Header) || header.phoff > 0x10000) {
            reject("invalid/unsupported ELF64 header"); continue;
        }
        std::vector<Program> programs(header.phnum);
        uint64_t bytes = programs.size()*sizeof(Program);
        if (!user_range(candidate.start, header.phoff + bytes) ||
            !readable(maps, candidate.start + header.phoff, bytes) ||
            !read(candidate.start + header.phoff, programs.data(), static_cast<size_t>(bytes))) {
            reject("program headers not resident/readable"); continue;
        }
        uint64_t bias = 0; bool have_bias = false, malformed = false;
        for (const auto& p : programs) {
            if (p.type != 1) continue;
            if (p.filesz > p.memsz || p.memsz > (1ULL << 40) || p.vaddr > (1ULL << 48) ||
                p.offset > UINT64_MAX-p.filesz || p.vaddr > UINT64_MAX-p.memsz ||
                (p.vaddr & mask) != (p.offset & mask) ||
                (p.align > 1 && ((p.align & (p.align-1)) || p.vaddr % p.align != p.offset % p.align))) {
                malformed = true; break;
            }
            if (p.offset == 0 && p.filesz >= header.phoff+bytes && candidate.start >= p.vaddr) {
                uint64_t proposed = candidate.start-p.vaddr;
                if (have_bias && proposed != bias) { malformed = true; break; }
                bias = proposed; have_bias = true;
            }
        }
        if (!have_bias || malformed || !user_range(bias, chain.facade_rva+8)) {
            reject("invalid PT_LOAD layout/load bias"); continue;
        }
        bool layout_ok = true, facade_in_load = false, have_exec = false;
        for (const auto& p : programs) {
            if (p.type != 1 || !p.memsz) continue;
            uint64_t begin = bias+p.vaddr;
            if (!user_range(begin, p.memsz)) { layout_ok = false; break; }
            uint64_t end = begin+p.memsz, cursor = begin;
            if (p.flags & 1) have_exec = true;
            uint64_t slot = bias+chain.facade_rva;
            if ((p.flags & 4) && slot >= begin && slot <= end && end-slot >= 8) facade_in_load = true;
            while (cursor < end) {
                const auto* row = containing(maps, cursor);
                if (!row || ((p.flags & 4) && !row->readable) || ((p.flags & 1) && !row->executable)) {
                    layout_ok = false; break;
                }
                if (cursor-begin < p.filesz) {
                    uint64_t segment_page = begin & ~mask;
                    uint64_t file_page = p.offset & ~mask;
                    uint64_t delta = cursor-segment_page;
                    if (file_page > UINT64_MAX-candidate.offset ||
                        delta > UINT64_MAX-(candidate.offset+file_page) ||
                        cursor-row->start > UINT64_MAX-row->offset) { layout_ok = false; break; }
                    uint64_t expected = candidate.offset+file_page+delta;
                    if (row->inode != candidate.inode || row->major != candidate.major || row->minor != candidate.minor ||
                        row->path != candidate.path || row->offset+(cursor-row->start) != expected) {
                        layout_ok = false; break;
                    }
                }
                cursor = std::min(end, row->end);
            }
            if (!layout_ok) break;
        }
        if (!layout_ok || !have_exec || !facade_in_load) {
            reject("segment mappings or anchor RVA do not match image"); continue;
        }
        if(!named && !soname_matches(maps,read,bias,programs,name)){
            reject("APK image SONAME does not identify requested library");continue;
        }
        if (!validate(maps, read, bias)) {
            reject("module anchor not ready or invalid"); continue;
        }
        if (std::find(accepted.begin(), accepted.end(), bias) == accepted.end()) accepted.push_back(bias);
        log(bias, "ELF layout and module anchor validated");
    }
    result.valid = accepted.size();
    if (!result.incomplete && accepted.size() == 1) result.base = accepted.front();
    return result;
}

template<class Read, class Log>
Result resolve(const Snapshot& maps, const char* name, uint64_t page_size,
               const Chain& chain, Read read, Log log,bool allow_apk=false) {
    return resolve_validated(maps, name, page_size, chain, read, log,
        [&](const Snapshot& snapshot, Read& reader, uint64_t bias) {
            return chain_valid(snapshot, reader, bias+chain.facade_rva, chain);
        },allow_apk);
}


}