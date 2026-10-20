#include "aim/windows_probe_support.hpp"
#include "aim/scene_profile.hpp"
#include "aim/messiah_projection.hpp"

#include <iostream>
#include <locale>
#include <map>

namespace cp = aim::camera_profile;
namespace sp = aim::scene_profile;
namespace wp = aim::windows_probe;
namespace mp = aim::messiah_projection;
namespace ei = aim::entity_identity;
namespace {

constexpr ULONGLONG memory_duration_ms = 2000;
struct Report : wp::IdentityReport {
    bool scene_attempted = false;
    sp::Result scene;
};

Report collect() {
    Report report;
    wp::Session session;
    if (!wp::open_client(session, report)) return report;
    wp::MemoryReader memory{session.process.value, 0};
    const ULONGLONG start = GetTickCount64();
    const cp::ReadBytes reader = [&](std::uint64_t address, void* destination, std::size_t bytes) {
        if (GetTickCount64() - start >= memory_duration_ms) {
            memory.error = ERROR_TIMEOUT;
            return false;
        }
        return memory(address, destination, bytes);
    };
    report.scene_attempted = true;
    report.scene = sp::probe(session.base, session.mapped_size, reader);
    report.error = memory.error;
    report.status = sp::status_name(report.scene.status);
    return report;
}

struct TypeCount {
    std::size_t count = 0, skeleton_checked = 0, skeleton_present = 0, position_checked = 0;
    std::size_t controller_present = 0, local_roles = 0, actor_components = 0;
    bool native_entity_type_known = false;
};

std::string json_report(const Report& report) {
    std::ostringstream json;
    json.imbue(std::locale::classic());
    json << std::boolalpha << std::setprecision(std::numeric_limits<double>::max_digits10);
    json << "{\n  \"format\":\"zyon-scene-candidate-v4\",\n"
         << "  \"validation\":\"static_scene_path_requires_runtime_confirmation\",\n"
         << "  \"status\":\"" << report.status << "\",\n"
         << "  \"win32_error\":" << report.error << ",\n"
         << "  \"crypto_status\":" << report.crypto_error << ",\n"
         << "  \"profile\":{\"sha256\":\"" << cp::file_sha256
         << "\",\"file_size\":" << cp::file_size << ",\"image_size\":" << cp::image_size
         << ",\"machine\":\"AMD64\"},\n"
         << "  \"file_size_matched\":" << report.file_size_matched << ",\n"
         << "  \"file_sha256_matched\":" << report.file_sha256_matched << ",\n"
         << "  \"mapped_image_size_matched\":" << report.image_size_matched << ",\n"
         << "  \"observed_file_sha256\":\"" << report.observed_sha256 << "\",\n"
         << "  \"world_source\":\"gameplay_scenario_active_world\",\n"
         << "  \"limits\":{\"entity_nodes\":" << sp::max_nodes
         << ",\"levels\":" << sp::max_levels << ",\"areas\":" << sp::max_areas
         << ",\"read_attempts\":" << sp::max_reads
         << ",\"requested_bytes\":" << sp::max_bytes
         << ",\"resource_identifier_bytes\":" << ei::max_resource_bytes
         << ",\"resource_signatures\":32,\"position_samples\":16"
         << ",\"memory_read_start_window_ms\":" << memory_duration_ms << "},\n"
         << "  \"scene_attempted\":" << report.scene_attempted;
    if (report.scene_attempted) {
        const auto& scene = report.scene;
        json << ",\n  \"scene\":{\"stage\":\"" << scene.stage
             << "\",\"detail_stage\":\"" << scene.detail_stage
             << "\",\"camera_status\":\"" << cp::status_name(scene.camera_probe.status)
             << "\",\"camera_stage\":\"" << scene.camera_probe.stage
             << "\",\"camera_vtable_rva\":" << scene.observed_camera_vtable_rva
             << ",\"scene_guards_matched\":" << scene.scene_guards_matched
             << ",\"consistency_recheck_passed\":" << scene.consistency_recheck_passed
             << ",\"collection_complete\":" << scene.collection_complete
             << ",\"local_role_present\":" << scene.local_role_present
             << ",\"local_role_observed\":" << scene.local_role_observed
             << ",\"read_attempts\":" << scene.read_count
             << ",\"requested_bytes\":" << scene.read_bytes
             << ",\"levels_observed\":" << scene.levels_observed
             << ",\"areas_observed\":" << scene.areas_observed
             << ",\"entity_nodes_observed\":" << scene.entity_nodes_observed
             << ",\"objects_observed\":" << scene.entities.size() << "},\n"
             << "  \"camera_geometry_read\":" << scene.camera_geometry_read;
        if (scene.camera_geometry_read) {
            const auto& camera = scene.camera_probe.values;
            json << ",\n  \"camera\":{\"origin\":[" << scene.origin[0] << ',' << scene.origin[1]
                 << ',' << scene.origin[2] << "],\"viewport_width\":" << scene.viewport_width
                 << ",\"viewport_height\":" << scene.viewport_height << ",\"view43\":[";
            for (std::size_t i = 0; i < camera.view43.size(); ++i) {
                if (i) json << ',';
                json << camera.view43[i];
            }
            json << "],\"vertical_fov_degrees\":" << camera.fov_degrees
                 << ",\"near_plane\":" << camera.near_plane
                 << ",\"far_plane\":" << camera.far_plane << '}';
        }
        std::map<std::uint64_t, TypeCount> types;
        for (const auto& entity : scene.entities) {
            auto& type = types[entity.vtable_rva];
            ++type.count;
            type.native_entity_type_known = entity.native_entity_type_known;
            type.skeleton_checked += entity.skeleton_checked ? 1 : 0;
            type.skeleton_present += entity.skeleton_present ? 1 : 0;
            type.position_checked += entity.position_checked ? 1 : 0;
            type.controller_present += entity.character_controller_present ? 1 : 0;
            type.local_roles += entity.is_local_role ? 1 : 0;
            type.actor_components += entity.resource.observed_vtable_rva == ei::actor_vtable_rva ? 1 : 0;
        }
        json << ",\n  \"observed_native_types\":[";
        bool first = true;
        for (const auto& entry : types) {
            if (!first) json << ',';
            first = false;
            const auto& type = entry.second;
            json << "{\"vtable_rva\":" << entry.first << ",\"count\":" << type.count
                 << ",\"native_entity_type_known\":" << type.native_entity_type_known
                 << ",\"skeleton_checked\":" << type.skeleton_checked
                 << ",\"skeleton_present\":" << type.skeleton_present
                 << ",\"position_checked\":" << type.position_checked
                 << ",\"character_controller_present\":" << type.controller_present
                 << ",\"local_roles\":" << type.local_roles
                 << ",\"actor_components\":" << type.actor_components << '}';
        }
        json << ']';
        mp::Camera camera;
        std::copy_n(scene.camera_probe.values.view43.begin(), 9, camera.rotation.begin());
        camera.origin = {scene.origin[0], scene.origin[1], scene.origin[2]};
        camera.viewport = {static_cast<double>(scene.viewport_width), static_cast<double>(scene.viewport_height)};
        camera.vertical_fov_degrees = scene.camera_probe.values.fov_degrees;
        json << ",\n  \"position_samples\":[";
        std::size_t samples = 0;
        // Prioritize nonlocal skeletons with a movement controller. This is a
        // technical signature, not a player/enemy classification.
        for (unsigned priority = 0; priority < 4; ++priority) {
            for (const auto& entity : scene.entities) {
                const unsigned entity_priority = !entity.skeleton_present ? 3 :
                    (entity.is_local_role ? 2 : (entity.character_controller_present ? 0 : 1));
                if (!entity.position_checked || entity_priority != priority || samples >= 16) continue;
                if (samples) json << ',';
                const auto projected = mp::project(entity.position, camera);
                json << "{\"sample\":" << ++samples << ",\"vtable_rva\":" << entity.vtable_rva
                     << ",\"skeleton_present\":" << entity.skeleton_present
                     << ",\"is_local_role\":" << entity.is_local_role
                     << ",\"actor_component_present\":" << (entity.resource.observed_vtable_rva == ei::actor_vtable_rva)
                     << ",\"character_controller_present\":" << entity.character_controller_present
                     << ",\"resource_status\":\"" << ei::status_name(entity.resource.status)
                     << "\",\"resource_source\":\"" << entity.resource.source << '"';
                if (entity.resource.status == ei::Status::resource_read)
                    json << ",\"resource_identifier\":\"" << entity.resource.path << '"';
                json
                     << ",\"world_position\":[" << entity.position.x << ',' << entity.position.y
                     << ',' << entity.position.z << "],\"projection_available\":" << projected.has_value();
                if (projected) {
                    json << ",\"screen_pixel\":[" << projected->pixel.x << ',' << projected->pixel.y
                         << "],\"depth\":" << projected->depth << ",\"in_viewport\":" << projected->in_viewport;
                }
                json << '}';
            }
        }
        json << ']';
        struct ResourceCount { std::size_t count = 0, controllers = 0, local_roles = 0; };
        std::map<std::pair<std::string, std::string>, ResourceCount> resources;
        std::map<std::uint64_t, std::size_t> skeleton_types;
        for (const auto& entity : scene.entities) {
            if (entity.resource.observed_vtable_rva) ++skeleton_types[entity.resource.observed_vtable_rva];
            if (entity.resource.status != ei::Status::resource_read) continue;
            auto& resource = resources[{entity.resource.source, entity.resource.path}];
            ++resource.count;
            resource.controllers += entity.character_controller_present ? 1 : 0;
            resource.local_roles += entity.is_local_role ? 1 : 0;
        }
        json << ",\n  \"resource_signatures_truncated\":" << (resources.size() > 32)
             << ",\n  \"resource_signatures\":[";
        std::size_t resource_count = 0;
        for (const auto& entry : resources) {
            if (resource_count >= 32) break;
            if (resource_count++) json << ',';
            // Only validated relative ASCII asset identifiers reach this map.
            json << "{\"resource_source\":\"" << entry.first.first
                 << "\",\"resource_identifier\":\"" << entry.first.second << "\",\"count\":" << entry.second.count
                 << ",\"character_controller_present\":" << entry.second.controllers
                 << ",\"local_roles\":" << entry.second.local_roles << '}';
        }
        json << "],\n  \"observed_skeleton_types\":[";
        bool first_skeleton = true;
        for (const auto& entry : skeleton_types) {
            if (!first_skeleton) json << ',';
            first_skeleton = false;
            json << "{\"vtable_rva\":" << entry.first << ",\"count\":" << entry.second << '}';
        }
        json << ']';
    }
    json << ",\n  \"limitations\":\"Default and active levels, root and immediate subareas only; live non-atomic observation. "
         << "Partial results may be incomplete or stale. Native types and skeleton presence do not "
         << "identify players, enemies, health or bones. Local-role references and technical model/controller signatures are not team/health data. "
         << "Position and projection candidates need live alignment verification. "
         << "No ESP or input control.\"\n}\n";
    return json.str();
}

} // namespace

int main() {
    Report report;
    try { report = collect(); } catch (...) { report.status = "internal_error"; }
    const std::string json = json_report(report);
    DWORD save_error = 0;
    const bool saved = wp::save_report(json, L"BloodStrike-scene", save_error);
    std::cout << json;
    if (!saved) {
        std::cerr << "Desktop report could not be saved. Win32 error: " << save_error << '\n';
        return 2;
    }
    return std::string(report.status) == "candidate_collection" ? 0 : 1;
}
