#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace bp_capture {
enum class RewriteKind { None, Direction, Quaternion };
struct RewriteSpec {
    RewriteKind kind;
    std::array<unsigned, 4> s;
    const char* label;
};
inline RewriteSpec rewrite_spec(uint64_t rva) {
    switch (rva) {
        case 0x7C5405C: return {RewriteKind::Direction, {3,4,5,0}, "Direction -> S3/S4/S5 (ray origin S0-S2)"};
        case 0x7C54594: return {RewriteKind::Direction, {13,12,11,0}, "Direction -> S13/S12/S11 (raycast output)"};
        case 0x7C56530: return {RewriteKind::Direction, {11,10,9,0}, "Direction -> S11/S10/S9 (alternate raycast output)"};
        case 0x72788BC: return {RewriteKind::Quaternion, {0,1,2,3}, "Camera quaternion -> S0/S1/S2/S3"};
        case 0x7014464: return {RewriteKind::Quaternion, {0,1,2,3}, "Aim quaternion argument -> S0/S1/S2/S3"};
        case 0x7014510: return {RewriteKind::Quaternion, {11,10,9,8}, "Aim quaternion store -> S11/S10/S9/S8"};
        case 0x7007308: return {RewriteKind::Quaternion, {0,1,2,3}, "Aim quaternion return -> S0/S1/S2/S3"};
        default: return {RewriteKind::None, {}, "Capture only: no complete register rewrite verified here"};
    }
}
inline bool direction_between(const std::array<float, 3>& origin, const std::array<float, 3>& target,
                              std::array<float, 3>& direction) {
    double square = 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (!std::isfinite(origin[i]) || !std::isfinite(target[i])) return false;
        const double d = double(target[i]) - origin[i];
        square += d*d;
    }
    if (!std::isfinite(square) || square < 1e-10) return false;
    const double length = std::sqrt(square);
    for (unsigned i = 0; i < 3; ++i) direction[i] = float((double(target[i])-origin[i])/length);
    return true;
}
inline std::array<unsigned,3> ray_origin_registers(uint64_t rva) {
    if (rva == 0x7C54594) return {15,9,8};
    if (rva == 0x7C56530) return {15,13,12};
    return {0,1,2}; // raycast entry ABI
}
}
