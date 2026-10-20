#pragma once

#include "aim/camera_profile.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aim::scene_collections {

namespace cp = camera_profile;
inline constexpr std::size_t max_nodes = 256;

enum class Status {
    candidate_collection, invalid_reader, invalid_collection, null_pointer,
    invalid_address, read_failed, list_cycle, list_links_inconsistent,
    limit_reached, snapshot_changed
};

inline const char* status_name(Status status) noexcept {
    switch (status) {
    case Status::candidate_collection: return "candidate_collection";
    case Status::invalid_reader: return "invalid_reader";
    case Status::invalid_collection: return "invalid_collection";
    case Status::null_pointer: return "null_pointer";
    case Status::invalid_address: return "invalid_address";
    case Status::read_failed: return "read_failed";
    case Status::list_cycle: return "list_cycle";
    case Status::list_links_inconsistent: return "list_links_inconsistent";
    case Status::limit_reached: return "limit_reached";
    case Status::snapshot_changed: return "snapshot_changed";
    }
    return "unknown";
}

// All remote pointers are INTERNAL. Serialize counts/statuses, never addresses.
struct Collection {
    Status status = Status::invalid_collection;
    const char* stage = "collection";
    std::vector<std::uint64_t> values;
    bool complete = false;
    bool consistency_recheck_passed = false;
    bool anchors_available = false;
    std::uint64_t owner = 0;
    std::uint64_t sentinel_field_offset = 0;
    std::uint64_t value_offset = 0;
    std::uint64_t sentinel = 0;
    std::array<std::uint64_t, 2> initial_links{};
};

namespace detail {
inline bool read_at(Collection& collection, const cp::ReadBytes& reader,
                    std::uint64_t object, std::uint64_t offset,
                    void* destination, std::size_t bytes, const char* stage) {
    collection.stage = stage;
    if (!reader) { collection.status = Status::invalid_reader; return false; }
    if (!object) { collection.status = Status::null_pointer; return false; }
    std::uint64_t location = 0;
    if ((object & 7) || !cp::checked_address(object, offset, bytes, location)) {
        collection.status = Status::invalid_address; return false;
    }
    if (!reader(location, destination, bytes)) {
        collection.status = Status::read_failed; return false;
    }
    return true;
}
} // namespace detail

// Rechecks the owning field and sentinel links, including after other scene
// work. This detects some mutations; it does not provide an atomic snapshot.
inline bool stable(Collection& collection, const cp::ReadBytes& reader) {
    collection.consistency_recheck_passed = false;
    if (!collection.anchors_available ||
        (collection.status != Status::candidate_collection &&
         collection.status != Status::limit_reached)) {
        collection.complete = false;
        return false;
    }
    std::uint64_t sentinel = 0;
    std::array<std::uint64_t, 2> links{};
    if (!detail::read_at(collection, reader, collection.owner,
                         collection.sentinel_field_offset, &sentinel,
                         sizeof(sentinel), "collection_owner_recheck")) {
        collection.complete = false;
        return false;
    }
    if (sentinel != collection.sentinel) {
        collection.status = Status::snapshot_changed;
        collection.complete = false;
        return false;
    }
    if (!detail::read_at(collection, reader, sentinel, 0, links.data(),
                         sizeof(links), "collection_sentinel_recheck")) {
        collection.complete = false;
        return false;
    }
    if (links != collection.initial_links) {
        collection.status = Status::snapshot_changed;
        collection.complete = false;
        return false;
    }
    collection.consistency_recheck_passed = true;
    return true;
}

// Reflection iterators and constructors establish circular linked collections.
// Only links and value pointers are read, never intervening map keys/strings.
// The caller supplies a reader with its aggregate byte/read/time budget.
inline Collection enumerate(std::uint64_t owner, std::uint64_t sentinel_field_offset,
                            std::uint64_t value_offset, const cp::ReadBytes& reader,
                            std::size_t limit = max_nodes) {
    Collection result;
    result.owner = owner;
    result.sentinel_field_offset = sentinel_field_offset;
    result.value_offset = value_offset;
    if (value_offset != 0x10 && value_offset != 0x30) return result;
    if (!detail::read_at(result, reader, owner, sentinel_field_offset, &result.sentinel,
                         sizeof(result.sentinel), "collection_sentinel") ||
        !detail::read_at(result, reader, result.sentinel, 0, result.initial_links.data(),
                         sizeof(result.initial_links), "collection_links")) return result;
    result.anchors_available = true;
    const auto bounded_limit = std::min(limit, max_nodes);
    std::array<std::uint64_t, max_nodes> visited{};
    std::uint64_t node = result.initial_links[0], previous = result.sentinel;
    while (node != result.sentinel) {
        const auto end = visited.begin() + result.values.size();
        if (std::find(visited.begin(), end, node) != end) {
            result.status = Status::list_cycle; result.stage = "collection_next"; return result;
        }
        if (result.values.size() >= bounded_limit) {
            result.status = Status::limit_reached;
            stable(result, reader);
            result.stage = result.status == Status::limit_reached ? "collection_limit" : result.stage;
            return result;
        }
        std::array<std::uint64_t, 2> links{}, checked_links{};
        std::uint64_t value = 0, checked_value = 0;
        if (!detail::read_at(result, reader, node, 0, links.data(), sizeof(links), "collection_node") ||
            !detail::read_at(result, reader, node, value_offset, &value, sizeof(value),
                             "collection_value")) return result;
        if (links[1] != previous) {
            result.status = Status::list_links_inconsistent;
            result.stage = "collection_node_previous";
            return result;
        }
        std::uint64_t address = 0;
        if (!value) { result.status = Status::null_pointer; return result; }
        if ((value & 7) || !cp::checked_address(value, 0, sizeof(std::uint64_t), address)) {
            result.status = Status::invalid_address; return result;
        }
        if (!detail::read_at(result, reader, node, 0, checked_links.data(), sizeof(checked_links),
                             "collection_node_recheck") ||
            !detail::read_at(result, reader, node, value_offset, &checked_value,
                             sizeof(checked_value), "collection_value_recheck")) return result;
        if (links != checked_links || value != checked_value) {
            result.status = Status::snapshot_changed; return result;
        }
        visited[result.values.size()] = node;
        result.values.push_back(value);
        previous = node;
        node = links[0];
    }
    if (result.initial_links[1] != previous) {
        result.status = Status::list_links_inconsistent;
        result.stage = "collection_sentinel_previous";
        return result;
    }
    result.status = Status::candidate_collection;
    result.complete = true;
    if (stable(result, reader)) result.stage = "candidate_collection";
    return result;
}

inline Collection active_levels(std::uint64_t world, const cp::ReadBytes& reader,
                                std::size_t limit = max_nodes) {
    return enumerate(world, 0x160, 0x10, reader, limit);
}
inline Collection levels(std::uint64_t world, const cp::ReadBytes& reader,
                         std::size_t limit = max_nodes) {
    return enumerate(world, 0x108, 0x30, reader, limit);
}
inline Collection sub_areas(std::uint64_t level, const cp::ReadBytes& reader,
                            std::size_t limit = max_nodes) {
    return enumerate(level, 0x100, 0x30, reader, limit);
}
inline Collection entities(std::uint64_t area, const cp::ReadBytes& reader,
                           std::size_t limit = max_nodes) {
    return enumerate(area, 0xb8, 0x10, reader, limit);
}

} // namespace aim::scene_collections
