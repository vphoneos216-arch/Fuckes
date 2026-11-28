#pragma once
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include "player.hpp"
#include "math.hpp"

namespace data {
    void update();
    void thread_start();
    void thread_stop();

    extern std::vector<PlayerData> players;
    extern std::mutex              players_mutex;
    extern std::atomic<uint64_t>   localPlayerAddr;
    extern std::atomic<bool>       inMatch;
    extern matrix                  viewMatrix;
    // Published with players under players_mutex; touch control must not combine
    // a newer camera matrix with an older target list.
    struct TouchFrame {
        matrix view{};
        std::chrono::steady_clock::time_point sampled{};
        uint64_t sequence=0;
    };
    extern TouchFrame touchFrame;
    
    extern std::atomic<int>        player_count;   // ✅ ADDED
}
