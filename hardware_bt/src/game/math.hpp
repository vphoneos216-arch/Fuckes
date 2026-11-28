#pragma once

#include "game.hpp"
#include "Offsets.h"
#include "../other/vector3.h"
#include "../other/memory.hpp"
#include "styles/styles.h"     // g_sw / g_sh
#include <cmath>

inline Vector3 GetPosition(uint64_t Transform) {
    if (Transform == 0) return Vector3::Zero();
    auto transformObjValue = rpm<uint64_t>(Transform + 0x10);
    if (transformObjValue == 0) return Vector3::Zero();

    auto matrixValue = rpm<uint64_t>(transformObjValue + 0x38);
    if (!matrixValue) return Vector3::Zero();

    uint64_t matrices = rpm<uint64_t>(matrixValue + 0x18);
    uint64_t indices  = rpm<uint64_t>(matrixValue + 0x20);
    if (!matrices || !indices) return Vector3::Zero();

    int indexValue = rpm<int>(transformObjValue + 0x40);
    if (indexValue < 0) return Vector3::Zero();

    auto resultValue = rpm<Vector3>(matrices + ((uint64_t)indexValue * 0x30));
    int transformIndexValue = rpm<int>(indices + ((uint64_t)indexValue * 4));

    int tries = 0;
    while (transformIndexValue >= 0 && tries < 50) {
        tries++;
        uint64_t matAddr = matrices + ((uint64_t)transformIndexValue * 0x30);
        auto t = rpm<TransformMatrix>(matAddr);
        if (t.scale.x == 0.0f && t.scale.y == 0.0f && t.scale.z == 0.0f) {
            t = rpm<TransformMatrix>(matAddr);
        }

        auto rx = t.rotation.x, ry = t.rotation.y, rz = t.rotation.z, rw = t.rotation.w;
        auto sx = resultValue.x * t.scale.x;
        auto sy = resultValue.y * t.scale.y;
        auto sz = resultValue.z * t.scale.z;

        resultValue.x = t.position.x + sx +
                        (sx * ((ry * ry * -2.0f) - (rz * rz * 2.0f))) +
                        (sy * ((rw * rz * -2.0f) - (ry * rx * -2.0f))) +
                        (sz * ((rz * rx *  2.0f) - (rw * ry * -2.0f)));
        resultValue.y = t.position.y + sy +
                        (sx * ((rx * ry *  2.0f) - (rw * rz * -2.0f))) +
                        (sy * ((rz * rz * -2.0f) - (rx * rx *  2.0f))) +
                        (sz * ((rw * rx * -2.0f) - (rz * ry * -2.0f)));
        resultValue.z = t.position.z + sz +
                        (sx * ((rw * ry * -2.0f) - (rx * rz * -2.0f))) +
                        (sy * ((ry * rz *  2.0f) - (rw * rx * -2.0f))) +
                        (sz * ((rx * rx * -2.0f) - (ry * ry *  2.0f)));

        transformIndexValue = rpm<int>(indices + ((uint64_t)transformIndexValue * 4));
    }

    if (tries >= 50) return Vector3::Zero();
    if (std::isnan(resultValue.x) || std::isnan(resultValue.y) || std::isnan(resultValue.z)) return Vector3::Zero();

    return resultValue;
}

inline Vector3 GetNodePosition(uint64_t nodeTransform) {
    if (nodeTransform == 0) return Vector3::Zero();
    auto transformValue = rpm<uint64_t>(nodeTransform + 0x10);
    if (transformValue == 0) return Vector3::Zero();
    return GetPosition(transformValue);
}

inline uint64_t GetPlayerHeadTF(uint64_t player) { return rpm<uint64_t>(player + Offsets::Head); }
inline uint64_t GetPlayerSpineTF(uint64_t player) { return rpm<uint64_t>(player + Offsets::Spine); }
inline uint64_t GetPlayerPeTF  (uint64_t player) { return rpm<uint64_t>(player + Offsets::Feet); }
// Generic bone transform read -- same one-line pattern as the three
// above, just parameterized on the offset, for the rest of the
// skeleton (shoulders/hands/hip/ankles) used by the full ESP skeleton.
inline uint64_t GetPlayerBoneTF(uint64_t player, uintptr_t offset) { return rpm<uint64_t>(player + offset); }

inline Vector3 WorldToScreenPoint(const matrix& vm, const Vector3& pos) {
    float v12 = (pos.x * vm.m14) + (pos.y * vm.m24) + (pos.z * vm.m34) + vm.m44;
    if (v12 >= 0.001f) {
        float inv = 1.0f / v12;
        float cx = g_sw * 0.5f;
        float cy = g_sh * 0.5f;
        float v9  = (pos.x * vm.m11) + (pos.y * vm.m21) + (pos.z * vm.m31) + vm.m41;
        float v10 = (pos.x * vm.m12) + (pos.y * vm.m22) + (pos.z * vm.m32) + vm.m42;
        return Vector3(cx + cx * v9 * inv, cy - cy * v10 * inv, 0);
    }
    return Vector3(-1, -1, -1);
}

// Extracts camera world position by solving the 3x3 projective system:
// [cx, cy, cz, 1] * vm = [0, 0, z_proj, 0]
inline Vector3 GetCameraPositionFromMatrix(const matrix& vm) {
    float a1 = vm.m11, b1 = vm.m21, c1 = vm.m31, d1 = -vm.m41;
    float a2 = vm.m12, b2 = vm.m22, c2 = vm.m32, d2 = -vm.m42;
    float a3 = vm.m14, b3 = vm.m24, c3 = vm.m34, d3 = -vm.m44;

    float det = a1 * (b2 * c3 - b3 * c2) - b1 * (a2 * c3 - a3 * c2) + c1 * (a2 * b3 - a3 * b2);
    if (fabsf(det) < 1e-6f) return Vector3::Zero();

    float detX = d1 * (b2 * c3 - b3 * c2) - b1 * (d2 * c3 - d3 * c2) + c1 * (d2 * b3 - d3 * b2);
    float detY = a1 * (d2 * c3 - d3 * c2) - d1 * (a2 * c3 - a3 * c2) + c1 * (a2 * d3 - a3 * d2);
    float detZ = a1 * (b2 * d3 - b3 * d2) - b1 * (a2 * d3 - a3 * d2) + d1 * (a2 * b3 - a3 * b2);

    Vector3 cam(detX / det, detY / det, detZ / det);
    if (std::isnan(cam.x) || std::isnan(cam.y) || std::isnan(cam.z) ||
        std::isinf(cam.x) || std::isinf(cam.y) || std::isinf(cam.z)) {
        return Vector3::Zero();
    }
    return cam;
}

inline float calculate_distance(const Vector3& a, const Vector3& b) noexcept {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}
