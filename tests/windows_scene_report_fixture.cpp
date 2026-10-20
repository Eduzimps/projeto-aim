// Exercise the production JSON writer with synthetic values only. This program
// never opens the game, reads process memory, or saves a Desktop report.
#define main zyon_production_probe_main
#include "../src/windows_scene_probe.cpp"
#undef main

int main() {
    Report report;
    report.status = "synthetic_fixture";
    report.scene_attempted = true;
    auto& scene = report.scene;
    scene.stage = "synthetic_fixture";
    scene.camera_geometry_read = true;
    scene.viewport_width = 1600;
    scene.viewport_height = 900;
    scene.camera_probe.values.view43 = {{1,0,0,0,1,0,0,0,1,0,0,0}};
    scene.camera_probe.values.fov_degrees = 90;
    scene.camera_probe.values.near_plane = 0.1f;
    scene.camera_probe.values.far_plane = 3000;
    scene.local_role_present = true;
    scene.local_role_observed = true;
    for (unsigned index = 0; index < 40; ++index) {
        sp::EntitySummary entity;
        entity.vtable_rva = sp::entity_vtables[2];
        entity.native_entity_type_known = true;
        entity.skeleton_present = true;
        entity.skeleton_checked = true;
        entity.position_checked = true;
        entity.character_controller_checked = true;
        entity.character_controller_present = true;
        entity.is_local_role = index == 0;
        entity.position = {static_cast<double>(index), .5, -10};
        entity.resource.status = ei::Status::resource_read;
        const bool actor = index % 2 != 0;
        entity.resource.observed_vtable_rva = actor ? ei::actor_vtable_rva : ei::skeleton_vtable_rva;
        entity.resource.source = actor ? "animation_graph" : "model_resource";
        entity.resource.path = "actors/body_" + std::to_string(index) + (actor ? ".graph" : ".skel");
        if (!ei::technical_resource(entity.resource.path)) return 1;
        scene.entities.push_back(entity);
    }
    std::cout << json_report(report);
}
