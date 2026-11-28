// Included inside c_driver. Only normal hardware breakpoints are used here.
private:
    struct NativePoint { uintptr_t address=0; uint64_t handle=0; bool active=false; uint64_t total=0,retained=0,copied=0; };
    std::array<NativePoint,2> native_points_{};
    std::string native_bp_stage_="Not requested";
    int native_bp_error_=0,native_bp_slots_=-1;
    bool native_bp_fail(const char* stage,int error){native_bp_stage_=stage;native_bp_error_=error?error:EIO;errno=native_bp_error_;return false;}
    // NB9 exposes disable, not keyed unregister. Retain disabled keys for reuse;
    // never invoke its global clear automatically (it can affect other clients).
    std::vector<std::pair<pid_t,uintptr_t>> nb9_owned_keys_;
    std::vector<std::pair<pid_t,uintptr_t>> nb9_written_keys_;
    uint64_t nb9_record_queries_=0,nb9_empty_queries_=0;
    bool native_remove_locked() {
        bool ok=true,had_points=false;
        for(unsigned index=0;index<native_points_.size();++index){
            auto& point=native_points_[index];
            if(!point.active)continue;
            had_points=true;
            bool removed=false;
            if(selected_driver_==4 && twt_fd>=0)
                removed=twt_remove_point_locked(index);
            if(selected_driver_==6 && nb9_driver)
                removed=nb9_driver->DisableHwBp(pid,point.address);
            if(removed)point={};else {native_bp_fail("Native HWBP cleanup",errno);ok=false;}
        }
        if(had_points&&ok&&!native_bp_error_)native_bp_stage_="Owned breakpoints removed";
        return ok;
    }
public:
    std::string breakpoint_diagnostics(){
        std::lock_guard<std::mutex> lock(paradise_lock);char text[1024];
        std::snprintf(text,sizeof(text),"Native HWBP stage=%s errno=%d (%s) slots=%d\nPoint0: address=0x%llx handle=0x%llx active=%d total=%llu retained=%llu copied=%llu\nPoint1: address=0x%llx handle=0x%llx active=%d total=%llu retained=%llu copied=%llu\n",
            native_bp_stage_.c_str(),native_bp_error_,strerror(native_bp_error_),native_bp_slots_,
            (unsigned long long)native_points_[0].address,(unsigned long long)native_points_[0].handle,native_points_[0].active&&(selected_driver_!=4||!twt_points_[0].empty()),(unsigned long long)native_points_[0].total,(unsigned long long)native_points_[0].retained,(unsigned long long)native_points_[0].copied,
            (unsigned long long)native_points_[1].address,(unsigned long long)native_points_[1].handle,native_points_[1].active&&(selected_driver_!=4||!twt_points_[1].empty()),(unsigned long long)native_points_[1].total,(unsigned long long)native_points_[1].retained,(unsigned long long)native_points_[1].copied);
        std::string result=text;
        if(selected_driver_==6)result+="NB9 record queries="+std::to_string(nb9_record_queries_)+" empty="+std::to_string(nb9_empty_queries_)+" sticky template keys="+std::to_string(nb9_written_keys_.size())+"\n";
        if(selected_driver_==4){
            result+="TWT per-TID ownership: point0="+std::to_string(twt_points_[0].size())+" point1="+std::to_string(twt_points_[1].size())+" last_install_tid="+std::to_string(twt_last_install_tid_)+"\n";
            result+="TWT bounded discovery: sampled_tids="+std::to_string(twt_sampled_tids_.size())+" capacity_rejections="+std::to_string(twt_capacity_rejections_)+" limited="+std::to_string(twt_capacity_limited_)+" history_rollovers="+std::to_string(twt_rollovers_)+"\n";
            for(unsigned i=0;i<2;++i){unsigned shown=0;
                for(const auto& p:twt_points_[i])if((p.hits||p.edited)&&shown++<32){
                    char row[192];std::snprintf(row,sizeof(row),"point=%u tid=%d handle=0x%llx hits=%llu edited=%d pc=0x%llx selected=%d\n",i,p.tid,(unsigned long long)p.handle,(unsigned long long)p.hits,p.edited,(unsigned long long)p.latest.regs.pc,p.handle==twt_chosen_handle_[i]);result+=row;
                }
            }
        }
        return result;
    }
    bool aim_hwbp_remove(){
        return hwbp_remove();
    }
    bool supports_aim_breakpoints() const {
        return selected_driver_==3 || selected_driver_==4 || selected_driver_==6;
    }
    bool breakpoint_has_simd_snapshots() const {return selected_driver_!=6;}
    bool breakpoint_has_total_hits() const {return selected_driver_!=6;}
    bool aim_hwbp_install(const uintptr_t* addresses,unsigned count) {
        if(selected_driver_==3){
            if(!addresses||!count||count>2)return false;
            paradise_bp_point_config configs[2]{};
            for(unsigned i=0;i<count;++i)configs[i]={addresses[i],PARADISE_BP_EXECUTE,4,PARADISE_BP_ALL_THREADS,0};
            return hwbp_set(configs,count);
        }
        std::lock_guard<std::mutex> lock(paradise_lock);
        if(!supports_aim_breakpoints()||pid<=0||!addresses||!count||count>2)return false;
        for(unsigned i=0;i<count;++i){
            if(!addresses[i]||(addresses[i]&3))return false;
            for(unsigned j=0;j<i;++j)if(addresses[i]==addresses[j])return false;
        }
        if(!native_remove_locked())return false;
        if(selected_driver_==4){
            if(twt_fd<0)return false;
            if(ioctl(twt_fd,twt_bp::check,0)!=0 && ioctl(twt_fd,twt_bp::init,0)!=0)return native_bp_fail("TWT BP initialization",errno);
            errno=0;native_bp_slots_=ioctl(twt_fd,twt_bp::num_brps,0);
            if(native_bp_slots_>=0 && static_cast<unsigned>(native_bp_slots_)<count)return native_bp_fail("TWT hardware slot capacity",ENOSPC);
            twt_retired_hits_={};twt_chosen_handle_={};twt_thread_scan_={};
            twt_rotate_after_={};twt_sampled_tids_.clear();twt_scan_cursor_=0;twt_capacity_limited_=false;
            for(unsigned i=0;i<count;++i)native_points_[i]={addresses[i],0,true};
            if(!twt_refresh_locked()){const int error=errno;native_remove_locked();errno=error;return false;}
            return true;
        }
        if(selected_driver_==6)nb9_owned_keys_.reserve(nb9_owned_keys_.size()+count);
        for(unsigned i=0;i<count;++i){
            auto& point=native_points_[i];
            bool ok=false;errno=0;
            if(selected_driver_==6 && nb9_connected && nb9_driver){
                nb9::HW_BP_INFO info{};info.pid=pid;info.addr=addresses[i];info.type=4;info.len=4;
                const auto key=std::make_pair(pid,addresses[i]);
                const bool owned=std::find(nb9_owned_keys_.begin(),nb9_owned_keys_.end(),key)!=nb9_owned_keys_.end();
                if(std::find(nb9_written_keys_.begin(),nb9_written_keys_.end(),key)!=nb9_written_keys_.end()){
                    native_remove_locked();return native_bp_fail("NB9 stored edits cannot be safely reset by the tested module",EOPNOTSUPP);
                }
                ok=owned||nb9_driver->AddHwBp(&info);
                if(ok){
                    if(!owned)nb9_owned_keys_.push_back(key);
                    point.address=addresses[i];point.active=true; // own the key before enabling can fail
                    ok=nb9_driver->UpdateAndEnableHwBp(&info); // enable zero-mask capture before waiting for a hit
                    if(!ok){const int error=errno;native_remove_locked();return native_bp_fail("NB9 explicit capture enable",error);}
                }
            }
            if(!ok){const int error=errno;native_remove_locked();return native_bp_fail("Native HWBP installation",error);}
            point.address=addresses[i];point.active=true;
        }
        native_bp_stage_="NB9 Add+Enable acknowledged; capture not yet observed";native_bp_error_=0;
        return true;
    }
    bool aim_hwbp_poll(unsigned index,paradise_bp_record& record,bool& available,uint64_t& hits,int guard=-1,uint64_t first=0,uint64_t second=0) {
        record={};available=false;hits=0;
        if(selected_driver_==3){uint32_t count=0;
            const bool ok=hwbp_get_records(index,0,&record,1,&count);
            available=count==1;hits=record.hit_count;return ok;
        }
        std::lock_guard<std::mutex> lock(paradise_lock);
        if(index>=native_points_.size()||!native_points_[index].active)return false;
        auto& point=native_points_[index];
        if(selected_driver_==4)return twt_poll_locked(index,record,available,hits,guard,first,second);
        if(selected_driver_==6 && nb9_driver){
            std::array<nb9::HWBP_HIT_ITEM,16> buffer{};
            nb9::HWBP_HIT_ARGS request{};request.pid=pid;request.addr=point.address;
            request.out_buf=buffer.data();request.out_len=buffer.size();
            ++nb9_record_queries_;
            if(!nb9_driver->GetHwBpHits(&request))return native_bp_fail("NB9 hit-record query",errno);
            if(request.real_count<0||request.real_count>int(buffer.size()))return native_bp_fail("NB9 hit-record reply count exceeds buffer",EPROTO);
            // NB9 has retained records, not an exposed lifetime hit counter.
            hits=request.real_count;
            point.retained=point.copied=request.real_count;
            if(!request.real_count){++nb9_empty_queries_;native_bp_stage_="NB9 no hit records; capture-dependent edits held off";return true;}
            native_bp_stage_="NB9 capture observed";
            const auto* latest=&buffer[0];
            for(int i=1;i<request.real_count;++i)if(buffer[i].hit_time>latest->hit_time)latest=&buffer[i];
            std::memcpy(&record.x0,latest->regs_info.regs,30*sizeof(uint64_t));record.lr=latest->regs_info.regs[30];
            record.sp=latest->regs_info.sp;record.pc=latest->regs_info.pc;record.pstate=latest->regs_info.pstate;
            available=true;return true;
        }
        return false;
    }
    bool aim_hwbp_write(unsigned index,const int* regs,const float* values,unsigned count) {
        if(index>=2||count>4||(count&&(!regs||!values)))return false;
        for(unsigned i=0;i<count;++i)if(regs[i]<0||regs[i]>31||!std::isfinite(values[i]))return false;
        if(selected_driver_==3){
            paradise_bp_record record{};uint32_t rec_count=0;
            if(!hwbp_get_records(index,0,&record,1,&rec_count))return false;
            for(int reg=0;reg<PARADISE_REG_COUNT;++reg)PARADISE_BP_SET_MASK(&record,reg,PARADISE_BP_OP_READ);
            if(count && regs && values){
                for(unsigned i=0;i<count;++i){
                    __uint128_t qval=0;std::memcpy(&qval,&values[i],sizeof(float));
                    std::memcpy(reinterpret_cast<char*>(&record)+offsetof(paradise_bp_record,q0)+regs[i]*16,&qval,16);
                    PARADISE_BP_SET_MASK(&record,PARADISE_REG_Q0+regs[i],PARADISE_BP_OP_WRITE);
                }
            }
            return hwbp_set_record(index,0,record);
        }
        std::lock_guard<std::mutex> lock(paradise_lock);
        if(!native_points_[index].active)return false;
        const auto& point=native_points_[index];
        if(selected_driver_==4)return twt_write_locked(index,regs,values,count);
        if(selected_driver_==6 && nb9_driver){
            nb9::HW_BP_INFO info{};info.pid=pid;info.addr=point.address;info.type=4;info.len=4;
            if(!count){
                info.is_write_fp_regs=false;info.fp_reg_count=0;
                return nb9_driver->UpdateAndEnableHwBp(&info);
            }
            info.is_write_fp_regs=true;info.fp_reg_count=count;
            for(unsigned i=0;i<count;++i){info.fp_reg_indices[i]=regs[i];std::memcpy(info.fp_reg_values[i],&values[i],sizeof(float));}
            const auto key=std::make_pair(pid,point.address);
            if(std::find(nb9_written_keys_.begin(),nb9_written_keys_.end(),key)==nb9_written_keys_.end())nb9_written_keys_.push_back(key);
            if(!nb9_driver->UpdateAndEnableHwBp(&info))return native_bp_fail("NB9 FP-template modification",errno);
            return true;
        }
        return false;
    }
