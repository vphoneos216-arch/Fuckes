#pragma once
#include "breakpoint_rewrite.hpp"

#include <algorithm>

namespace hwbp_aim {
inline bool retain_target(float locked_distance,float best_distance) {
    return !(best_distance < locked_distance*0.65f && locked_distance-best_distance > 20.f);
}
struct Settings {
    // fire_check is always on now -- it only gates Aimbot's (X) rotation
    // write, has no menu toggle, and defaults true with no way to turn it
    // off (see hwbp_aim.hpp's step(), per-point loop).
    bool x=false, y=false, fire_check=true, draw_fov=true;
    int x_option=0, y_option=0, bone=0;
    float fov=200, range=250, lead_ms=16;
};
struct Point {uint64_t rva=0; bool quaternion=false;};

// Every pointer the local player's own two aim-info slots have been seen
// to hold recently, plus the slot addresses themselves.
//
// This is the fix for "stationary = headshot, moving = bullet doesn't
// connect". The Y context guard used to require that the context
// register captured at the raycast equal the pointer value read out of
// local+0xE58 / local+0xE60 in THIS poll. The engine does not keep one
// AimInfo instance for the whole match: it moves between the raw slot
// and the adjusted slot, and the pointer inside a slot is replaced as
// the player's own movement/aim state changes. So while standing still
// the live read and the captured register agree and every shot is
// redirected; the moment the player moves, they disagree, the guard
// rejects the hit, and the armed write is torn down -- the bullet then
// flies with the game's own direction, which is exactly "redirect
// dikhta hai kabhi kabhi, hit nahi hota".
//
// Keeping the recently-observed values preserves exactly what the guard
// is supposed to prove -- this hit belongs to one of OUR aim-info
// objects and not to another entity's raycast -- without demanding it
// be the instance that happened to be current a few microseconds ago.
// Nothing is invented or guessed: every value in this set was read out
// of the local player's own two slots at some point in the last few
// polls, and the set is cleared whenever the local player changes.
struct AimContext {
    static constexpr unsigned kMax=10;
    std::array<uint64_t,kMax> values{};
    unsigned count=0;
    void clear(){values={};count=0;}
    void add(uint64_t value){
        if(!value)return;
        for(unsigned i=0;i<count;++i)if(values[i]==value)return;
        if(count<kMax){values[count++]=value;return;}
        for(unsigned i=1;i<kMax;++i)values[i-1]=values[i]; // drop oldest, keep newest
        values[kMax-1]=value;
    }
    bool holds(uint64_t value) const {
        if(!value)return false;
        for(unsigned i=0;i<count;++i)if(values[i]==value)return true;
        return false;
    }
};

inline bool record_matches(const Point& point,uint64_t base,uint64_t pc,uint64_t local,
                           const AimContext& context,uint64_t x0,uint64_t x1,uint64_t x8){
    if(pc!=base+point.rva)return false;
    // HWBP X: quaternion path - x0 must belong to local player
    if(point.quaternion){
        if(x0!=0 && local!=0 && x0!=local) return false;
        return true;
    }
    // HWBP Y: At 0x7C5405C, x0 holds the raycast helper object (NOT the player).
    // Only x1 reliably identifies our local player's AimInfo context.
    if(point.rva==0x7C5405C)return context.holds(x1);
    // 0x7C56530 alternate output: x8 holds the AimInfo pointer
    return context.holds(x8);
}
class Motion {
    std::array<float,3> previous_{},velocity_{};
    uint64_t sequence_=0;
    double sampled_=0;
public:
    void reset(){*this=Motion{};}
    std::array<float,3> observe(std::array<float,3> p,uint64_t sequence,double sampled){
        if(sequence==sequence_)return velocity_;
        for(float value:p)if(!std::isfinite(value)){reset();return {};}
        const double dt=sampled-sampled_;
        if(sequence_&&dt>=0.005&&dt<=0.12){
            std::array<float,3> raw{};float square=0,dot=0;
            for(unsigned i=0;i<3;++i){raw[i]=float((p[i]-previous_[i])/dt);square+=raw[i]*raw[i];dot+=raw[i]*velocity_[i];}
            if(square>1600)velocity_={};
            else{
                float alpha=float(1-std::exp(-dt/(square<0.0625?0.01:0.02)));
                if(dot<0)alpha=std::max(alpha,0.9f);
                for(unsigned i=0;i<3;++i)velocity_[i]+=alpha*(raw[i]-velocity_[i]);
            }
        }else velocity_={};
        previous_=p;sequence_=sequence;sampled_=sampled;return velocity_;
    }
};
inline std::array<Point,2> plan(const Settings& settings, unsigned& count) {
    std::array<Point,2> points{}; count=0;
    if(settings.x) points[count++]={settings.x_option==0 ? 0x7014464ULL : 0x7014510ULL,true};
    if(settings.y) points[count++]={settings.y_option==0 ? 0x7C5405CULL : 0x7C56530ULL,false};
    return points;
}
inline bool same_plan(const Settings& a,const Settings& b) {
    return a.x==b.x && a.y==b.y && (!a.x||a.x_option==b.x_option) && (!a.y||a.y_option==b.y_option);
}
inline bool fresh(double now,double sample) {
    // Back to 0.12s. I tightened this to 0.04 last change and that was
    // the wrong lever: a failed fresh() check does not "skip aiming at
    // a stale position", it goes down target_wait() ->
    // capture_without_writes(), which REMOVES the armed register write
    // entirely. A removed write means the bullet flies with the game's
    // own direction, i.e. a guaranteed miss -- strictly worse than
    // keeping a head direction that is a few milliseconds old. Tighter
    // thresholds therefore produce MORE misses while moving, not
    // fewer, because the scan thread jitters most exactly when the
    // player is moving. The real cause of the moving-vs-stationary
    // split is the context guard below, not sample age.
    return std::isfinite(now)&&std::isfinite(sample)&&sample>0&&now>=sample&&now-sample<=0.12;
}
inline std::array<float,3> predicted(std::array<float,3> world,std::array<float,3> velocity,
                                    double age,float lead_ms) {
    const float horizon=float(std::clamp(age+std::clamp(double(lead_ms),0.0,100.0)/1000.0,0.0,0.12));
    float length=0;
    std::array<float,3> shift{};
    for(unsigned i=0;i<3;++i){shift[i]=velocity[i]*horizon;length+=shift[i]*shift[i];}
    // Limit extrapolation through abrupt movement, teleports, or bone jitter.
    const float scale=length>1.0f ? 1.0f/std::sqrt(length) : 1.0f;
    for(unsigned i=0;i<3;++i)world[i]+=shift[i]*scale;
    return world;
}
}
