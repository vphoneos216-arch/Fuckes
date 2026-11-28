#pragma once
#include <cstddef>
#include <cstdint>

namespace bp_capture {
enum class AddressKind { Code, Player, AimInfo };
struct Target {
    const char* label;
    AddressKind kind;
    uint64_t offset;
    unsigned components;
    const char* detail;
};
// Fixed offsets from the IDA input documented in ida_direction_camera_findings.txt.
inline constexpr Target targets[] = {
    {"Aim direction | *(Player+E58)+40", AddressKind::AimInfo, 0x40, 3, "Aim ray direction; final weapon spread may differ."},
    {"Aim origin | *(Player+E58)+4C", AddressKind::AimInfo, 0x4C, 3, "Aim ray start position."},
    {"Aim quaternion | Player+61C", AddressKind::Player, 0x61C, 4, "Player aim rotation quaternion."},
    {"Follow camera rotation call | +72788BC", AddressKind::Code, 0x72788BC, 1, "Camera Transform in X0; quaternion in S0-S3."},
    {"set_rotation_Injected | +A1202A4", AddressKind::Code, 0xA1202A4, 1, "Generic Transform: X0=Transform, X1=Quaternion pointer."},
    {"SetPositionAndRotation_Injected | +A120824", AddressKind::Code, 0xA120824, 1, "Generic Transform: X0=Transform, X1=position pointer, X2=rotation pointer."},
    {"set_localRotation_Injected | +A12032C", AddressKind::Code, 0xA12032C, 1, "Generic Transform: X0=Transform, X1=local Quaternion pointer."},
    {"Vertical-view forward call | +80C2674", AddressKind::Code, 0x80C2674, 1, "Vertical-view mode: X0=ArtRoot, S0-S2=forward vector."},
    {"Player.UpdateAiming | +707A364", AddressKind::Code, 0x707A364, 1, "Player in X0; builds and adjusts the aim ray."},
    {"Aim direction store | +707BD60", AddressKind::Code, 0x707BD60, 1, "Before store: X9=aim record, S1=x, S0=y; z reloaded afterward."},
    {"Aim raycast helper | +7C5405C", AddressKind::Code, 0x7C5405C, 1, "Origin in S0-S2, direction in S3-S5 at entry."},
    {"Weapon direction consumer | +751F63C", AddressKind::Code, 0x751F63C, 1, "Returns normalized/scaled aim vector in S0-S2."},
    {"Player.GetAimRotation | +70072C0", AddressKind::Code, 0x70072C0, 1, "Player in X0; returns quaternion in S0-S3."},
    {"GetLocalAimDirection | +829C624", AddressKind::Code, 0x829C624, 1, "UGC API reads direction from the main aiming record."},
    {"Aim quaternion setter entry | +7014464", AddressKind::Code, 0x7014464, 1, "Player in X0; aim quaternion argument in S0-S3."},
    {"Aim quaternion first store | +7014510", AddressKind::Code, 0x7014510, 1, "Quaternion x/y/z/w in S11/S10/S9/S8."},
    {"Raycast direction output | +7C54594", AddressKind::Code, 0x7C54594, 1, "Direction x/y/z in S13/S12/S11 before the stores."},
    {"Alternate raycast direction output | +7C56530", AddressKind::Code, 0x7C56530, 1, "Direction x/y/z in S11/S10/S9 before the stores."},
    {"Aim quaternion return | +7007308", AddressKind::Code, 0x7007308, 1, "Normal getter return quaternion in S0-S3, after its loads."},
};
inline constexpr std::size_t target_count = sizeof(targets) / sizeof(targets[0]);
inline bool valid_pointer(uint64_t p) { return p >= 0x10000 && p < 0x00007FFFFFFFFFFFULL; }
template<class Read>
uint64_t resolve(const Target& target, unsigned component, uint64_t base, uint64_t player, Read read) {
    if (component >= target.components) return 0;
    uint64_t owner = base;
    if (target.kind != AddressKind::Code) {
        if (!valid_pointer(player)) return 0;
        owner = player;
        if (target.kind == AddressKind::AimInfo &&
            (!read(player + 0xE58, &owner, sizeof(owner)) || !valid_pointer(owner))) return 0;
    }
    if (!valid_pointer(owner)) return 0;
    const uint64_t address = owner + target.offset + component * 4;
    return valid_pointer(address) && !(address & 3) ? address : 0;
}
}
