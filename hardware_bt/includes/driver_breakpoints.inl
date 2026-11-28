// Included inside c_driver. Native protocol types deliberately remain distinct.
private:
    bool bp_hardware_active = false;
    bool bp_pte_active = false;
    bool bp_clone_active = false;
    bool bp_brk_active = false;
    bool bp_wrp_active = false;

    bool paradise_bp_ready() const {
        return selected_driver_ == 3 && paradise_connected && paradise && pid > 0;
    }
    bool wind_bp_ready() const {
        return selected_driver_ == 5 && wind && wind->ok() && wind->is_bound() &&
               pid > 0 && wind->target_pid() == pid;
    }
    // Call before target changes and while API objects still exist at shutdown.
    bool release_breakpoints() {
        bool ok = true;
        if(selected_driver_==4 || selected_driver_==6){
            std::lock_guard<std::mutex> lock(paradise_lock);
            ok=native_remove_locked();
        }
        if (bp_hardware_active) ok = hwbp_remove() && ok;
        if (bp_pte_active) ok = ptebp_remove() && ok;
        if (bp_clone_active) ok = clonebp_remove() && ok;
        if (bp_brk_active) ok = brkbp_remove() && ok;
        if (bp_wrp_active) ok = wrpbp_remove() && ok;
        return ok;
    }
public:
    bool supports_breakpoints() const {
        return selected_driver_ == 3 || selected_driver_ == 4 || selected_driver_ == 5 || selected_driver_ == 6;
    }

    // HWBP native API; bool results mean the driver acknowledged success.
    bool hwbp_set(const paradise_bp_point_config* points, size_t count) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (!paradise_bp_ready() || !points || count == 0 || count > PARADISE_BP_MAX_POINTS) return false;
        bool ok = paradise->hwbp_set(points, count);
        if (ok) bp_hardware_active = true;
        return ok;
    }
    bool hwbp_get_config(paradise_bp_config* out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (out) *out = {};
        return paradise_bp_ready() && out && paradise->hwbp_get_config(out);
    }
    bool hwbp_get_records(uint32_t point, uint32_t start, paradise_bp_record* records,
                          uint32_t capacity, uint32_t* count_out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (count_out) *count_out = 0;
        return paradise_bp_ready() && point < PARADISE_BP_MAX_POINTS && records &&
               capacity > 0 && capacity <= PARADISE_BP_MAX_RECORDS && count_out &&
               paradise->hwbp_get_records(point, start, records, capacity, count_out);
    }
    bool hwbp_set_record(uint32_t point, uint32_t index, const paradise_bp_record& record) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        return paradise_bp_ready() && point < PARADISE_BP_MAX_POINTS && index < PARADISE_BP_MAX_RECORDS &&
               paradise->hwbp_set_record(point, index, &record);
    }
    bool hwbp_set(const wind_backend::HWBP_POINT_CONFIG* points, uint32_t count) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (!wind_bp_ready() || !points || count == 0 || count > WIND_BP_MAX_POINTS) return false;
        bool ok = wind->hwbp_set(points, count) == 0;
        if (ok) bp_hardware_active = true;
        return ok;
    }
    bool hwbp_get_config(wind_backend::HWBP_CONFIG* out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (out) *out = {};
        return wind_bp_ready() && out && wind->hwbp_get_config(out) == 0;
    }
    bool hwbp_get_records(uint32_t point, uint32_t start, wind_backend::HWBP_RECORD* records,
                          uint32_t capacity, uint32_t* count_out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (count_out) *count_out = 0;
        return wind_bp_ready() && point < WIND_BP_MAX_POINTS && records && capacity > 0 &&
               capacity <= WIND_BP_RECORDS_MAX && count_out &&
               wind->hwbp_get_records(point, start, records, capacity, count_out) == 0;
    }
    bool hwbp_set_record(uint32_t point, uint32_t index, const wind_backend::HWBP_RECORD& record) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        return wind_bp_ready() && point < WIND_BP_MAX_POINTS && index < WIND_BP_RECORDS_MAX &&
               wind->hwbp_set_record(point, index, record) == 0;
    }
    bool hwbp_remove() {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if(selected_driver_==4 || selected_driver_==6)return native_remove_locked();
        bool ok = false;
        if (paradise_bp_ready()) ok = paradise->hwbp_remove();
        if (wind_bp_ready()) ok = wind->hwbp_remove() == 0;
        if (ok) bp_hardware_active = false;
        return ok;
    }
    bool hwbp_clear_records(int32_t point = -1) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (point < -1) return false;
        if (paradise_bp_ready()) return point < PARADISE_BP_MAX_POINTS && paradise->hwbp_clear_records(point);
        return wind_bp_ready() && point < WIND_BP_MAX_POINTS && wind->hwbp_clear_records(point) == 0;
    }

    // PTEBP native API; bool results mean the driver acknowledged success.
    bool ptebp_set(const paradise_bp_point_config* points, size_t count) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (!paradise_bp_ready() || !points || count == 0 || count > PARADISE_BP_MAX_POINTS) return false;
        bool ok = paradise->ptebp_set(points, count);
        if (ok) bp_pte_active = true;
        return ok;
    }
    bool ptebp_get_config(paradise_bp_config* out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (out) *out = {};
        return paradise_bp_ready() && out && paradise->ptebp_get_config(out);
    }
    bool ptebp_get_records(uint32_t point, uint32_t start, paradise_bp_record* records,
                          uint32_t capacity, uint32_t* count_out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (count_out) *count_out = 0;
        return paradise_bp_ready() && point < PARADISE_BP_MAX_POINTS && records &&
               capacity > 0 && capacity <= PARADISE_BP_MAX_RECORDS && count_out &&
               paradise->ptebp_get_records(point, start, records, capacity, count_out);
    }
    bool ptebp_set_record(uint32_t point, uint32_t index, const paradise_bp_record& record) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        return paradise_bp_ready() && point < PARADISE_BP_MAX_POINTS && index < PARADISE_BP_MAX_RECORDS &&
               paradise->ptebp_set_record(point, index, &record);
    }
    bool ptebp_set(const wind_backend::PTEBP_POINT_CONFIG* points, uint32_t count) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (!wind_bp_ready() || !points || count == 0 || count > WIND_BP_MAX_POINTS) return false;
        bool ok = wind->ptebp_set(points, count) == 0;
        if (ok) bp_pte_active = true;
        return ok;
    }
    bool ptebp_get_config(wind_backend::PTEBP_CONFIG* out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (out) *out = {};
        return wind_bp_ready() && out && wind->ptebp_get_config(out) == 0;
    }
    bool ptebp_get_records(uint32_t point, uint32_t start, wind_backend::PTEBP_RECORD* records,
                          uint32_t capacity, uint32_t* count_out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (count_out) *count_out = 0;
        return wind_bp_ready() && point < WIND_BP_MAX_POINTS && records && capacity > 0 &&
               capacity <= WIND_BP_RECORDS_MAX && count_out &&
               wind->ptebp_get_records(point, start, records, capacity, count_out) == 0;
    }
    bool ptebp_set_record(uint32_t point, uint32_t index, const wind_backend::PTEBP_RECORD& record) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        return wind_bp_ready() && point < WIND_BP_MAX_POINTS && index < WIND_BP_RECORDS_MAX &&
               wind->ptebp_set_record(point, index, record) == 0;
    }
    bool ptebp_remove() {
        std::lock_guard<std::mutex> lock(paradise_lock);
        bool ok = false;
        if (paradise_bp_ready()) ok = paradise->ptebp_remove();
        if (wind_bp_ready()) ok = wind->ptebp_remove() == 0;
        if (ok) bp_pte_active = false;
        return ok;
    }
    bool ptebp_clear_records(int32_t point = -1) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (point < -1) return false;
        if (paradise_bp_ready()) return point < PARADISE_BP_MAX_POINTS && paradise->ptebp_clear_records(point);
        return wind_bp_ready() && point < WIND_BP_MAX_POINTS && wind->ptebp_clear_records(point) == 0;
    }

    // CLONEBP native API; bool results mean the driver acknowledged success.
    bool clonebp_set(const paradise_bp_point_config* points, size_t count) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (!paradise_bp_ready() || !points || count == 0 || count > PARADISE_BP_MAX_POINTS) return false;
        bool ok = paradise->clonebp_set(points, count);
        if (ok) bp_clone_active = true;
        return ok;
    }
    bool clonebp_get_config(paradise_bp_config* out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (out) *out = {};
        return paradise_bp_ready() && out && paradise->clonebp_get_config(out);
    }
    bool clonebp_get_records(uint32_t point, uint32_t start, paradise_bp_record* records,
                          uint32_t capacity, uint32_t* count_out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (count_out) *count_out = 0;
        return paradise_bp_ready() && point < PARADISE_BP_MAX_POINTS && records &&
               capacity > 0 && capacity <= PARADISE_BP_MAX_RECORDS && count_out &&
               paradise->clonebp_get_records(point, start, records, capacity, count_out);
    }
    bool clonebp_set_record(uint32_t point, uint32_t index, const paradise_bp_record& record) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        return paradise_bp_ready() && point < PARADISE_BP_MAX_POINTS && index < PARADISE_BP_MAX_RECORDS &&
               paradise->clonebp_set_record(point, index, &record);
    }
    // bytes must include the flexible points array in the WIND request.
    bool clonebp_set(const wind_backend::CLONEBP_SET_REQUEST* request, size_t bytes) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (!wind_bp_ready() || !request || bytes < sizeof(*request) ||
            request->pid != static_cast<uint32_t>(pid) || request->point_count == 0 ||
            request->point_count > CLONEBP_MAX_POINTS ||
            bytes < sizeof(*request) + request->point_count * sizeof(wind_backend::CLONEBP_POINT_CONFIG)) return false;
        bool ok = wind->clonebp_set(request) == 0;
        if (ok) bp_clone_active = true;
        return ok;
    }
    bool clonebp_get_config(wind_backend::CLONEBP_CONFIG* out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (out) *out = {};
        return wind_bp_ready() && out && wind->clonebp_get_config(out) == 0;
    }
    bool clonebp_get_records(uint32_t point, uint32_t start, wind_backend::CLONEBP_RECORD* records,
                          uint32_t capacity, uint32_t* count_out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (count_out) *count_out = 0;
        return wind_bp_ready() && point < CLONEBP_MAX_POINTS && records && capacity > 0 &&
               capacity <= CLONEBP_RECORD_MAX && count_out &&
               wind->clonebp_get_records(point, start, records, capacity, count_out) == 0;
    }
    bool clonebp_remove() {
        std::lock_guard<std::mutex> lock(paradise_lock);
        bool ok = false;
        if (paradise_bp_ready()) ok = paradise->clonebp_remove();
        if (wind_bp_ready()) ok = wind->clonebp_remove() == 0;
        if (ok) bp_clone_active = false;
        return ok;
    }
    bool clonebp_clear_records(int32_t point = -1) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (point < -1) return false;
        if (paradise_bp_ready()) return point < PARADISE_BP_MAX_POINTS && paradise->clonebp_clear_records(point);
        return wind_bp_ready() && point < CLONEBP_MAX_POINTS && wind->clonebp_clear_records(point) == 0;
    }

    // BRKBP native API; bool results mean the driver acknowledged success.
    bool brkbp_set(const wind_backend::BRKBP_POINT_CONFIG* points, uint32_t count) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (!wind_bp_ready() || !points || count == 0 || count > WIND_BP_MAX_POINTS) return false;
        bool ok = wind->brkbp_set(points, count) == 0;
        if (ok) bp_brk_active = true;
        return ok;
    }
    bool brkbp_get_config(wind_backend::BRKBP_CONFIG* out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (out) *out = {};
        return wind_bp_ready() && out && wind->brkbp_get_config(out) == 0;
    }
    bool brkbp_get_records(uint32_t point, uint32_t start, wind_backend::BRKBP_RECORD* records,
                          uint32_t capacity, uint32_t* count_out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (count_out) *count_out = 0;
        return wind_bp_ready() && point < WIND_BP_MAX_POINTS && records && capacity > 0 &&
               capacity <= WIND_BP_RECORDS_MAX && count_out &&
               wind->brkbp_get_records(point, start, records, capacity, count_out) == 0;
    }
    bool brkbp_set_record(uint32_t point, uint32_t index, const wind_backend::BRKBP_RECORD& record) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        return wind_bp_ready() && point < WIND_BP_MAX_POINTS && index < WIND_BP_RECORDS_MAX &&
               wind->brkbp_set_record(point, index, record) == 0;
    }
    bool brkbp_remove() {
        std::lock_guard<std::mutex> lock(paradise_lock);
        bool ok = false;
        if (wind_bp_ready()) ok = wind->brkbp_remove() == 0;
        if (ok) bp_brk_active = false;
        return ok;
    }
    bool brkbp_clear_records(int32_t point = -1) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (point < -1) return false;
        return wind_bp_ready() && point < WIND_BP_MAX_POINTS && wind->brkbp_clear_records(point) == 0;
    }

    // WRPBP native API; bool results mean the driver acknowledged success.
    bool wrpbp_set(const wind_backend::WRPBP_POINT_CONFIG* points, uint32_t count) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (!wind_bp_ready() || !points || count == 0 || count > WIND_BP_MAX_POINTS) return false;
        bool ok = wind->wrpbp_set(points, count) == 0;
        if (ok) bp_wrp_active = true;
        return ok;
    }
    bool wrpbp_get_config(wind_backend::WRPBP_CONFIG* out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (out) *out = {};
        return wind_bp_ready() && out && wind->wrpbp_get_config(out) == 0;
    }
    bool wrpbp_get_records(uint32_t point, uint32_t start, wind_backend::HWBP_RECORD* records,
                          uint32_t capacity, uint32_t* count_out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (count_out) *count_out = 0;
        return wind_bp_ready() && point < WIND_BP_MAX_POINTS && records && capacity > 0 &&
               capacity <= WIND_WRPBP_RECORDS_MAX && count_out &&
               wind->wrpbp_get_records(point, start, records, capacity, count_out) == 0;
    }
    bool wrpbp_remove() {
        std::lock_guard<std::mutex> lock(paradise_lock);
        bool ok = false;
        if (wind_bp_ready()) ok = wind->wrpbp_remove() == 0;
        if (ok) bp_wrp_active = false;
        return ok;
    }
    bool wrpbp_clear_records(int32_t point = -1) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (point < -1) return false;
        return wind_bp_ready() && point < WIND_BP_MAX_POINTS && wind->wrpbp_clear_records(point) == 0;
    }
    bool hwbp_set_regs(int32_t point, bool enable, float x, float y, float z, uint32_t mask = 0x7) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        return wind_bp_ready() && point >= 0 && point < WIND_BP_MAX_POINTS && !(mask & ~0x7u) &&
               wind->hwbp_set_regs(point, enable, x, y, z, mask) == 0;
    }
    bool ptebp_set_regs(int32_t point, bool enable, float x, float y, float z, uint32_t mask = 0x7) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        return wind_bp_ready() && point >= 0 && point < WIND_BP_MAX_POINTS && !(mask & ~0x7u) &&
               wind->ptebp_set_regs(point, enable, x, y, z, mask) == 0;
    }
    bool wrpbp_set_vec(int32_t point, bool enable, float x, float y, float z, uint32_t mask = 0x7) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        return wind_bp_ready() && point >= 0 && point < WIND_BP_MAX_POINTS && !(mask & ~0x7u) &&
               wind->wrpbp_set_vec(point, enable, x, y, z, mask) == 0;
    }
    bool hwbp_refresh() {
        std::lock_guard<std::mutex> lock(paradise_lock);
        return wind_bp_ready() && wind->hwbp_refresh() == 0;
    }
    bool ptebp_refresh() {
        std::lock_guard<std::mutex> lock(paradise_lock);
        return wind_bp_ready() && wind->ptebp_refresh() == 0;
    }
    bool clonebp_refresh() {
        std::lock_guard<std::mutex> lock(paradise_lock);
        return wind_bp_ready() && wind->clonebp_refresh() == 0;
    }
    bool bp_diag(wind_backend::WIND_BP_DIAG* out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (out) *out = {};
        return wind_bp_ready() && out && wind->bp_diag(out) == 0;
    }
    bool ptebp_get_coord_samples(uint64_t after_sequence, paradise_ptebp_coord_sample* samples,
                                  uint32_t capacity, uint32_t* count_out, uint64_t* newest_sequence_out) {
        std::lock_guard<std::mutex> lock(paradise_lock);
        if (count_out) *count_out = 0;
        if (newest_sequence_out) *newest_sequence_out = 0;
        return paradise_bp_ready() && samples && capacity > 0 &&
               capacity <= PARADISE_PTEBP_MAX_COORD_SAMPLES && count_out && newest_sequence_out &&
               paradise->ptebp_get_coord_samples(after_sequence, samples, capacity, count_out, newest_sequence_out);
    }
    bool bp_remove_all() {
        if (!paradise_bp_ready() && !wind_bp_ready()) return false;
        bool ok = hwbp_remove();
        ok = ptebp_remove() && ok;
        ok = clonebp_remove() && ok;
        if (selected_driver_ == 5) {
            ok = brkbp_remove() && ok;
            ok = wrpbp_remove() && ok;
        }
        return ok;
    }
