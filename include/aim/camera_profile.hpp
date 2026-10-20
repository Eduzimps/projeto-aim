#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

namespace aim::camera_profile {

// Build-specific STATIC CANDIDATE. These offsets do not establish projection
// conventions or validate entity/player layouts beyond the guarded root chain.
inline constexpr char file_sha256[] = "d1a6589bfe38da8f316b577cf8dc861e2aa87693b3a6c3846a0e1555540e9535";
inline constexpr std::uint64_t file_size = 141431288;
inline constexpr std::uint64_t image_size = 0x094e9000;
inline constexpr std::uint64_t engine_global_rva = 0x080d5980;
inline constexpr std::uint64_t engine_vtable_rva = 0x04e68940;
inline constexpr std::uint64_t gameplay_vtable_rva = 0x04f2a280;
inline constexpr std::uint64_t player_vtable_rva = 0x04f9ca40;
inline constexpr std::uint64_t rdata_begin_rva = 0x04e34000;
inline constexpr std::uint64_t rdata_end_rva = 0x07635216; // Exclusive.
inline constexpr std::uint64_t max_user_address = 0x00007fffffffffff;

using ReadBytes = std::function<bool(std::uint64_t, void*, std::size_t)>;

struct CameraValues {
    std::array<float, 12> view43{};
    float fov_degrees = 0.0f;
    float near_plane = 0.0f;
    float far_plane = 0.0f;
};

enum class ProbeStatus {
    candidate_sample, invalid_module, invalid_reader, null_pointer,
    invalid_address, read_failed, vtable_mismatch, camera_vtable_outside_rdata,
    nonfinite_view, invalid_projection_parameters
};

inline const char* status_name(ProbeStatus status) noexcept {
    switch (status) {
    case ProbeStatus::candidate_sample: return "candidate_sample";
    case ProbeStatus::invalid_module: return "invalid_module";
    case ProbeStatus::invalid_reader: return "invalid_reader";
    case ProbeStatus::null_pointer: return "null_pointer";
    case ProbeStatus::invalid_address: return "invalid_address";
    case ProbeStatus::read_failed: return "read_failed";
    case ProbeStatus::vtable_mismatch: return "vtable_mismatch";
    case ProbeStatus::camera_vtable_outside_rdata: return "camera_vtable_outside_rdata";
    case ProbeStatus::nonfinite_view: return "nonfinite_view";
    case ProbeStatus::invalid_projection_parameters: return "invalid_projection_parameters";
    }
    return "unknown";
}

struct ProbeResult {
    ProbeStatus status = ProbeStatus::invalid_module;
    const char* stage = "module";
    bool root_vtables_matched = false;
    bool camera_vtable_in_rdata = false;
    std::uint64_t camera_address = 0; // Internal consistency check; never serialize.
    CameraValues values;
};

// Canonical 64-bit user addresses only; rejects wraparound and the null page.
inline bool checked_address(std::uint64_t base, std::uint64_t offset,
                            std::size_t bytes, std::uint64_t& result) noexcept {
    if (base < 0x10000 || base > max_user_address || bytes == 0 ||
        offset > max_user_address - base) return false;
    result = base + offset;
    return bytes - 1 <= max_user_address - result;
}

inline ProbeResult probe(std::uint64_t module_base, std::uint64_t mapped_size,
                         const ReadBytes& reader) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                  "The profile requires IEEE-754 binary32");
    ProbeResult result;
    std::uint64_t address = 0;
    if (mapped_size != image_size ||
        !checked_address(module_base, 0, static_cast<std::size_t>(mapped_size), address)) return result;
    if (!reader) {
        result.status = ProbeStatus::invalid_reader;
        return result;
    }
    const auto read_at = [&](std::uint64_t base, std::uint64_t offset,
                             void* destination, std::size_t bytes, const char* stage) {
        result.stage = stage;
        if (base == 0) { result.status = ProbeStatus::null_pointer; return false; }
        std::uint64_t location = 0;
        if (!checked_address(base, offset, bytes, location)) {
            result.status = ProbeStatus::invalid_address;
            return false;
        }
        if (!reader(location, destination, bytes)) {
            result.status = ProbeStatus::read_failed;
            return false;
        }
        return true;
    };
    const auto guard_vtable = [&](std::uint64_t object, std::uint64_t expected_rva,
                                  const char* stage) {
        std::uint64_t vtable = 0;
        if (!read_at(object, 0, &vtable, sizeof(vtable), stage)) return false;
        if (vtable != module_base + expected_rva) {
            result.status = ProbeStatus::vtable_mismatch;
            return false;
        }
        return true;
    };
    std::uint64_t engine = 0, gameplay = 0, player = 0, camera = 0, camera_vtable = 0;
    if (!read_at(module_base, engine_global_rva, &engine, sizeof(engine), "engine_global") ||
        !guard_vtable(engine, engine_vtable_rva, "engine_vtable") ||
        !read_at(engine, 0x58, &gameplay, sizeof(gameplay), "engine_gameplay") ||
        !guard_vtable(gameplay, gameplay_vtable_rva, "gameplay_vtable") ||
        !read_at(gameplay, 0x58, &player, sizeof(player), "gameplay_player") ||
        !guard_vtable(player, player_vtable_rva, "player_vtable")) return result;
    result.root_vtables_matched = true;
    if (!read_at(player, 0xd88, &camera, sizeof(camera), "player_camera") ||
        !read_at(camera, 0, &camera_vtable, sizeof(camera_vtable), "camera_vtable")) return result;
    if (camera_vtable < module_base + rdata_begin_rva ||
        camera_vtable >= module_base + rdata_end_rva) {
        result.status = ProbeStatus::camera_vtable_outside_rdata;
        return result;
    }
    result.camera_vtable_in_rdata = true;
    if (!read_at(camera, 0x41c, result.values.view43.data(), 12 * sizeof(float), "camera_view43")) return result;
    std::array<float, 3> parameters{};
    if (!read_at(camera, 0x454, parameters.data(), sizeof(parameters), "camera_fov_depth")) return result;
    result.values.fov_degrees = parameters[0];
    result.values.near_plane = parameters[1];
    result.values.far_plane = parameters[2];
    for (float value : result.values.view43) {
        if (!std::isfinite(value)) {
            result.stage = "camera_view43";
            result.status = ProbeStatus::nonfinite_view;
            return result;
        }
    }
    if (!std::isfinite(parameters[0]) || !std::isfinite(parameters[1]) ||
        !std::isfinite(parameters[2]) || parameters[0] <= 0.0f || parameters[0] >= 180.0f ||
        parameters[1] <= 0.0f || parameters[2] <= parameters[1]) {
        result.status = ProbeStatus::invalid_projection_parameters;
        return result;
    }
    result.stage = "candidate_camera";
    result.camera_address = camera;
    result.status = ProbeStatus::candidate_sample;
    return result;
}

} // namespace aim::camera_profile
