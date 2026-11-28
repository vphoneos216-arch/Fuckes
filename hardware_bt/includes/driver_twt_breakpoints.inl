// TWT's device probe demonstrates per-TID coverage. Own every installed handle.
private:
    struct TwtThreadPoint {
        pid_t tid=0;uint64_t start=0,handle=0,hits=0,retained=0;
        bool snapshot=false,edited=false,recycle=false;
        twt_bp::Hit latest{};
        std::chrono::steady_clock::time_point probed{};
    };
    std::array<std::vector<TwtThreadPoint>,2> twt_points_;
    std::array<uint64_t,2> twt_retired_hits_{},twt_chosen_handle_{};
    std::chrono::steady_clock::time_point twt_thread_scan_{},twt_rotate_after_{};
    std::vector<int> twt_sampled_tids_;
    size_t twt_scan_cursor_=0;
    uint64_t twt_capacity_rejections_=0,twt_rollovers_=0;
    bool twt_capacity_limited_=false;
    struct TwtLearned {pid_t owner=0,tid=0;uintptr_t address=0;uint64_t start=0;};
    std::vector<TwtLearned> twt_learned_;
    std::vector<twt_bp::Hit> twt_snapshot_scratch_;
    pid_t twt_last_install_tid_=0;
    void twt_remember_locked(unsigned index,const TwtThreadPoint& point){
        const auto address=native_points_[index].address;
        auto found=std::find_if(twt_learned_.begin(),twt_learned_.end(),[&](const auto& p){return p.owner==pid&&p.address==address;});
        const TwtLearned value{pid,point.tid,address,point.start};
        if(found!=twt_learned_.end())*found=value;
        else {if(twt_learned_.size()==16)twt_learned_.erase(twt_learned_.begin());twt_learned_.push_back(value);}
    }
    bool twt_remove_point_locked(unsigned index){
        auto& points=twt_points_[index];bool ok=true;
        for(auto it=points.begin();it!=points.end();){
            errno=0;const int rc=ioctl(twt_fd,twt_bp::remove,&it->handle);
            if(!rc||errno==ENOENT){twt_retired_hits_[index]+=it->hits;it=points.erase(it);}
            else {native_bp_fail("TWT per-thread cleanup",errno);ok=false;++it;}
        }
        if(points.empty())twt_chosen_handle_[index]=0;
        return ok;
    }
    bool twt_refresh_locked(){
        const auto now=std::chrono::steady_clock::now();
        if(now<twt_thread_scan_)return true;
        twt_thread_scan_=now+std::chrono::milliseconds(500);
        const auto tids=native_thread_ids();
        if(tids.empty())return native_bp_fail("TWT target thread enumeration",ESRCH);
        if(tids.size()>1024)return native_bp_fail("TWT thread-enumeration bound (1024)",E2BIG);
        unsigned requested=0;for(const auto& point:native_points_)if(point.active)++requested;
        const size_t budget=twt_policy::thread_budget(native_bp_slots_,requested);
        if(!budget)return native_bp_fail("TWT capture handle budget",ENOSPC);
        std::vector<twt_policy::Candidate> candidates;candidates.reserve(tids.size());
        for(pid_t tid:tids)if(native_thread_start(tid))candidates.push_back({tid,native_thread_rank(tid)});
        std::vector<int> pinned;bool has_known[2]{};
        for(const auto& known:twt_learned_)if(known.owner==pid&&native_thread_start(known.tid)==known.start){
            bool selected=false;for(unsigned i=0;i<2;++i)if(native_points_[i].active&&native_points_[i].address==known.address){selected=true;has_known[i]=true;}
            if(selected)pinned.push_back(known.tid);
        }
        // Before a context-validated write has identified a caller, retain at
        // most one observed caller per point, leaving spare discovery capacity.
        for(unsigned i=0;i<2;++i){
            if(has_known[i])continue;
            auto found=std::find_if(twt_points_[i].begin(),twt_points_[i].end(),[&](const auto& p){return p.handle==twt_chosen_handle_[i]&&p.snapshot;});
            if(found!=twt_points_[i].end())pinned.push_back(found->tid);
        }
        const bool rotate=now>=twt_rotate_after_;
        auto desired=twt_policy::select(candidates,pinned,twt_sampled_tids_,budget,twt_scan_cursor_,rotate);
        if(desired.empty())return native_bp_fail("TWT no live discovery candidates",ESRCH);
        if(rotate)twt_rotate_after_=now+std::chrono::milliseconds(500);
        for(auto& entries:twt_points_)entries.reserve(budget);
        twt_learned_.reserve(16);
        // Free only stale/unproductive leases. Proven callers remain armed.
        for(unsigned i=0;i<2;++i){
            auto& entries=twt_points_[i];
            for(auto it=entries.begin();it!=entries.end();){
                const bool keep=native_points_[i].active&&!it->recycle&&
                    std::find(desired.begin(),desired.end(),it->tid)!=desired.end()&&native_thread_start(it->tid)==it->start;
                if(keep){++it;continue;}
                errno=0;const int rc=ioctl(twt_fd,twt_bp::remove,&it->handle);
                if(rc&&errno!=ENOENT)return native_bp_fail("TWT lease cleanup",errno);
                twt_retired_hits_[i]+=it->hits;if(it->recycle)++twt_rollovers_;it=entries.erase(it);
            }
        }
        twt_sampled_tids_=desired;twt_capacity_limited_=false;bool stop=false;
        // Allocate both selected points for one candidate before another TID.
        for(pid_t tid:desired){
            for(unsigned i=0;i<2;++i){
                if(!native_points_[i].active)continue;
                auto& entries=twt_points_[i];
                if(std::any_of(entries.begin(),entries.end(),[&](const auto& p){return p.tid==tid;}))continue;
                const auto start=native_thread_start(tid);if(!start)continue;
                twt_bp::Install request{};request.pid=tid;request.address=native_points_[i].address;
                request.length=4;request.type=4;request.flags=1;twt_last_install_tid_=tid;
                errno=0;const int rc=ioctl(twt_fd,twt_bp::install,&request);
                if(rc&&errno==ESRCH)continue;
                if(rc&&errno==ENOSPC){
                    ++twt_capacity_rejections_;twt_capacity_limited_=true;
                    twt_thread_scan_=now+std::chrono::seconds(2);twt_rotate_after_=twt_thread_scan_;
                    native_bp_stage_="TWT capacity limited; retained working handles, discovery backs off";
                    native_bp_error_=ENOSPC;stop=true;break;
                }
                if(rc)return native_bp_fail("TWT per-thread installation",errno);
                if(!request.address)return native_bp_fail("TWT installation returned zero handle",EPROTO);
                entries.push_back({tid,start,request.address});
                if(native_thread_start(tid)!=start){
                    errno=0;const int removed=ioctl(twt_fd,twt_bp::remove,&entries.back().handle);
                    if(removed&&errno!=ENOENT)return native_bp_fail("TWT changed-thread rollback",errno);
                    entries.pop_back();
                }
            }
            if(stop)break;
        }
        for(unsigned i=0;i<2;++i)native_points_[i].handle=twt_points_[i].empty()?0:twt_points_[i].front().handle;
        if(!twt_capacity_limited_){native_bp_stage_="TWT bounded capture; learned callers retained";native_bp_error_=0;}
        return true;
    }
    bool twt_poll_locked(unsigned index,paradise_bp_record& record,bool& available,uint64_t& hits,int guard=-1,uint64_t first=0,uint64_t second=0){
        if(guard< -1||guard>30)return native_bp_fail("TWT invalid context register",EINVAL);
        if(!twt_refresh_locked())return false;
        const auto now=std::chrono::steady_clock::now();
        hits=twt_retired_hits_[index];TwtThreadPoint* newest=nullptr;
        auto& summary=native_points_[index];summary.retained=summary.copied=0;
        for(auto& point:twt_points_[index]){
            if(point.hits || now-point.probed>=std::chrono::milliseconds(250)){
                twt_bp::Counts counts{point.handle,0,0};errno=0;
                if(ioctl(twt_fd,twt_bp::counts,&counts)!=0)return native_bp_fail("TWT per-thread hit-count query",errno);
                point.probed=now;
                const bool changed=point.hits!=counts.total;
                point.hits=counts.total;point.retained=counts.retained;
                if(counts.retained&&(changed||!point.snapshot)){
                    // Fetch the retained history, not a permanently stale first 16 records.
                    if(counts.retained>256){
                        if(!twt_write_locked(index,nullptr,nullptr,0))return false;
                        point.snapshot=false;point.recycle=true;twt_thread_scan_={};
                        hits+=point.hits;summary.retained+=point.retained;continue;
                    }
                    twt_snapshot_scratch_.resize(static_cast<size_t>(counts.retained));
                    twt_bp::Items request{point.handle,reinterpret_cast<uint64_t>(twt_snapshot_scratch_.data()),twt_snapshot_scratch_.size()*sizeof(twt_bp::Hit),0};
                    errno=0;if(ioctl(twt_fd,twt_bp::items,&request)!=0)return native_bp_fail("TWT per-thread snapshot query",errno);
                    if(request.copied>twt_snapshot_scratch_.size())return native_bp_fail("TWT snapshot reply exceeds buffer",EPROTO);
                    summary.copied+=request.copied;
                    for(size_t i=0;i<request.copied;++i){const auto& hit=twt_snapshot_scratch_[i];
                        if(hit.tid!=static_cast<uint64_t>(point.tid)||hit.regs.pc!=summary.address)continue;
                        if(!point.snapshot||hit.time>=point.latest.time){point.latest=hit;point.snapshot=true;}
                    }
                }
            }
            hits+=point.hits;summary.retained+=point.retained;
            const bool context=guard<0 || (first&&point.latest.regs.x[guard]==first) || (second&&point.latest.regs.x[guard]==second);
            if(point.snapshot&&context&&(!newest||point.latest.time>newest->latest.time))newest=&point;
        }
        summary.total=hits;twt_chosen_handle_[index]=newest?newest->handle:0;
        if(!newest)return twt_write_locked(index,nullptr,nullptr,0);
        const auto& hit=newest->latest;
        std::memcpy(&record.x0,hit.regs.x,30*sizeof(uint64_t));record.lr=hit.regs.x[30];
        record.sp=hit.regs.sp;record.pc=hit.regs.pc;record.pstate=hit.regs.pstate;
        record.orig_x0=hit.regs.orig_x0;record.syscallno=static_cast<int32_t>(hit.regs.syscallno);
        std::memcpy(&record.q0,hit.regs.v,sizeof(hit.regs.v));record.hit_count=hits;available=true;return true;
    }
    bool twt_write_locked(unsigned index,const int* regs,const float* values,unsigned count){
        alignas(16) twt_bp::Registers registers{};uint64_t mask=0;
        for(unsigned i=0;i<count;++i){mask|=1ULL<<regs[i];std::memcpy(&registers.v[regs[i]],&values[i],sizeof(float));}
        for(auto& point:twt_points_[index]){
            // The controller validated the selected snapshot's context. Do not
            // copy its template to unrelated threads with different snapshots.
            const bool apply=count&&point.handle==twt_chosen_handle_[index];
            if(!apply&&!point.edited)continue;
            twt_bp::Modify request{point.handle,0,apply?mask:0,reinterpret_cast<uint64_t>(&registers)};
            if(apply)point.edited=true; // an error may follow a partially applied native update
            errno=0;if(ioctl(twt_fd,twt_bp::modify,&request)!=0)return native_bp_fail("TWT per-thread FP-template modification",errno);
            point.edited=apply;
            if(apply)twt_remember_locked(index,point);
        }
        return true;
    }
