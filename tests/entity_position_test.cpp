#include "aim/entity_position.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace ep = aim::entity_position;
namespace cp = aim::camera_profile;
namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
constexpr std::uint64_t entity = 0x20000100000;
struct Memory {
    std::map<std::uint64_t, std::vector<unsigned char>> blocks;
    std::vector<std::pair<std::uint64_t, std::size_t>> requests;
    std::uint64_t fail_address = 0;
    template<class T> void put(std::uint64_t address, const T& value) {
        static_assert(std::is_trivially_copyable<T>::value, "Raw memory fixture");
        auto& bytes = blocks[address];
        bytes.resize(sizeof(T));
        std::memcpy(bytes.data(), &value, sizeof(T));
    }
    bool read(std::uint64_t address, void* output, std::size_t bytes) {
        requests.emplace_back(address, bytes);
        const auto found = blocks.find(address);
        if (address == fail_address || found == blocks.end() || found->second.size() != bytes) return false;
        std::memcpy(output, found->second.data(), bytes);
        return true;
    }
};
Memory fixture(std::array<float, 3> local = {{1.25f, -2.5f, 3.75f}},
               std::array<std::int32_t, 3> integer = {{100, -200, 300}}) {
    Memory memory;
    memory.put(entity + 0x7c, local);
    memory.put(entity + 0x1ac, integer);
    return memory;
}
ep::ReadResult run(Memory& memory, std::uint64_t address = entity) {
    return ep::read(address, [&](std::uint64_t location, void* output, std::size_t bytes) {
        return memory.read(location, output, bytes);
    });
}
}

int main() {
    auto memory = fixture();
    auto result = run(memory);
    check(result.status == ep::Status::candidate_position &&
          result.position.x == 101.25 && result.position.y == -202.5 &&
          result.position.z == 303.75, "signed integer translation is added to the local floats in double precision");
    check(memory.requests == std::vector<std::pair<std::uint64_t, std::size_t>>{
              {entity + 0x7c, 12}, {entity + 0x1ac, 12}},
          "successful decoding makes exactly the two specified narrow reads");
    constexpr auto maximum = std::numeric_limits<std::int32_t>::max();
    constexpr auto minimum = std::numeric_limits<std::int32_t>::min();
    memory = fixture({{1.25f,-2.5f,3.75f}}, {{maximum,maximum,maximum}});
    result = run(memory);
    check(result.status == ep::Status::candidate_position && result.position.x == 1.25 &&
          result.position.y == -2.5 && result.position.z == 3.75,
          "all three INT32_MAX components encode absent integer translation");
    memory = fixture({{1.25f,-2.5f,3.75f}}, {{maximum,0,minimum}});
    result = run(memory);
    check(result.status == ep::Status::candidate_position &&
          result.position.x == static_cast<double>(maximum) + 1.25 &&
          result.position.y == -2.5 && result.position.z == static_cast<double>(minimum) + 3.75,
          "mixed sentinel components retain all signed values instead of being zeroed");
    memory = fixture({{std::numeric_limits<float>::max(),-std::numeric_limits<float>::max(),0}},
                     {{maximum,minimum,-100}});
    result = run(memory);
    check(result.status == ep::Status::candidate_position && aim::finite(result.position),
          "large finite values are not arbitrarily clamped");

    memory = fixture();
    check(ep::read(entity, {}).status == ep::Status::invalid_reader,
          "an absent reader is reported without a call");
    check(run(memory, 0).status == ep::Status::null_pointer && memory.requests.empty(),
          "null object is rejected without a read");
    for (const std::uint64_t bad : {std::uint64_t{8}, entity + 1,
                                   cp::max_user_address + 1,
                                   (cp::max_user_address - 0x100) & ~std::uint64_t{7}}) {
        memory = fixture();
        check(run(memory, bad).status == ep::Status::invalid_address && memory.requests.empty(),
              "low, unaligned, noncanonical and wrapped ranges are rejected before reading");
    }
    memory = fixture(); memory.fail_address = entity + 0x7c;
    result = run(memory);
    check(result.status == ep::Status::read_failed &&
          std::string(result.stage) == "entity_position_local" && memory.requests.size() == 1,
          "local read failure stops before integer translation");
    memory = fixture(); memory.fail_address = entity + 0x1ac;
    result = run(memory);
    check(result.status == ep::Status::read_failed &&
          std::string(result.stage) == "entity_position_integer" && memory.requests.size() == 2,
          "integer read failure is attributed to the correct field");
    for (const float value : {std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity()}) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            std::array<float, 3> local{{1,2,3}};
            local[axis] = value;
            memory = fixture(local);
            result = run(memory);
            check(result.status == ep::Status::nonfinite_position && memory.requests.size() == 1,
                  "every nonfinite local component is rejected before a second read");
        }
    }
    if (failures) return EXIT_FAILURE;
    std::cout << "All entity position checks passed\n";
    return EXIT_SUCCESS;
}
