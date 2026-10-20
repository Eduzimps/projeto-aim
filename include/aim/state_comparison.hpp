#pragma once

#include "aim/calibration_frame.hpp"
#include <cstring>
#include <iomanip>
#include <locale>
#include <sstream>

namespace aim::state_comparison {
// End of the verified generic IEntity split-translation fields, not a guessed
// player structure size. No component, string or Python pointer is followed.
inline constexpr std::size_t prefix_bytes = 0x1b8;
inline constexpr std::uint64_t generic_vtable_rva = 0x04f2a180;
inline constexpr std::size_t max_changes = 64;
struct Sample {
    const char* status = "not_captured";
    std::uint64_t object = 0; // Never serialized.
    bool partial = true, skeleton = false, controller = false;
    std::array<unsigned char,prefix_bytes> data{};
};
inline bool usable_scene(const scene_profile::Result& scene) {
    return scene.scene_guards_matched && scene.consistency_recheck_passed && scene.local_role_present &&
        (scene.status == scene_profile::Status::candidate_collection ||
         scene.status == scene_profile::Status::node_limit_reached);
}
inline bool candidate(const scene_profile::EntitySummary& entity) {
    return entity.native_entity_type_known && entity.vtable_rva == generic_vtable_rva &&
        entity.skeleton_present && entity.character_controller_checked &&
        entity.character_controller_present && !entity.is_local_role && entity.position_checked &&
        entity.resource.observed_vtable_rva == entity_identity::actor_vtable_rva;
}

// Selection is explicit (the user aims at the feet); it is not an enemy filter.
inline std::uint64_t select(const scene_profile::Result& scene) {
    if (!usable_scene(scene)) return 0;
    double best = 240.0, second = 240.0;
    std::uint64_t object = 0;
    for (const auto& entity : scene.entities) {
        if (!candidate(entity)) continue;
        auto single = scene;
        single.entities = {entity};
        const auto frame = calibration::make_frame(single,scene.viewport_width,scene.viewport_height);
        if (frame.markers.empty()) continue;
        const auto pixel = frame.markers.front().pixel;
        const double distance = std::hypot(pixel.x-scene.viewport_width/2.0,
                                           pixel.y-scene.viewport_height/2.0);
        if (distance < best) { second = best; best = distance; object = entity.object_address; }
        else if (distance < second) second = distance;
    }
    return second-best >= 16.0 ? object : 0;
}

inline Sample capture(scene_profile::Result& scene, std::uint64_t module_base,
                      std::uint64_t object, const camera_profile::ReadBytes& reader) {
    Sample sample;
    sample.partial = !scene.collection_complete;
    if (!usable_scene(scene)) { sample.status = "scene_unavailable"; return sample; }
    if (!object) { sample.status = "selection_missing_or_ambiguous"; return sample; }
    const auto found = std::find_if(scene.entities.begin(),scene.entities.end(),
        [&](const auto& entity) { return entity.object_address == object; });
    if (found == scene.entities.end()) {
        sample.status = sample.partial ? "not_observed_in_partial_collection" : "not_observed_in_collection";
        return sample;
    }
    if (!found->native_entity_type_known || found->vtable_rva != generic_vtable_rva || found->is_local_role) {
        sample.status = "entity_type_changed"; return sample;
    }
    const auto read = [&](std::uint64_t address, void* destination, std::size_t bytes) {
        std::uint64_t checked = 0;
        if (!reader || !camera_profile::checked_address(address,0,bytes,checked) ||
            scene.read_count >= scene_profile::max_reads ||
            bytes > scene_profile::max_bytes-scene.read_bytes) return false;
        ++scene.read_count; scene.read_bytes += bytes;
        return reader(checked,destination,bytes);
    };
    std::uint64_t before = 0, after = 0, inside = 0;
    if (!read(object,&before,sizeof(before)) || before != module_base+generic_vtable_rva ||
        !read(object,sample.data.data(),sample.data.size()) ||
        !read(object,&after,sizeof(after))) {
        sample.data = {}; sample.status = "read_failed_or_type_changed"; return sample;
    }
    std::memcpy(&inside,sample.data.data(),sizeof(inside));
    if (before != after || inside != before) {
        sample.data = {}; sample.status = "type_changed_during_read"; return sample;
    }
    sample.status = "captured"; sample.object = object;
    sample.skeleton = found->skeleton_present; sample.controller = found->character_controller_present;
    return sample;
}
inline bool captured(const Sample& sample) { return std::strcmp(sample.status,"captured") == 0; }
inline bool pointer_like(std::uint64_t value) {
    return value >= 0x10000 && value <= 0x00007fffffffffffULL;
}
inline std::uint32_t word(const Sample& sample, std::size_t offset) {
    std::uint32_t value = 0; std::memcpy(&value,sample.data.data()+offset,sizeof(value)); return value;
}
// Pointer-shaped 8-byte slots are omitted in every phase, even if they later
// become null. This deliberately sacrifices some scalar observations.
inline bool excluded(const std::array<Sample,3>& samples, std::size_t offset) {
    if (offset < 8 || (offset >= 0x7c && offset < 0x88) || offset >= 0x1ac) return true;
    for (const auto& sample : samples) {
        std::uint64_t value = 0;
        std::memcpy(&value,sample.data.data()+(offset/8)*8,sizeof(value));
        if (pointer_like(value)) return true;
    }
    return false;
}
inline std::string json_report(const std::array<Sample,3>& samples) {
    std::ostringstream json; json.imbue(std::locale::classic());
    json << std::boolalpha << std::setprecision(9);
    const bool comparable = captured(samples[0]) && captured(samples[1]) && captured(samples[2]) &&
        samples[0].object == samples[1].object && samples[0].object == samples[2].object;
    json << "{\n\"format\":\"zyon-state-comparison-v1\",\n\"profile_sha256\":\""
         << camera_profile::file_sha256 << "\",\n\"labels_are_user_supplied\":true,\n"
         << "\"same_native_address_all_phases\":" << comparable << ",\n\"phases\":[";
    constexpr std::array<const char*,3> labels{{"alive","dead","respawned"}};
    for (std::size_t n=0;n<samples.size();++n) {
        const auto& sample = samples[n];
        if (n) json << ',';
        json << "{\"label\":\"" << labels[n] << "\",\"status\":\"" << sample.status
             << "\",\"partial_collection\":" << sample.partial << ",\"skeleton_present\":"
             << sample.skeleton << ",\"controller_present\":" << sample.controller << '}';
    }
    json << "],\n\"prefix_bytes\":" << prefix_bytes << ",\n\"changes\":[";
    std::size_t count=0, total=0;
    if (comparable) for (std::size_t offset=8;offset+4<=prefix_bytes;offset+=4) {
        if (excluded(samples,offset)) continue;
        const std::array<std::uint32_t,3> values{{word(samples[0],offset),word(samples[1],offset),word(samples[2],offset)}};
        if (values[0] == values[1] && values[1] == values[2]) continue;
        ++total; if (count >= max_changes) continue;
        if (count++) json << ',';
        json << "{\"offset\":" << offset << ",\"u32\":[";
        for (std::size_t n=0;n<3;++n) { if(n) json << ','; json << values[n]; }
        json << "],\"f32\":[";
        for (std::size_t n=0;n<3;++n) {
            float value = 0; std::memcpy(&value,&values[n],sizeof(value));
            if(n) json << ',';
            if (std::isfinite(value)) json << value; else json << "null";
        }
        json << "]}";
    }
    json << "],\n\"changes_truncated\":" << (total > count)
         << ",\n\"limitations\":\"User labels are not validated. Same address does not prove object lifetime. "
            "Live non-atomic generic native prefix only; pointer-shaped slots and known translation fields omitted. "
            "No health, death, player or team classification. Script/component health may be outside this prefix.\"\n}\n";
    return json.str();
}
} // namespace aim::state_comparison
