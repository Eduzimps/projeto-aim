#pragma once

#include "aim/camera_profile.hpp"
#include "aim/scene_collections.hpp"
#include "aim/entity_position.hpp"
#include "aim/entity_identity.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <utility>

namespace aim::scene_profile {

namespace cp = camera_profile;
namespace sc = scene_collections;
namespace ep = entity_position;
namespace ei = entity_identity;
// These are build-specific native RTTI primary vtables, not player classes.
inline constexpr std::uint64_t camera_vtable_rva = 0x04e57558;
inline constexpr std::uint64_t scenario_vtable_rva = 0x04fa1ce8;
inline constexpr std::uint64_t world_vtable_rva = 0x04eca2e0;
inline constexpr std::array<std::uint64_t, 2> level_vtables{{0x04f2b1b0, 0x04f2b208}};
inline constexpr std::uint64_t area_vtable_rva = 0x04eca310;
inline constexpr std::array<std::uint64_t, 6> entity_vtables{{
    0x04e57558, 0x04e57df8, 0x04f2a180, 0x0559a1c0, 0x055cd200, 0x055cd750}};
inline constexpr std::size_t max_nodes = 256;
inline constexpr std::size_t max_levels = 16;
inline constexpr std::size_t max_areas = 32;
inline constexpr std::size_t max_reads = 4096;
inline constexpr std::size_t max_bytes = 65536;

enum class Status {
    candidate_collection, invalid_module, invalid_reader, camera_unavailable,
    null_pointer, invalid_address, read_failed, read_budget_exceeded,
    vtable_mismatch, camera_type_unrecognized, invalid_camera_geometry,
    entity_vtable_outside_rdata, list_cycle, list_links_inconsistent,
    node_limit_reached, snapshot_changed, invalid_entity_position
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
    case Status::invalid_entity_position: return "invalid_entity_position";
    }
    return "unknown";
}

struct EntitySummary {
    std::uint64_t vtable_rva = 0;
    bool native_entity_type_known = false;
    bool skeleton_checked = false;
    bool skeleton_present = false;
    bool position_checked = false;
    bool character_controller_checked = false;
    bool character_controller_present = false;
    bool is_local_role = false;
    ei::Resource resource;
    Vec3 position;
};
struct Result {
    Status status = Status::invalid_module;
    const char* stage = "module";
    const char* detail_stage = "none";
    cp::ProbeResult camera_probe;
    std::uint64_t camera_address = 0; // Internal only; do not serialize remote addresses.
    std::uint64_t observed_camera_vtable_rva = 0;
    std::array<float, 3> origin{};
    std::uint16_t viewport_width = 0, viewport_height = 0;
    bool camera_geometry_read = false;
    bool scene_guards_matched = false;
    bool consistency_recheck_passed = false;
    bool collection_complete = false;
    bool local_role_present = false;
    bool local_role_observed = false;
    std::size_t read_count = 0, read_bytes = 0;
    std::size_t levels_observed = 0, areas_observed = 0;
    std::size_t entity_nodes_observed = 0;
    std::vector<EntitySummary> entities;
};

// Observes ClientScenario.ActiveWorld, its default/active levels, and each
// level's root and immediate subareas. The camera may legitimately be detached.
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
    // The world belongs to the active scenario, not necessarily to the camera.
    std::array<std::uint64_t, 6> roots{}; // engine, gameplay, player, camera, scenario, world
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
    if (!read_pointer(roots[1], 0x60, roots[4], "gameplay_scenario") ||
        !exact_vtable(roots[4], scenario_vtable_rva, "scenario_vtable") ||
        !read_pointer(roots[4], 0xd60, roots[5], "scenario_active_world") ||
        !exact_vtable(roots[5], world_vtable_rva, "world_vtable")) return result;
    result.scene_guards_matched = true;

    struct Anchor { std::uint64_t owner, offset, value; };
    std::vector<Anchor> anchors;
    const auto anchor_pointer = [&](std::uint64_t owner, std::uint64_t offset,
                                    std::uint64_t& value, const char* stage) {
        if (!read_pointer(owner, offset, value, stage)) return false;
        anchors.push_back({owner, offset, value});
        return true;
    };
    const auto accept_collection = [&](const sc::Collection& collection, const char* stage,
                                       bool allow_limit = false) {
        if (collection.status == sc::Status::candidate_collection && collection.complete) return true;
        if (allow_limit && collection.status == sc::Status::limit_reached) return true;
        result.stage = stage;
        result.detail_stage = collection.stage;
        if (result.status == Status::read_budget_exceeded) return false;
        switch (collection.status) {
        case sc::Status::invalid_reader: result.status = Status::invalid_reader; break;
        case sc::Status::null_pointer: result.status = Status::null_pointer; break;
        case sc::Status::invalid_collection:
        case sc::Status::invalid_address: result.status = Status::invalid_address; break;
        case sc::Status::list_cycle: result.status = Status::list_cycle; break;
        case sc::Status::list_links_inconsistent: result.status = Status::list_links_inconsistent; break;
        case sc::Status::limit_reached: result.status = Status::node_limit_reached; break;
        case sc::Status::snapshot_changed: result.status = Status::snapshot_changed; break;
        default: result.status = Status::read_failed; break;
        }
        return false;
    };
    std::vector<sc::Collection> collections;
    std::vector<std::uint64_t> levels, areas, observed_entities;
    std::uint64_t local_role = 0;
    if (!anchor_pointer(roots[2], 0xdd8, local_role, "local_role")) return result;
    result.local_role_present = local_role != 0;
    const auto add_unique = [&](std::vector<std::uint64_t>& values, std::uint64_t value,
                                std::size_t limit, const char* stage) {
        if (!value || std::find(values.begin(), values.end(), value) != values.end()) return true;
        if (values.size() >= limit) {
            result.status = Status::node_limit_reached; result.stage = stage; return false;
        }
        values.push_back(value);
        return true;
    };
    std::uint64_t default_level = 0;
    if (!anchor_pointer(roots[5], 0x148, default_level, "world_default_level") ||
        !add_unique(levels, default_level, max_levels, "level_limit")) return result;
    auto active = sc::active_levels(roots[5], bounded_reader, max_levels);
    if (!accept_collection(active, "active_levels")) return result;
    for (auto level : active.values)
        if (!add_unique(levels, level, max_levels, "level_limit")) return result;
    collections.push_back(std::move(active));
    for (auto level : levels) {
        std::uint64_t vtable = 0, root_area = 0;
        if (!read_pointer(level, 0, vtable, "level_vtable")) return result;
        if (std::none_of(level_vtables.begin(), level_vtables.end(),
                         [&](std::uint64_t rva) { return vtable == module_base + rva; })) {
            result.status = Status::vtable_mismatch; return result;
        }
        ++result.levels_observed;
        if (!anchor_pointer(level, 0xf0, root_area, "level_root_area") ||
            !add_unique(areas, root_area, max_areas, "area_limit")) return result;
        auto subareas = sc::sub_areas(level, bounded_reader, max_areas);
        if (!accept_collection(subareas, "level_sub_areas")) return result;
        for (auto area : subareas.values)
            if (!add_unique(areas, area, max_areas, "area_limit")) return result;
        collections.push_back(std::move(subareas));
    }
    const auto limit = std::min(node_limit, max_nodes);
    bool truncated = false;
    for (auto area : areas) {
        if (!exact_vtable(area, area_vtable_rva, "area_vtable")) return result;
        ++result.areas_observed;
        auto objects = sc::entities(area, bounded_reader, limit - result.entity_nodes_observed);
        result.entity_nodes_observed += objects.values.size();
        if (!accept_collection(objects, "area_entities", true)) return result;
        for (auto object : objects.values) {
            if (std::find(observed_entities.begin(), observed_entities.end(), object) !=
                observed_entities.end()) continue;
            if (result.entities.size() >= limit) {
                result.status = Status::node_limit_reached; result.stage = "entity_limit"; return result;
            }
            std::uint64_t entity_vtable = 0;
            if (!read_pointer(object, 0, entity_vtable, "entity_vtable")) return result;
            if ((entity_vtable & 7) || entity_vtable < module_base + cp::rdata_begin_rva ||
                entity_vtable >= module_base + cp::rdata_end_rva) {
                result.status = Status::entity_vtable_outside_rdata; return result;
            }
            EntitySummary entity;
            entity.is_local_role = object == local_role;
            result.local_role_observed = result.local_role_observed || entity.is_local_role;
            entity.vtable_rva = entity_vtable - module_base;
            entity.native_entity_type_known = std::find(entity_vtables.begin(), entity_vtables.end(),
                                                       entity.vtable_rva) != entity_vtables.end();
            if (entity.native_entity_type_known) {
                std::uint8_t tag = 0;
                std::uint64_t components = 0, skeleton = 0;
                if (!read_at(object, 0xa1, &tag, sizeof(tag), "entity_skeleton_tag")) return result;
                if (tag & 0x80) {
                    if (!read_pointer(object, 0x40, components, "entity_components") ||
                        !read_pointer(components, (tag & 0x7f) * sizeof(std::uint64_t), skeleton,
                                      "entity_skeleton_component")) return result;
                    entity.skeleton_present = skeleton != 0;
                }
                entity.skeleton_checked = true;
                std::uint8_t controller_tag = 0;
                if (!read_at(object, 0xa7, &controller_tag, sizeof(controller_tag), "entity_controller_tag")) return result;
                if (controller_tag & 0x80) {
                    std::uint64_t controller = 0;
                    if ((!components && !read_pointer(object, 0x40, components, "entity_components")) ||
                        !read_pointer(components, (controller_tag & 0x7f) * sizeof(std::uint64_t), controller,
                                      "entity_controller_component")) return result;
                    entity.character_controller_present = controller != 0;
                }
                entity.character_controller_checked = true;
                if (skeleton) {
                    entity.resource = ei::read_resource(module_base, skeleton, bounded_reader);
                    if (entity.resource.status == ei::Status::read_failed ||
                        entity.resource.status == ei::Status::invalid_address ||
                        entity.resource.status == ei::Status::snapshot_changed) {
                        result.stage = "skeleton_resource";
                        if (result.status != Status::read_budget_exceeded)
                            result.status = entity.resource.status == ei::Status::snapshot_changed ?
                                Status::snapshot_changed : (entity.resource.status == ei::Status::invalid_address ?
                                    Status::invalid_address : Status::read_failed);
                        return result;
                    }
                }
                const auto position = ep::read(object, bounded_reader);
                if (position.status != ep::Status::candidate_position) {
                    result.stage = position.stage;
                    if (result.status != Status::read_budget_exceeded)
                        result.status = position.status == ep::Status::read_failed ?
                            Status::read_failed : Status::invalid_entity_position;
                    return result;
                }
                entity.position = position.position;
                entity.position_checked = true;
            }
            observed_entities.push_back(object);
            result.entities.push_back(entity);
        }
        if (objects.status == sc::Status::limit_reached) {
            truncated = true;
            collections.push_back(std::move(objects));
            break;
        }
        if (!accept_collection(objects, "area_entities")) return result;
        collections.push_back(std::move(objects));
    }
    const std::array<std::uint64_t, 6> owners{{module_base, roots[0], roots[1], roots[2], roots[1], roots[4]}};
    constexpr std::array<std::uint64_t, 6> offsets{{cp::engine_global_rva, 0x58, 0x58, 0xd88, 0x60, 0xd60}};
    for (std::size_t n = 0; n < roots.size(); ++n) anchors.push_back({owners[n], offsets[n], roots[n]});
    for (const auto& anchor : anchors) {
        std::uint64_t actual = 0;
        if (!read_pointer(anchor.owner, anchor.offset, actual, "root_recheck")) return result;
        if (actual != anchor.value) { result.status = Status::snapshot_changed; return result; }
    }
    for (auto& collection : collections) {
        if (!sc::stable(collection, bounded_reader)) {
            accept_collection(collection, "collection_final_recheck"); return result;
        }
    }

    result.consistency_recheck_passed = true;
    result.collection_complete = !truncated;
    result.stage = truncated ? "candidate_scene_prefix" : "candidate_scene_collection";
    result.detail_stage = truncated ? "collection_limit" : "none";
    result.status = truncated ? Status::node_limit_reached : Status::candidate_collection;
    return result;
}

} // namespace aim::scene_profile
