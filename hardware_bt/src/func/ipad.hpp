#pragma once
#include "../other/memory.hpp"
namespace func {
    static constexpr uint64_t IPAD_OFFSET = 0x36C9BD0;

    inline float get_ipad() {
        if (proc::lib == 0) return 1.0f;
        return rpm<float>(proc::lib + IPAD_OFFSET);
    }

    inline void set_ipad(float value) {
        if (proc::lib == 0) return;
        wpm<float>(proc::lib + IPAD_OFFSET, value);
    }
}
