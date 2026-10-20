#pragma once

#include "aim/camera_profile.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aim::scene_profile {

namespace cp = camera_profile;
// These are build-specific native RTTI primary vtables, not player classes.
inline constexpr std::uint64_t camera_vtable_rva = 0x04e57558;
inline constexpr std::uint64_t world_vtable_rva = 0x04eca2e0;
inline constexpr std::array<std::uint64_t, 2> level_vtables{{0x04f2b1b0, 0x04f2b208}};
inline constexpr std::uint64_t area_vtable_rva = 0x04eca310;
inline constexpr std::array<std::uint64_t, 6> entity_vtables{{
    0x04e57558, 0x04e57df8, 0x04f2a180, 0x0559a1c0, 0x055cd200, 0x055cd750}};
inline constexpr std::size_t max_nodes = 256;
inline constexpr std::size_t max_reads = 2048;
inline constexpr std::size_t max_bytes = 65536;

enum class Status {
    candidate_collection, invalid_module, invalid_reader, camera_unavailable,
    null_pointer, invalid_address, read_failed, read_budget_exceeded,
    vtable_mismatch, camera_type_unrecognized, invalid_camera_geometry,
    entity_vtable_outside_rdata, list_cycle, list_links_inconsistent,
    node_limit_reached, snapshot_changed
};
inline const char* status_name(Status status) noexcept {
    switch (status) {
    case Status::candidate_collection: return "candidate_collection";
    case Status::invalid_module: return "invalid_module";
    case Status::invalid_reader: return "invalid_reader";
    case Status::camera_unavailable: return "camera_unavailable";
    case Status::null_pointer: return "null_pointer";
    case Status::invalid_address: return "invalid_address";
    case Status::read_failed: return "read_failed";
    case Status::read_budget_exceeded: return "read_budget_exceeded";
    case Status::vtable_mismatch: return "vtable_mismatch";
    case Status::camera_type_unrecognized: return "camera_type_unrecognized";
    case Status::invalid_camera_geometry: return "invalid_camera_geometry";
    case Status::entity_vtable_outside_rdata: return "entity_vtable_outside_rdata";
    case Status::list_cycle: return "list_cycle";
    case Status::list_links_inconsistent: return "list_links_inconsistent";
    case Status::node_limit_reached: return "node_limit_reached";
    case Status::snapshot_changed: return "snapshot_changed";
    }
    return "unknown";
}

struct EntitySummary {
    std::uint64_t vtable_rva = 0;
    bool native_entity_type_known = false;
    bool skeleton_checked = false;
    bool skeleton_present = false;
};
struct Result {
    Status status = Status::invalid_module;
    const char* stage = "module";
    cp::ProbeResult camera_probe;
    std::uint64_t camera_address = 0; // Internal only; do not serialize remote addresses.
    std::uint64_t observed_camera_vtable_rva = 0;
    std::array<float, 3> origin{};
    std::uint16_t viewport_width = 0, viewport_height = 0;
    bool camera_geometry_read = false;
    bool scene_guards_matched = false;
    bool consistency_recheck_passed = false;
    bool collection_complete = false;
    std::size_t read_count = 0, read_bytes = 0;
    std::vector<EntitySummary> entities;
};

// Observes only the active camera's World.DefaultLevel.RootArea.Entities.
// The list is live: rechecks detect some mutations but do not make an atomic snapshot.
inline Result probe(std::uint64_t module_base, std::uint64_t mapped_size,
                    const cp::ReadBytes& reader, std::size_t node_limit = max_nodes) {
    Result result;
    std::uint64_t address = 0;
    if (mapped_size != cp::image_size ||
        !cp::checked_address(module_base, 0, static_cast<std::size_t>(mapped_size), address)) return result;
    if (!reader) { result.status = Status::invalid_reader; return result; }
    const cp::ReadBytes bounded_reader = [&](std::uint64_t location, void* destination, std::size_t bytes) {
        if (result.read_count >= max_reads || bytes > max_bytes - result.read_bytes) {
            result.status = Status::read_budget_exceeded;
            return false;
        }
        ++result.read_count;
        result.read_bytes += bytes;
        return reader(location, destination, bytes);
    };
    result.camera_probe = cp::probe(module_base, mapped_size, bounded_reader);
    if (result.camera_probe.status != cp::ProbeStatus::candidate_sample) {
        if (result.status != Status::read_budget_exceeded) result.status = Status::camera_unavailable;
        result.stage = result.camera_probe.stage;
        return result;
    }
    const auto read_at = [&](std::uint64_t object, std::uint64_t offset,
                             void* destination, std::size_t bytes, const char* stage,
                             bool aligned = false) {
        result.stage = stage;
        if (!object) { result.status = Status::null_pointer; return false; }
        std::uint64_t location = 0;
        if ((aligned && (object & 7)) || !cp::checked_address(object, offset, bytes, location)) {
            result.status = Status::invalid_address; return false;
        }
        if (!bounded_reader(location, destination, bytes)) {
            if (result.status != Status::read_budget_exceeded) result.status = Status::read_failed;
            return false;
        }
        return true;
    };
    const auto read_pointer = [&](std::uint64_t object, std::uint64_t offset,
                                  std::uint64_t& pointer, const char* stage) {
        return read_at(object, offset, &pointer, sizeof(pointer), stage, true);
    };
    const auto exact_vtable = [&](std::uint64_t object, std::uint64_t expected, const char* stage) {
        std::uint64_t actual = 0;
        if (!read_pointer(object, 0, actual, stage)) return false;
        if (actual != module_base + expected) { result.status = Status::vtable_mismatch; return false; }
        return true;
    };
    std::array<std::uint64_t, 8> roots{}; // engine, gameplay, player, camera, world, level, area, sentinel
    if (!read_pointer(module_base, cp::engine_global_rva, roots[0], "engine_global") ||
        !exact_vtable(roots[0], cp::engine_vtable_rva, "engine_vtable") ||
        !read_pointer(roots[0], 0x58, roots[1], "engine_gameplay") ||
        !exact_vtable(roots[1], cp::gameplay_vtable_rva, "gameplay_vtable") ||
        !read_pointer(roots[1], 0x58, roots[2], "gameplay_player") ||
        !exact_vtable(roots[2], cp::player_vtable_rva, "player_vtable") ||
        !read_pointer(roots[2], 0xd88, roots[3], "player_camera")) return result;
    if (roots[3] != result.camera_probe.camera_address) {
        result.status = Status::snapshot_changed; result.stage = "camera_root_recheck"; return result;
    }
    std::uint64_t camera_vtable = 0;
    if (!read_pointer(roots[3], 0, camera_vtable, "camera_type")) return result;
    if (camera_vtable >= module_base && camera_vtable - module_base < mapped_size)
        result.observed_camera_vtable_rva = camera_vtable - module_base;
    if (camera_vtable != module_base + camera_vtable_rva) {
        result.status = Status::camera_type_unrecognized; return result;
    }
    result.camera_address = roots[3];
    std::array<std::uint16_t, 2> viewport{};
    if (!read_at(roots[3], 0x7c, result.origin.data(), sizeof(result.origin), "camera_origin") ||
        !read_at(roots[3], 0x408, viewport.data(), sizeof(viewport), "camera_viewport")) return result;
    if (!std::all_of(result.origin.begin(), result.origin.end(), [](float x) { return std::isfinite(x); }) ||
        viewport[0] == 0 || viewport[1] == 0) {
        result.status = Status::invalid_camera_geometry; return result;
    }
    result.viewport_width = viewport[0]; result.viewport_height = viewport[1];
    result.camera_geometry_read = true;
    if (!read_pointer(roots[3], 0x98, roots[4], "camera_world") ||
        !exact_vtable(roots[4], world_vtable_rva, "world_vtable") ||
        !read_pointer(roots[4], 0x148, roots[5], "world_default_level")) return result;
    std::uint64_t level_vtable = 0;
    if (!read_pointer(roots[5], 0, level_vtable, "level_vtable")) return result;
    if (std::none_of(level_vtables.begin(), level_vtables.end(),
                     [&](std::uint64_t rva) { return level_vtable == module_base + rva; })) {
        result.status = Status::vtable_mismatch; return result;
    }
    if (!read_pointer(roots[5], 0xf0, roots[6], "level_root_area") ||
        !exact_vtable(roots[6], area_vtable_rva, "area_vtable") ||
        !read_pointer(roots[6], 0xb8, roots[7], "area_entity_sentinel")) return result;
    result.scene_guards_matched = true;
    std::array<std::uint64_t, 2> sentinel_links{};
    if (!read_at(roots[7], 0, sentinel_links.data(), sizeof(sentinel_links), "sentinel_links", true)) return result;
    const auto limit = std::min(node_limit, max_nodes);
    std::array<std::uint64_t, max_nodes> visited{};
    std::uint64_t node = sentinel_links[0], previous = roots[7];
    while (node != roots[7]) {
        if (std::find(visited.begin(), visited.begin() + result.entities.size(), node) !=
            visited.begin() + result.entities.size()) {
            result.status = Status::list_cycle; result.stage = "list_next"; return result;
        }
        if (result.entities.size() >= limit) {
            result.status = Status::node_limit_reached; result.stage = "list_limit"; return result;
        }
        std::array<std::uint64_t, 3> record{};
        if (!read_at(node, 0, record.data(), sizeof(record), "entity_node", true)) return result;
        if (record[1] != previous) {
            result.status = Status::list_links_inconsistent; result.stage = "entity_node_previous"; return result;
        }
        std::uint64_t entity_vtable = 0;
        if (!read_pointer(record[2], 0, entity_vtable, "entity_vtable")) return result;
        if ((entity_vtable & 7) || entity_vtable < module_base + cp::rdata_begin_rva ||
            entity_vtable >= module_base + cp::rdata_end_rva) {
            result.status = Status::entity_vtable_outside_rdata; return result;
        }
        EntitySummary entity;
        entity.vtable_rva = entity_vtable - module_base;
        entity.native_entity_type_known = std::find(entity_vtables.begin(), entity_vtables.end(),
                                                   entity.vtable_rva) != entity_vtables.end();
        if (entity.native_entity_type_known) {
            // IEntity.Skeleton getter 0x95be70: tagged component index, not a raw pointer.
            std::uint8_t tag = 0;
            if (!read_at(record[2], 0xa1, &tag, sizeof(tag), "entity_skeleton_tag")) return result;
            if (tag & 0x80) {
                std::uint64_t components = 0, skeleton = 0;
                if (!read_pointer(record[2], 0x40, components, "entity_components") ||
                    !read_pointer(components, (tag & 0x7f) * sizeof(std::uint64_t), skeleton,
                                  "entity_skeleton_component")) return result;
                entity.skeleton_present = skeleton != 0;
            }
            entity.skeleton_checked = true;
        }
        std::array<std::uint64_t, 3> rechecked{};
        if (!read_at(node, 0, rechecked.data(), sizeof(rechecked), "entity_node_recheck", true)) return result;
        if (record != rechecked) { result.status = Status::snapshot_changed; return result; }
        visited[result.entities.size()] = node;
        result.entities.push_back(entity);
        previous = node; node = record[0];
    }
    if (sentinel_links[1] != previous) {
        result.status = Status::list_links_inconsistent; result.stage = "sentinel_previous"; return result;
    }
    const std::array<std::uint64_t, 8> owners{{module_base, roots[0], roots[1], roots[2],
                                              roots[3], roots[4], roots[5], roots[6]}};
    constexpr std::array<std::uint64_t, 8> offsets{{cp::engine_global_rva, 0x58, 0x58, 0xd88,
                                                  0x98, 0x148, 0xf0, 0xb8}};
    for (std::size_t n = 0; n < roots.size(); ++n) {
        std::uint64_t actual = 0;
        if (!read_pointer(owners[n], offsets[n], actual, "root_recheck")) return result;
        if (actual != roots[n]) { result.status = Status::snapshot_changed; return result; }
    }
    std::array<std::uint64_t, 2> final_links{};
    if (!read_at(roots[7], 0, final_links.data(), sizeof(final_links), "sentinel_recheck", true)) return result;
    if (final_links != sentinel_links) { result.status = Status::snapshot_changed; return result; }
    result.consistency_recheck_passed = true;
    result.collection_complete = true;
    result.stage = "candidate_root_area_collection";
    result.status = Status::candidate_collection;
    return result;
}

} // namespace aim::scene_profile
