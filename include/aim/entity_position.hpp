#pragma once

#include "aim/camera_profile.hpp"
#include "aim/core.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace aim::entity_position {

namespace cp = camera_profile;

enum class Status {
    candidate_position, invalid_reader, null_pointer, invalid_address,
    read_failed, nonfinite_position
};

inline const char* status_name(Status status) noexcept {
    switch (status) {
    case Status::candidate_position: return "candidate_position";
    case Status::invalid_reader: return "invalid_reader";
    case Status::null_pointer: return "null_pointer";
    case Status::invalid_address: return "invalid_address";
    case Status::read_failed: return "read_failed";
    case Status::nonfinite_position: return "nonfinite_position";
    }
    return "unknown";
}

struct ReadResult {
    Status status = Status::invalid_address;
    const char* stage = "entity";
    Vec3 position;
};

// Build-specific translation decoder for GetEntityPosition's helper at RVA
// 0x8977b0. The caller must first establish a supported IEntity base at offset
// zero and supply the shared bounded reader. This function reads no vtable,
// follows no pointers, and performs exactly two 12-byte reads on success.
// Separate live reads do not constitute an atomic position snapshot.
inline ReadResult read(std::uint64_t entity, const cp::ReadBytes& reader) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                  "The profile requires IEEE-754 binary32");
    ReadResult result;
    if (!reader) {
        result.status = Status::invalid_reader;
        result.stage = "reader";
        return result;
    }
    if (entity == 0) {
        result.status = Status::null_pointer;
        return result;
    }
    if (entity & 7) return result;
    std::uint64_t local_address = 0, integer_address = 0;
    result.stage = "entity_position_local";
    if (!cp::checked_address(entity, 0x7c, 3 * sizeof(float), local_address)) return result;
    result.stage = "entity_position_integer";
    if (!cp::checked_address(entity, 0x1ac, 3 * sizeof(std::int32_t), integer_address)) return result;

    std::array<float, 3> local{};
    result.stage = "entity_position_local";
    if (!reader(local_address, local.data(), sizeof(local))) {
        result.status = Status::read_failed;
        return result;
    }
    if (!std::all_of(local.begin(), local.end(), [](float value) { return std::isfinite(value); })) {
        result.status = Status::nonfinite_position;
        return result;
    }
    std::array<std::int32_t, 3> integer{};
    result.stage = "entity_position_integer";
    if (!reader(integer_address, integer.data(), sizeof(integer))) {
        result.status = Status::read_failed;
        return result;
    }
    // The inspected helper zeroes the integer translation only when all three
    // components equal INT32_MAX; a mixed sentinel is not partially zeroed.
    if (std::all_of(integer.begin(), integer.end(), [](std::int32_t value) {
            return value == std::numeric_limits<std::int32_t>::max();
        })) integer.fill(0);
    result.position = {
        static_cast<double>(local[0]) + static_cast<double>(integer[0]),
        static_cast<double>(local[1]) + static_cast<double>(integer[1]),
        static_cast<double>(local[2]) + static_cast<double>(integer[2])
    };
    if (!finite(result.position)) {
        result.status = Status::nonfinite_position;
        return result;
    }
    result.stage = "candidate_entity_position";
    result.status = Status::candidate_position;
    return result;
}

} // namespace aim::entity_position
