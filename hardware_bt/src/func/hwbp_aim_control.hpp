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
inline bool record_matches(const Point& point,uint64_t base,uint64_t pc,uint64_t local,
                           uint64_t info,uint64_t adjusted,uint64_t x0,uint64_t x1,uint64_t x8){
    if(pc!=base+point.rva)return false;
    // HWBP X: quaternion path - x0 must belong to local player
    if(point.quaternion){
        if(x0!=0 && local!=0 && x0!=local) return false;
        return true;
    }
    // HWBP Y: At 0x7C5405C, x0 holds the raycast helper object (NOT the player).
    // Only x1 reliably identifies our local player's AimInfo context.
    if(point.rva==0x7C5405C)
        return x1==local+0xE58 || x1==local+0xE60 || (info&&x1==info) || (adjusted&&x1==adjusted);
    // 0x7C56530 alternate output: x8 holds the AimInfo pointer
    return (info&&x8==info) || (adjusted&&x8==adjusted);
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
