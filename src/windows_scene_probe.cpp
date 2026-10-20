#include "aim/windows_probe_support.hpp"
#include "aim/scene_profile.hpp"

#include <iostream>
#include <locale>
#include <map>

namespace cp = aim::camera_profile;
namespace sp = aim::scene_profile;
namespace wp = aim::windows_probe;
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
    std::size_t count = 0, skeleton_checked = 0, skeleton_present = 0;
    bool native_entity_type_known = false;
};

std::string json_report(const Report& report) {
    std::ostringstream json;
    json.imbue(std::locale::classic());
    json << std::boolalpha << std::setprecision(std::numeric_limits<float>::max_digits10);
    json << "{\n  \"format\":\"zyon-scene-candidate-v1\",\n"
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
         << "  \"limits\":{\"nodes\":" << sp::max_nodes << ",\"read_attempts\":" << sp::max_reads
         << ",\"requested_bytes\":" << sp::max_bytes
         << ",\"memory_read_start_window_ms\":" << memory_duration_ms << "},\n"
         << "  \"scene_attempted\":" << report.scene_attempted;
    if (report.scene_attempted) {
        const auto& scene = report.scene;
        json << ",\n  \"scene\":{\"stage\":\"" << scene.stage
             << "\",\"camera_status\":\"" << cp::status_name(scene.camera_probe.status)
             << "\",\"camera_stage\":\"" << scene.camera_probe.stage
             << "\",\"camera_vtable_rva\":" << scene.observed_camera_vtable_rva
             << ",\"scene_guards_matched\":" << scene.scene_guards_matched
             << ",\"consistency_recheck_passed\":" << scene.consistency_recheck_passed
             << ",\"collection_complete\":" << scene.collection_complete
             << ",\"read_attempts\":" << scene.read_count
             << ",\"requested_bytes\":" << scene.read_bytes
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
                 << ",\"skeleton_present\":" << type.skeleton_present << '}';
        }
        json << ']';
    }
    json << ",\n  \"limitations\":\"Default-level root area only; live non-atomic observation. "
         << "Partial results may be incomplete or stale. Native types and skeleton presence do not "
         << "identify players, enemies, health or bones. No ESP or input control.\"\n}\n";
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
