#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <vector>
#include <string>

namespace native_memory {
// Android tagged user pointers use the low 48 VA bits on these supplied ABIs.
inline bool range(uintptr_t address,size_t size,uintptr_t& normalized){
    if(address&UINT64_C(0x00ff000000000000))return false;
    normalized=address&UINT64_C(0x0000ffffffffffff);
    return normalized>=0x1000 && size && size<=UINT64_C(0x1000000000000)-normalized;
}
template<class Transfer>
bool pages(uintptr_t address,void* buffer,size_t size,size_t page_size,bool writing,Transfer transfer){
    uintptr_t normalized=0;
    if(!buffer||!range(address,size,normalized)||!page_size||(page_size&(page_size-1))){errno=EINVAL;return false;}
    auto* bytes=static_cast<unsigned char*>(buffer);
    for(size_t done=0;done<size;){
        const auto current=normalized+done;
        const size_t chunk=std::min({size-done,page_size-(current&(page_size-1)),size_t(4096)});
        if(!transfer(current,bytes+done,chunk)){
            const int error=errno?errno:EIO;
            if(!writing)std::memset(buffer,0,size);
            errno=error;return false;
        }
        done+=chunk;
    }
    return true;
}
template<class Read,class Write>
bool self_check(size_t page_size,Read read,Write write,std::string& stage){
    if(page_size<4096||page_size>65536||(page_size&(page_size-1))){stage="invalid page size";errno=EINVAL;return false;}
    const size_t bytes=page_size*2+37;
    std::vector<unsigned char> arena(bytes+29),expected(bytes),observed(bytes,0xa5);
    auto* target=arena.data()+13;
    for(size_t i=0;i<bytes;++i)target[i]=static_cast<unsigned char>((i*37+11)&255);
    std::memcpy(expected.data(),target,bytes);
    const auto address=reinterpret_cast<uintptr_t>(target);
    stage="cross-page read";
    if(!pages(address,observed.data(),bytes,page_size,false,read)||observed!=expected){if(!errno)errno=EIO;return false;}
    for(auto& byte:expected)byte^=0x6d;
    stage="cross-page write";
    if(!pages(address,expected.data(),bytes,page_size,true,write))return false;
    const volatile unsigned char* actual=target;
    for(size_t i=0;i<bytes;++i)if(actual[i]!=expected[i]){errno=EIO;stage="write acknowledged but own memory unchanged";return false;}
    stage="cross-page readback";std::fill(observed.begin(),observed.end(),0xa5);
    if(!pages(address,observed.data(),bytes,page_size,false,read)||observed!=expected){if(!errno)errno=EIO;return false;}
    stage="cross-page read/write/readback verified";return true;
}
}
