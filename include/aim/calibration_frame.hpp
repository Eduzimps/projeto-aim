#pragma once

#include "aim/scene_profile.hpp"
#include "aim/messiah_projection.hpp"

namespace aim::calibration {
inline constexpr std::size_t max_markers = 64;
enum class State { unavailable, viewport_mismatch, ready };
struct Marker { Vec2 pixel; std::size_t number = 0; };
struct Frame {
    State state = State::unavailable;
    bool partial = true;
    std::size_t candidates = 0;
    std::vector<Marker> markers;
};

// Refresh the camera after the entity walk, sharing its remaining read budget.
// This reduces camera age at presentation; live reads are still not atomic.
inline bool refresh_camera(scene_profile::Result& scene, std::uint64_t module_base,
                           std::uint64_t mapped_size, const camera_profile::ReadBytes& reader) {
    const bool eligible = scene.camera_geometry_read && scene.scene_guards_matched &&
        scene.consistency_recheck_passed;
    scene.camera_geometry_read = false;
    if (!eligible || !reader) return false;
    const camera_profile::ReadBytes bounded = [&](auto address, void* destination, std::size_t bytes) {
        if (scene.read_count >= scene_profile::max_reads ||
            bytes > scene_profile::max_bytes - scene.read_bytes) return false;
        ++scene.read_count; scene.read_bytes += bytes;
        return reader(address,destination,bytes);
    };
    const auto latest = camera_profile::probe(module_base,mapped_size,bounded);
    if (latest.status != camera_profile::ProbeStatus::candidate_sample ||
        latest.camera_address != scene.camera_address) return false;
    const auto read_at = [&](std::uint64_t offset, void* destination, std::size_t bytes) {
        std::uint64_t address = 0;
        return camera_profile::checked_address(latest.camera_address,offset,bytes,address) &&
            bounded(address,destination,bytes);
    };
    std::uint64_t vtable = 0;
    std::array<float,3> origin{};
    std::array<std::uint16_t,2> viewport{};
    if (!read_at(0,&vtable,sizeof(vtable)) || vtable != module_base + scene_profile::camera_vtable_rva ||
        !read_at(0x7c,origin.data(),sizeof(origin)) ||
        !read_at(0x408,viewport.data(),sizeof(viewport))) return false;
    scene.camera_probe = latest; scene.origin = origin;
    scene.viewport_width = viewport[0]; scene.viewport_height = viewport[1];
    scene.camera_geometry_read = true;
    return true;
}

inline bool supported_viewport(int width, int height) noexcept {
    return width > 0 && height > 0 && width <= 8192 && height <= 8192 &&
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) <= 33554432;
}

// A technical signature for calibration, never a player/enemy classifier.
// Positions are origins, not a head/bone or an inferred body-height offset.
inline Frame make_frame(const scene_profile::Result& scene, int width, int height) {
    Frame frame;
    if (!scene.camera_geometry_read || !scene.scene_guards_matched || !scene.local_role_present ||
        !scene.consistency_recheck_passed ||
        (scene.status != scene_profile::Status::candidate_collection &&
         scene.status != scene_profile::Status::node_limit_reached)) return frame;
    if (!supported_viewport(width, height) || width != scene.viewport_width || height != scene.viewport_height) {
        frame.state = State::viewport_mismatch; return frame;
    }
    const auto& values = scene.camera_probe.values;
    if (!std::isfinite(values.near_plane) || !std::isfinite(values.far_plane) ||
        values.near_plane <= 0 || values.far_plane <= values.near_plane ||
        !std::isfinite(values.fov_degrees) || values.fov_degrees <= 0 || values.fov_degrees >= 180) return frame;
    messiah_projection::Camera camera;
    std::copy_n(values.view43.begin(), 9, camera.rotation.begin());
    camera.origin = {scene.origin[0], scene.origin[1], scene.origin[2]};
    camera.viewport = {static_cast<double>(width), static_cast<double>(height)};
    camera.vertical_fov_degrees = values.fov_degrees;
    if (!finite(camera.origin) || !std::all_of(camera.rotation.begin(), camera.rotation.end(),
                                             [](float value) { return std::isfinite(value); })) return frame;
    frame.state = State::ready;
    frame.partial = !scene.collection_complete;
    for (const auto& entity : scene.entities) {
        if (!entity.native_entity_type_known || !entity.skeleton_present ||
            !entity.character_controller_checked || !entity.character_controller_present ||
            entity.is_local_role || !entity.position_checked ||
            entity.resource.observed_vtable_rva != entity_identity::actor_vtable_rva) continue;
        ++frame.candidates;
        const auto projected = messiah_projection::project(entity.position, camera);
        // Clip before any integer raster conversion, including the huge but
        // finite coordinates caused by points almost on the camera plane.
        if (!projected || !projected->in_viewport || projected->depth < values.near_plane ||
            projected->depth > values.far_plane) continue;
        if (frame.markers.size() >= max_markers) { frame.partial = true; continue; }
        frame.markers.push_back({projected->pixel, frame.candidates});
    }
    return frame;
}
} // namespace aim::calibration
