#pragma once
#include "release_log.hpp"
// TEMPORARY TERMINAL DEBUG: change this 1 to 0 to disable every trace.
#ifndef OFFSET_CHAIN_DEBUG
#define OFFSET_CHAIN_DEBUG 0
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include "VersionProfiles.hpp"

namespace offset_chain_debug {
inline constexpr std::int64_t intervalMs=2000;
inline std::array<std::atomic<std::int64_t>,2> last{};
enum Reject { InvalidEntity,AvatarManager,Avatar,TeamData,Team,Dead,PhyX,CameraTransform,
              Health,HeadTransform,HeadPosition,FeetTransform,FeetPosition,Distance,HeadScreen,FeetScreen,Count };
inline constexpr const char* rejectNames[]={"invalid/self entity","avatar manager","avatar object","team data",
 "team flag","dead flag","knock/PhyXdata","camera transform","HP <= 0","head transform","head position",
 "feet transform","feet position","range","head offscreen","feet offscreen"};
using Reader=bool(*)(std::uint64_t,void*,std::size_t);
class Trace {
    bool active_{};
    const char* channel_;
    const char* stage_="starting";
    Reader reader_{};
    unsigned lines_{};
    std::array<unsigned,Count> rejected_{};
public:
    Trace(unsigned channel,Reader reader=nullptr):channel_(channel?"CHAIN":"ATTACH"),reader_(reader){
        if constexpr(TOUCHBASE_DIAGNOSTICS && OFFSET_CHAIN_DEBUG){
            const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            auto previous=last[channel].load();
            active_=now-previous>=intervalMs&&last[channel].compare_exchange_strong(previous,now);
        }
        note("%s %s package=%s",versions::selected->label,versions::selected->version,versions::selected->package);
    }
    ~Trace(){
        note("END stage=%s",stage_);
        for(unsigned i=0;i<Count;++i)if(rejected_[i])note("rejected %-22s count=%u",rejectNames[i],rejected_[i]);
        if(active_)std::fflush(stderr);
    }
    void stage(const char* stage){stage_=stage;}
    template<class... Args>void note(const char* format,Args... args){
        if(!active_||lines_++>=72)return;
        char message[768];
        if constexpr(sizeof...(args)==0)std::snprintf(message,sizeof(message),"%s",format);
        else std::snprintf(message,sizeof(message),format,args...);
        std::fprintf(stderr,"[%s] %s\n",channel_,message);
    }
    template<class T>T read(const char* name,std::uint64_t address){
        T value{};
        const bool ok=reader_&&reader_(address,&value,sizeof(value));
        if(!ok)value=T{};
        note("%-24s @0x%llx -> 0x%llx [%s]",name,
             static_cast<unsigned long long>(address),static_cast<unsigned long long>(value),ok?"read OK":"READ FAILED");
        return value;
    }
    void reject(Reject reason,std::uint64_t entity,std::uint64_t value,std::uint64_t offset){
        if(!active_)return;
        if(rejected_[reason]++==0)note("first reject %-20s entity=0x%llx offset=+0x%llx value=0x%llx",
            rejectNames[reason],static_cast<unsigned long long>(entity),static_cast<unsigned long long>(offset),static_cast<unsigned long long>(value));
    }
};
}
