#pragma once

#include "aim/camera_profile.hpp"

#include <array>
#include <cstring>
#include <string>
#include <utility>

namespace aim::entity_identity {
namespace cp = camera_profile;

inline constexpr std::uint64_t skeleton_vtable_rva = 0x04fa22a0;
inline constexpr std::size_t max_resource_bytes = 192;

enum class Status {
    not_requested, resource_read, empty_resource, unsupported_skeleton_type,
    invalid_address, read_failed, invalid_resource_layout, resource_too_long,
    nontechnical_resource, snapshot_changed
};
inline const char* status_name(Status status) noexcept {
    switch (status) {
    case Status::not_requested: return "not_requested";
    case Status::resource_read: return "resource_read";
    case Status::empty_resource: return "empty_resource";
    case Status::unsupported_skeleton_type: return "unsupported_skeleton_type";
    case Status::invalid_address: return "invalid_address";
    case Status::read_failed: return "read_failed";
    case Status::invalid_resource_layout: return "invalid_resource_layout";
    case Status::resource_too_long: return "resource_too_long";
    case Status::nontechnical_resource: return "nontechnical_resource";
    case Status::snapshot_changed: return "snapshot_changed";
    }
    return "unknown";
}

struct Resource {
    Status status = Status::not_requested;
    std::uint64_t observed_vtable_rva = 0;
    std::string path;
};

// Only relative ASCII asset identifiers are emitted. This excludes absolute
// filesystem paths, quotes, control characters and parent traversal segments.
inline bool technical_resource(std::string& path) {
    if (path.empty()) return false;
    for (char& c : path) {
        if (c == '\\') c = '/';
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/')) return false;
    }
    if (path.front() == '/') return false;
    std::size_t start = 0;
    for (;;) {
        const auto end = path.find('/', start);
        if (path.substr(start, end == std::string::npos ? end : end - start) == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

// Build-specific SkeletonComponent.ResourcePath: member getter RVA0xcc6f70
// returns this+0x240, with zero this-adjustment. Its reflected string adapter
// RVA0x58b2a0 uses the MSVC string size/capacity/SSO layout below. RTTI requires
// the complete SkeletonComponent at displacement zero before reading the field.
// The caller supplies the shared bounded reader; no string is evaluated or opened.
inline Resource read_resource(std::uint64_t module_base, std::uint64_t skeleton,
                              const cp::ReadBytes& reader) {
    Resource result;
    const auto read_at = [&](std::uint64_t object, std::uint64_t offset,
                             void* destination, std::size_t bytes) {
        std::uint64_t location = 0;
        if (!reader || (object & 7) || !cp::checked_address(object, offset, bytes, location)) {
            result.status = Status::invalid_address; return false;
        }
        if (!reader(location, destination, bytes)) { result.status = Status::read_failed; return false; }
        return true;
    };
    std::uint64_t module_end = 0, vtable = 0;
    if (!cp::checked_address(module_base, cp::image_size - 1, 1, module_end)) {
        result.status = Status::invalid_address; return result;
    }
    if (!read_at(skeleton, 0, &vtable, sizeof(vtable))) return result;
    if (vtable >= module_base && vtable <= module_end) result.observed_vtable_rva = vtable - module_base;
    if (vtable != module_base + skeleton_vtable_rva) {
        result.status = Status::unsupported_skeleton_type; return result;
    }
    std::array<unsigned char, 32> header{}, rechecked{};
    if (!read_at(skeleton, 0x240, header.data(), header.size())) return result;
    std::uint64_t length = 0, capacity = 0, data = 0;
    std::memcpy(&data, header.data(), sizeof(data));
    std::memcpy(&length, header.data() + 16, sizeof(length));
    std::memcpy(&capacity, header.data() + 24, sizeof(capacity));
    if (capacity < 15 || capacity < length || (capacity == 15 && length > 15)) {
        result.status = Status::invalid_resource_layout; return result;
    }
    if (length > max_resource_bytes) { result.status = Status::resource_too_long; return result; }
    std::string path(static_cast<std::size_t>(length), '\0');
    if (length) {
        if (capacity == 15) std::memcpy(&path[0], header.data(), static_cast<std::size_t>(length));
        else {
            std::uint64_t location = 0;
            if (!cp::checked_address(data, 0, static_cast<std::size_t>(length), location)) {
                result.status = Status::invalid_address; return result;
            }
            // Character buffers need not have pointer alignment.
            if (!reader(location, &path[0], static_cast<std::size_t>(length))) {
                result.status = Status::read_failed; return result;
            }
        }
    }
    if (!read_at(skeleton, 0x240, rechecked.data(), rechecked.size())) return result;
    if (header != rechecked) { result.status = Status::snapshot_changed; return result; }
    if (!length) { result.status = Status::empty_resource; return result; }
    if (!technical_resource(path)) { result.status = Status::nontechnical_resource; return result; }
    result.path = std::move(path);
    result.status = Status::resource_read;
    return result;
}
} // namespace aim::entity_identity
