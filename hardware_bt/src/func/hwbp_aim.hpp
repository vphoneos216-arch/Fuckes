#pragma once
#include "hwbp_aim_control.hpp"
#include "breakpoint_capture.hpp"
#include "../game/math.hpp"
#include <atomic>
#include <thread>

namespace hwbp_aim {

// Forces the current weapon's recoil float to 0 -- same chain as
// misc.hpp's No Recoil (invMgr -> activeItem -> wcomp -> NoRecoil
// float), duplicated here (not shared/called across files) so Bullet
// Track's own correctness doesn't depend on whatever state the Misc
// tab's separate No Recoil toggle happens to be in. Bullet Track (Y)
// computes one aim direction per poll and writes it once; the game's
// own recoil pattern then nudges the ACTUAL fired shot away from that
// exact direction on every following round in a burst (climbing
// spread), which is exactly what reads as "redirects, but a lot of
// bullets still miss/recoil off the body". So whenever Y is on, this
// is called every poll, independent of the Misc toggle, to keep the
// weapon's own recoil contribution at zero for as long as Bullet
// Track is active -- Y's single computed direction then really is
// where every round in the burst goes.
static inline void force_zero_weapon_recoil(uint64_t local) {
    if (!local || local <= 0x10000ULL) return;
    uint64_t invMgr = rpm<uint64_t>(local + Offsets::InventoryManager);
    if (!invMgr || invMgr <= 0x10000ULL) return;
    uint64_t activeItem = rpm<uint64_t>(invMgr + Offsets::Item);
    if (!activeItem || activeItem <= 0x10000ULL) return;
    uint64_t wcomp = rpm<uint64_t>(activeItem + Offsets::WeaponComponent);
    if (!wcomp || wcomp <= 0x10000ULL) return;
    wpm<float>(wcomp + Offsets::NoRecoil, 0.0f);
}

struct Status {
    Settings configuration{};
    std::string text="OFF", x="OFF", y="OFF";
    uint64_t target=0, x_hits=0, y_hits=0, updates=0;
    int pid=0;
    uint64_t base=0,local=0;
    Vector3 origin{}, target_point{}, velocity{};
    uint64_t fire_address=0;
    bool fire_read_ok=false;
    uint8_t fire_value=0;
};
class Controller {
    std::mutex mutex_;
    Settings settings_{};
    Status status_{};
    std::atomic<bool> running_{true};
    std::thread worker_;
    std::array<Point,2> points_{};
    unsigned count_=0;
    bool armed_=false;
    bool templates_written_=false;
    int pid_=0;
    uint64_t base_=0, locked_=0;
    Motion motion_;
    int tracking_bone_=-1;
    uint64_t tracking_local_=0;
    Settings applied_{};
    bp_capture::State exporter_;
    std::array<std::string,2> snapshots_{};
    std::array<std::string,2> published_snapshots_{};
    std::array<double,2> snapshot_time_{};

    static bool finite(Vector3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
    static bool read_pointer(uint64_t at,uint64_t& p){
        return driver->read(at,&p,sizeof(p))&&bp_capture::valid_pointer(p);
    }
    bool disarm(Status& status) {
        if(!armed_)return true;
        if(!driver){armed_=false;status.text="Driver unavailable";return false;}
        if(!driver->aim_hwbp_remove()){status.text="Breakpoint cleanup failed; retrying";return false;}
        armed_=false;templates_written_=false;count_=0;return true;
    }
    void publish(const Status& status){std::lock_guard<std::mutex> lock(mutex_);status_=status;published_snapshots_=snapshots_;}
    void pause(Status& status,const char* reason){if(disarm(status))status.text=reason;}
    static double seconds(std::chrono::steady_clock::time_point at){return std::chrono::duration<double>(at.time_since_epoch()).count();}
    // headWorld is an extra out-param: the SAME selected enemy's actual
    // head-bone world position, independent of settings.bone (which only
    // decides the X/aimbot aim point and FOV/lock radius). Bullet Track
    // (HWBPY) always redirects onto the real head bone -- see step() --
    // regardless of what body part the operator picked for X.
    uint64_t select(const Settings& settings,Vector3& world,Vector3& headWorld,data::TouchFrame& frame,Status& status) {
        std::lock_guard<std::mutex> lock(data::players_mutex);
        frame=data::touchFrame;
        const double now=seconds(std::chrono::steady_clock::now());
        if(!fresh(now,seconds(frame.sampled))){status.text="Waiting for fresh player data";return 0;}
        uint64_t best=0;float best_radius=settings.fov,locked_radius=0;
        Vector3 locked_world{},locked_head{};bool eligible=false;
        for(const auto& player:data::players){
            if(!player.addr||player.curHP<=0||!player.isVisible||player.isKnocked||player.distance>settings.range)continue;
            const auto point=player.getTargetWorld(settings.bone);
            if(!finite(point)||point==Vector3::Zero())continue;
            const auto screen=WorldToScreenPoint(frame.view,point);
            if(!finite(screen)||screen.x<1||screen.y<1||screen.x>=g_sw||screen.y>=g_sh)continue;
            const float radius=std::hypot(screen.x-g_sw*0.5f,screen.y-g_sh*0.5f);
            if(radius>settings.fov)continue;
            if(player.addr==locked_){eligible=true;locked_world=point;locked_radius=radius;locked_head=player.headWorld;}
            if(radius<best_radius){best=player.addr;best_radius=radius;world=point;headWorld=player.headWorld;}
        }
        if(eligible&&retain_target(locked_radius,best_radius)){world=locked_world;headWorld=locked_head;return locked_;}
        if(!best)status.text="Waiting for target in FOV";
        return best;
    }
    // Aim records contain the local firing-ray origin. Never substitute a
    // third-person camera/last foreign hit origin or a hard-coded shoulder bias.
    bool local_origin(uint64_t local,Vector3& origin,uint64_t& info,uint64_t& adjusted) {
        info=adjusted=0;
        read_pointer(local+0xE58,info);read_pointer(local+0xE60,adjusted);
        return info&&driver->read(info+0x4C,&origin,sizeof(origin))&&finite(origin)&&origin!=Vector3::Zero();
    }
    bool clear_point(unsigned point,paradise_bp_record record) {
        if(driver->selected_backend()!=3)return driver->aim_hwbp_write(point,nullptr,nullptr,0);
        for(int reg=0;reg<PARADISE_REG_COUNT;++reg)PARADISE_BP_SET_MASK(&record,reg,PARADISE_BP_OP_READ);
        return driver->hwbp_set_record(point,0,record);
    }
    bool ensure_capture(const Settings& settings,int pid,uint64_t base,Status& status){
        if(armed_&&(pid!=pid_||base!=base_||!same_plan(settings,applied_))&&!disarm(status))return false;
        if(armed_)return true;
        points_=plan(settings,count_);uintptr_t addresses[2]{};
        for(unsigned i=0;i<count_;++i)addresses[i]=base+points_[i].rva;
        armed_=true;templates_written_=true; // a failed/reused-key install can be partially acknowledged
        if(!driver->aim_hwbp_install(addresses,count_)){
            const int error=errno;
            const auto reason=bp_capture::State::format("Breakpoint installation failed: errno=%d (%s)",error,strerror(error));
            pause(status,reason.c_str());return false;
        }
        pid_=pid;base_=base;applied_=settings;templates_written_=false;
        snapshots_={};snapshot_time_={};status.x_hits=status.y_hits=0;
        if(driver->selected_backend()==3)driver->hwbp_clear_records();
        return true;
    }
    void capture_without_writes(const Settings& settings,int pid,uint64_t base,Status& status,const char* reason="Fire Check: capturing hits, register edits disabled"){
        if(!ensure_capture(settings,pid,base,status))return;
        if(templates_written_){
            for(unsigned i=0;i<count_;++i)if(!driver->aim_hwbp_write(i,nullptr,nullptr,0)){pause(status,"Fire Check: failed to clear register edits");return;}
            templates_written_=false;
        }
        status.text=reason;
        for(unsigned i=0;i<count_;++i){
            paradise_bp_record record{};bool available=false;uint64_t hits=0;
            if(!driver->aim_hwbp_poll(i,record,available,hits)){pause(status,"Capture-only record query failed");return;}
            if(points_[i].quaternion){status.x_hits=hits;status.x="Capture only";}else{status.y_hits=hits;status.y="Capture only";}
            if(available&&seconds(std::chrono::steady_clock::now())-snapshot_time_[i]>=0.25){
                if(driver->breakpoint_has_simd_snapshots())snapshots_[i]=bp_capture::State::snapshot(record,0,false);
                else {
                    snapshots_[i]=bp_capture::State::format("NB9 capture-only retained=%llu PC=%llx LR/X30=%llx SP=%llx PSTATE=%llx; SIMD snapshots unavailable\n",(unsigned long long)hits,(unsigned long long)record.pc,(unsigned long long)record.lr,(unsigned long long)record.sp,(unsigned long long)record.pstate);
                    for(unsigned r=0;r<30;++r){uint64_t value;std::memcpy(&value,reinterpret_cast<const char*>(&record.x0)+r*8,8);snapshots_[i]+=bp_capture::State::format("X%u=%llx\n",r,(unsigned long long)value);}
                }
                snapshot_time_[i]=seconds(std::chrono::steady_clock::now());
            }
        }
    }
    void target_wait(const Settings& settings,int pid,uint64_t base,Status& status,const char* reason){
        if(driver&&armed_)capture_without_writes(settings,pid,base,status,reason);
        else pause(status,reason);
    }
    void step(const Settings& settings,Status& status) {
        status.fire_address=0;status.fire_read_ok=false;status.fire_value=0;
        status.x=settings.x?"Waiting":"OFF";status.y=settings.y?"Waiting":"OFF";
        if(!settings.x&&!settings.y){disarm(status);status.text=armed_?status.text:"OFF";status.x=status.y="OFF";locked_=0;motion_.reset();return;}
        if(!driver||!driver->supports_aim_breakpoints()){
            pause(status,"HWBP X/Y requires compatible Paradise, TWT or NB9");return;
        }
        const int pid=proc::pid;const uint64_t base=proc::lib,local=data::localPlayerAddr.load();
        status.pid=pid;status.base=base;status.local=local;
        if(tracking_bone_!=settings.bone||tracking_local_!=local){motion_.reset();locked_=0;tracking_bone_=settings.bone;tracking_local_=local;}
        if(pid<=0||!base||!data::inMatch.load()||!bp_capture::valid_pointer(local)||cfg::aim::visible_enabled){
            pause(status,"Waiting for local player (legacy HWBP must be off)");locked_=0;motion_.reset();return;
        }
        // Bullet Track forces its own weapon recoil to zero every poll
        // while Y is on, regardless of the Misc tab's separate No Recoil
        // toggle -- see force_zero_weapon_recoil() above for why.
        if(settings.y) force_zero_weapon_recoil(local);
        // Fire Check applies to Aimbot's (X) rotation write only now, and
        // only gates that one point's write further down in the per-point
        // loop. It must NOT gate target selection or Bullet Track (Y) at
        // all: Y needs its register kept continuously fresh with the
        // correct head direction so that whenever the game's own raycast
        // actually executes -- whether that's a real fired shot or the
        // engine's own continuous "is the reticle on target" check -- it
        // reads a current value, not one that was just cleared because
        // the firing byte happened to read false a moment earlier. Gating
        // the whole function here (old behaviour) was clearing Y's write
        // between shots and was the real cause of both the reticle only
        // turning red sometimes and shots landing on the body instead of
        // the head.
        Vector3 origin{};uint64_t info=0,adjusted=0;
        if(!local_origin(local,origin,info,adjusted)){target_wait(settings,pid,base,status,"Waiting for local ray origin");return;}
        Vector3 world{},headWorld{};data::TouchFrame frame;
        const auto enemy=select(settings,world,headWorld,frame,status);
        if(!enemy){const auto reason=status.text;target_wait(settings,pid,base,status,reason.c_str());locked_=0;motion_.reset();return;}
        if(enemy!=locked_){motion_.reset();locked_=enemy;}
        const double now=seconds(std::chrono::steady_clock::now()),sample=seconds(frame.sampled);
        if(!fresh(now,sample)){target_wait(settings,pid,base,status,"Target sample became stale");return;}
        const auto velocity=motion_.observe({world.x,world.y,world.z},frame.sequence,sample);
        const auto lead=predicted({world.x,world.y,world.z},velocity,now-sample,settings.lead_ms);
        const Vector3 target{lead[0],lead[1],lead[2]};
        if(!finite(target)){target_wait(settings,pid,base,status,"Invalid target prediction");return;}
        std::array<float,3> dir{};
        if(!bp_capture::direction_between({origin.x,origin.y,origin.z},lead,dir)){
            target_wait(settings,pid,base,status,"Invalid ray direction");return;
        }

        // Bullet Track (HWBPY) direction: this register write lands inside
        // the game's raycast itself at the exact instant it executes --
        // there is no bullet travel time to lead for, and no safe way to
        // extrapolate it further either. Any velocity-based prediction,
        // even just an "age" correction, multiplies a noisy estimate
        // (jump-shooting, strafing, direction changes) by a time delta and
        // adds that error on top of the real sample -- against a head-
        // sized target, that is exactly the kind of small, inconsistent
        // overshoot that reads as "redirects, but lands on the body, not
        // the head." Y is snapped straight onto the real, currently-
        // sampled head-bone world position every single poll (no lead, no
        // velocity term at all) -- the data thread already resamples this
        // every ~2ms, so the value is never more than one poll old, and
        // it is never shifted away from where the head actually is. Y
        // always targets the real head bone (not settings.bone, which
        // only affects X). If this frame's explicit head read failed
        // (best-effort, same as the skeleton bones), use the already-
        // selected bone point rather than skip the write outright -- still
        // the same single breakpoint-based path, no alternate aiming
        // method.
        const bool haveHead = finite(headWorld) && headWorld != Vector3::Zero();
        const Vector3 target_y = haveHead ? headWorld : world;
        if(!finite(target_y)||target_y==Vector3::Zero()){target_wait(settings,pid,base,status,"Invalid bullet-track target");return;}
        std::array<float,3> dir_y{};
        if(!bp_capture::direction_between({origin.x,origin.y,origin.z},{target_y.x,target_y.y,target_y.z},dir_y)){
            target_wait(settings,pid,base,status,"Invalid bullet-track ray direction");return;
        }

        // Camera origin anchor: in third-person view, using the true camera origin
        // avoids the feedback loop where bone/weapon rotation shifts the ray origin.
        Vector3 quat_origin = origin;
        Vector3 camPos = GetCameraPositionFromMatrix(frame.view);
        if (finite(camPos) && camPos != Vector3::Zero()) {
            quat_origin = camPos;
        }
        std::array<float,3> quat_dir = dir;
        bp_capture::direction_between({quat_origin.x,quat_origin.y,quat_origin.z},lead,quat_dir);

        auto quat_raw=Quaternion::Normalize(Quaternion::LookRotation(Vector3(quat_dir[0],quat_dir[1],quat_dir[2])));
        if(!std::isfinite(quat_raw.x)||!std::isfinite(quat_raw.y)||!std::isfinite(quat_raw.z)||!std::isfinite(quat_raw.w)){
            target_wait(settings,pid,base,status,"Invalid aim rotation");return;
        }

        // 1. Hemisphere alignment with current player rotation to prevent 360-degree reverse flips
        Quaternion curQ{};
        if (local && driver && driver->read(local + Offsets::AimRotation, &curQ, sizeof(curQ))) {
            float dot = curQ.x * quat_raw.x + curQ.y * quat_raw.y + curQ.z * quat_raw.z + curQ.w * quat_raw.w;
            if (dot < 0.0f) {
                quat_raw.x = -quat_raw.x;
                quat_raw.y = -quat_raw.y;
                quat_raw.z = -quat_raw.z;
                quat_raw.w = -quat_raw.w;
            }
        }

        // 2. High-speed smooth filter: absorbs bone jitter, recoil bounce, and eliminates screen shake
        static Quaternion s_smooth_quat{};
        static uint64_t s_smooth_target = 0;
        static bool s_smooth_init = false;

        if (!s_smooth_init || enemy != s_smooth_target) {
            s_smooth_quat = quat_raw;
            s_smooth_target = enemy;
            s_smooth_init = true;
        } else {
            float sdot = s_smooth_quat.x * quat_raw.x + s_smooth_quat.y * quat_raw.y + s_smooth_quat.z * quat_raw.z + s_smooth_quat.w * quat_raw.w;
            if (sdot < 0.0f) {
                quat_raw.x = -quat_raw.x;
                quat_raw.y = -quat_raw.y;
                quat_raw.z = -quat_raw.z;
                quat_raw.w = -quat_raw.w;
            }
            s_smooth_quat = Quaternion::Normalize(Quaternion(
                s_smooth_quat.x + (quat_raw.x - s_smooth_quat.x) * 0.40f,
                s_smooth_quat.y + (quat_raw.y - s_smooth_quat.y) * 0.40f,
                s_smooth_quat.z + (quat_raw.z - s_smooth_quat.z) * 0.40f,
                s_smooth_quat.w + (quat_raw.w - s_smooth_quat.w) * 0.40f
            ));
        }
        const auto quaternion = s_smooth_quat;

        if(!ensure_capture(settings,pid,base,status))return;
        status.target=enemy;status.origin=origin;status.target_point=target;status.velocity={velocity[0],velocity[1],velocity[2]};
        status.text="Active";status.x=settings.x?"Waiting for hit":"OFF";status.y=settings.y?"Waiting for hit":"OFF";

        for(unsigned point=0;point<count_;++point){
            paradise_bp_record record{};bool available=false;uint64_t hits=0;
            auto& text=points_[point].quaternion?status.x:status.y;
            int guard=-1;uint64_t first=0,second=0;
            if(!points_[point].quaternion){
                if(points_[point].rva==0x7C5405C){guard=1;first=local+0xE58;second=local+0xE60;}
                else {guard=8;first=info;second=adjusted;}
            } else {
                guard=0;first=local;second=0;
            }
            if(!driver->aim_hwbp_poll(point,record,available,hits,guard,first,second)){pause(status,"Record read failed; writes removed");return;}
            if(points_[point].quaternion)status.x_hits=hits;else status.y_hits=hits;
            if(!available){
                if(driver->selected_backend()==6){text="Waiting for NB9 hit records";status.text="NB9 capture required before guarded register edits";}
                continue;
            }
            if(!record_matches(points_[point],base,record.pc,local,info,adjusted,record.x0,record.x1,record.x8)){
                if(!clear_point(point,record)){pause(status,"Context cleanup failed");return;}
                text=points_[point].quaternion?"Waiting for selected PC":"Waiting for local context";continue;
            }
            // Keep report formatting out of the high-rate register-update path.
            const double observed_at=seconds(std::chrono::steady_clock::now());
            if(observed_at-snapshot_time_[point]>=0.25){
                if(driver->breakpoint_has_simd_snapshots()){
                    snapshots_[point]=bp_capture::State::snapshot(record,0,driver->selected_backend()==3,true);
                    if(driver->selected_backend()==4)snapshots_[point]+="TWT does not supply FPSR/FPCR or Paradise register masks.\n";
                }
                else {
                    snapshots_[point]=bp_capture::State::format("NB9 retained records=%llu PC=%llx LR=%llx SP=%llx PSTATE=%llx\nSIMD snapshots and lifetime hit counts are unavailable in this ABI.\n",
                        (unsigned long long)hits,(unsigned long long)record.pc,(unsigned long long)record.lr,(unsigned long long)record.sp,(unsigned long long)record.pstate);
                    for(unsigned r=0;r<30;++r){uint64_t value;std::memcpy(&value,reinterpret_cast<const char*>(&record.x0)+r*8,8);snapshots_[point]+=bp_capture::State::format("X%u=%llx\n",r,(unsigned long long)value);}
                }
                snapshot_time_[point]=observed_at;
            }
            const auto spec=bp_capture::rewrite_spec(points_[point].rva);
            // Y (bullet track) writes dir_y -- the zero-extra-lead,
            // real-head-bone direction computed above -- not dir (which
            // still carries X's full lead/prediction, correct for the
            // visual aim rotation but the actual source of Y's misses).
            const std::array<float,4> values=points_[point].quaternion ?
                std::array<float,4>{quaternion.x,quaternion.y,quaternion.z,quaternion.w} :
                std::array<float,4>{dir_y[0],dir_y[1],dir_y[2],0};
            for(int reg=0;reg<PARADISE_REG_COUNT;++reg)PARADISE_BP_SET_MASK(&record,reg,PARADISE_BP_OP_READ);
            for(unsigned i=0;i<(points_[point].quaternion?4u:3u);++i){
                __uint128_t value=0;std::memcpy(&value,&values[i],4);
                std::memcpy(reinterpret_cast<char*>(&record)+offsetof(paradise_bp_record,q0)+spec.s[i]*16,&value,16);
                PARADISE_BP_SET_MASK(&record,PARADISE_REG_Q0+spec.s[i],PARADISE_BP_OP_WRITE);
            }
            // A release during target sampling must not enable a new template.
            if(!fresh(seconds(std::chrono::steady_clock::now()),sample)){
                target_wait(settings,pid,base,status,"Target sample became stale; writes removed");return;
            }
            // Fire Check gates Aimbot's (X) rotation write only, and is
            // always on (no menu toggle, no off state) -- see Settings.
            // Bullet Track (Y) is never gated on the firing byte: skipping
            // just this one point's write, instead of returning out of the
            // whole loop, is what lets Y's point below still run on every
            // single poll regardless of X's firing state.
            if(points_[point].quaternion){
                uint8_t fire=0;status.fire_read_ok=driver->read(local+Offsets::isFiring,&fire,1);status.fire_value=fire;
                if(!status.fire_read_ok||!fire){
                    // Not firing: remove just X's own register write (same
                    // per-point clear the old whole-function gate used) so
                    // the visual rotation doesn't keep replaying while idle
                    // -- Y's point below is untouched and keeps writing.
                    if(!clear_point(point,record)){pause(status,"Context cleanup failed");return;}
                    text="Waiting for firing";continue;
                }
            }
            templates_written_=true; // preserve the need to clear even if only some native writes succeed
            bool wrote=false;
            if(driver->selected_backend()==3)wrote=driver->hwbp_set_record(point,0,record);
            else {int regs[4]{};for(unsigned i=0;i<4;++i)regs[i]=spec.s[i];
                wrote=driver->aim_hwbp_write(point,regs,values.data(),points_[point].quaternion?4:3);
            }
            if(!wrote){pause(status,"Register write failed; templates removed");return;}
            if(points_[point].quaternion && Offsets::CurrentAimRotation && local && driver){
                driver->write(local + Offsets::CurrentAimRotation, &quaternion, sizeof(quaternion));
            }
            templates_written_=true;
            ++status.updates;text="Tracking";
        }
    }
    void loop(){
        Status status;
        while(running_.load()){
            const auto begin=std::chrono::steady_clock::now();
            Settings settings;{std::lock_guard<std::mutex> lock(mutex_);settings=settings_;}
            status.configuration=settings;
            status.target=0;
            try{step(settings,status);}catch(const std::exception& e){pause(status,e.what());}
            publish(status);
            // Tightened from 8333us (120Hz) to 2000us (500Hz): every poll
            // re-writes the breakpoint's register-write mask with the
            // current direction/quaternion, and the hardware breakpoint
            // replays whatever is currently armed on EVERY hit at that PC
            // (not just once) -- so a narrower poll period shrinks the
            // window in which a stale (not-yet-refreshed) write could be
            // in effect when a shot's raycast executes, for weapons that
            // fire several rounds close together. Still fully breakpoint-
            // based; no fallback path of any kind was added.
            std::this_thread::sleep_until(begin+std::chrono::microseconds(2000));
        }
        disarm(status);
    }
public:
    Controller(){worker_=std::thread([this]{loop();});}
    ~Controller(){running_.store(false);if(worker_.joinable())worker_.join();}
    Settings settings(){std::lock_guard<std::mutex> lock(mutex_);return settings_;}
    Status status(){std::lock_guard<std::mutex> lock(mutex_);return status_;}
    void configure(Settings settings){
        std::lock_guard<std::mutex> lock(mutex_);settings_=settings;
    }
    std::string save(){
        Settings settings;Status status;std::array<std::string,2> snapshots;
        {std::lock_guard<std::mutex> lock(mutex_);status=status_;settings=status.configuration;snapshots=published_snapshots_;}
        std::string text="HWBP X/Y report\n";
        if(driver)text+=std::string("Backend: ")+driver_selection::name(driver->selected_backend())+"\n";
        if(driver)text+=driver->connection_diagnostics();
        if(driver)text+=driver->breakpoint_diagnostics();
        if(driver&&!driver->breakpoint_has_total_hits())text+="NB9: X_hits/Y_hits show retained records, not lifetime totals. S/Q snapshots are unavailable.\n";
        text+=bp_capture::State::format("X=%s option=%c | Y=%s option=%c | Fire Check=%s\nFOV=%.1f range=%.1f lead_ms=%.1f\n",
            settings.x?"ON":"OFF",settings.x_option?'B':'A',settings.y?"ON":"OFF",settings.y_option?'B':'A',
            settings.fire_check?"ON":"OFF",settings.fov,settings.range,settings.lead_ms);
        text+=status.text+"\nX: "+status.x+"\nY: "+status.y+"\n";
        if(settings.fire_check)text+=bp_capture::State::format("Fire sample: address=0x%llx read_ok=%d raw_byte=%u (client sampled)\n",(unsigned long long)status.fire_address,status.fire_read_ok,unsigned(status.fire_value));
        text+=bp_capture::State::format("PID=%d IL2CPP=0x%llx local=0x%llx\n",status.pid,(unsigned long long)status.base,(unsigned long long)status.local);
        text+=bp_capture::State::format("target=%llx X_hits=%llu Y_hits=%llu acknowledged_updates=%llu\norigin=(%.9g,%.9g,%.9g) target=(%.9g,%.9g,%.9g) velocity=(%.9g,%.9g,%.9g)\n",
            (unsigned long long)status.target,(unsigned long long)status.x_hits,(unsigned long long)status.y_hits,(unsigned long long)status.updates,
            status.origin.x,status.origin.y,status.origin.z,status.target_point.x,status.target_point.y,status.target_point.z,status.velocity.x,status.velocity.y,status.velocity.z);
        text+="Writes are sampled templates; neither firing nor object identity is checked atomically by the native handler. Register values may reflect configured writes.\n";
        text+="HWBP X uses shared setter templates: the latest aggregated player pointer does not gate writes. X can affect other players invoking the same setter. HWBP Y retains its local ray-context checks.\n";
        for(const auto& snapshot:snapshots)text+=snapshot+"\n";
        exporter_.save_report_text(text);return exporter_.export_status;
    }
};
// Construct after c_driver in main so this worker stops before driver destruction.
inline Controller& controller(){static Controller instance;return instance;}
}
