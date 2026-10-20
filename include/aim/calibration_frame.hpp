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
