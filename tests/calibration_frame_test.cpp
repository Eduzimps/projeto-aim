#include "aim/calibration_frame.hpp"
#include <cstdlib>
#include <iostream>

namespace c = aim::calibration;
namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
aim::scene_profile::Result scene() {
    aim::scene_profile::Result s;
    s.status = aim::scene_profile::Status::node_limit_reached;
    s.camera_geometry_read = s.scene_guards_matched = s.consistency_recheck_passed = true;
    s.local_role_present = true;
    s.viewport_width = 1600; s.viewport_height = 900;
    s.camera_probe.values.view43 = {{1,0,0,0,1,0,0,0,1,0,0,0}};
    s.camera_probe.values.fov_degrees = 90;
    s.camera_probe.values.near_plane = .1f; s.camera_probe.values.far_plane = 3000;
    aim::scene_profile::EntitySummary e;
    e.native_entity_type_known = e.skeleton_present = e.character_controller_checked = true;
    e.character_controller_present = e.position_checked = true;
    e.resource.observed_vtable_rva = aim::entity_identity::actor_vtable_rva;
    e.position = {0,0,-10}; s.entities.push_back(e);
    return s;
}
}
int main() {
    auto s = scene(); auto f = c::make_frame(s,1600,900);
    check(f.state == c::State::ready && f.partial && f.markers.size() == 1 &&
          f.markers[0].pixel.x == 800 && f.markers[0].pixel.y == 450,
          "a rechecked partial prefix yields a clipped origin marker");
    s.entities[0].is_local_role = true;
    check(c::make_frame(s,1600,900).candidates == 0, "local role excluded");
    s = scene(); s.entities[0].resource.observed_vtable_rva = aim::entity_identity::skeleton_vtable_rva;
    check(c::make_frame(s,1600,900).candidates == 0, "base skeleton alone is not the actor signature");
    s = scene(); s.entities[0].character_controller_present = false;
    check(c::make_frame(s,1600,900).candidates == 0, "animated attachments without controller excluded");
    for (auto position : {aim::Vec3{0,0,-1e-9}, aim::Vec3{0,0,-4000}, aim::Vec3{0,0,1},
                          aim::Vec3{10000,0,-1}, aim::Vec3{0,10000,-1e-9}}) {
        s = scene(); s.entities[0].position = position;
        check(c::make_frame(s,1600,900).markers.empty(), "near/far/behind/offscreen positions never reach rasterization");
    }
    s = scene(); s.consistency_recheck_passed = false;
    check(c::make_frame(s,1600,900).state == c::State::unavailable, "unrechecked observations are not displayed");
    s = scene(); s.status = aim::scene_profile::Status::read_failed;
    check(c::make_frame(s,1600,900).markers.empty(), "read failures cannot retain stale markers");
    s = scene(); s.local_role_present = false;
    check(c::make_frame(s,1600,900).state == c::State::unavailable, "a missing local reference cannot label candidates as nonlocal");
    s = scene();
    check(c::make_frame(s,1920,1080).state == c::State::viewport_mismatch,
          "viewport mismatch is explicit rather than guessing letterbox scaling");
    const auto e = s.entities[0]; s.entities.assign(100,e);
    check(c::make_frame(s,1600,900).markers.size() == c::max_markers, "raster marker count capped");
    check(!c::supported_viewport(0,900) && !c::supported_viewport(8192,8192), "invalid or excessive bitmap sizes rejected");
    if (failures) return EXIT_FAILURE;
    std::cout << "All calibration frame checks passed\n";
}
