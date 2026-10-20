#include "aim/scene_collections.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>

namespace sc = aim::scene_collections;
namespace cp = aim::camera_profile;
namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
constexpr std::uint64_t owner = 0x20000000000;
constexpr std::uint64_t sentinel = owner + 0x10000;
constexpr std::uint64_t nodes = owner + 0x20000;
constexpr std::uint64_t objects = owner + 0x100000;
struct Memory {
    std::map<std::uint64_t, std::vector<unsigned char>> blocks;
    std::map<std::uint64_t, std::size_t> counts;
    std::uint64_t fail = 0, change = 0;
    std::size_t reads = 0, bytes = 0, change_on_read = 2;
    template<class T> void put(std::uint64_t address, const T& value) {
        auto& block = blocks[address]; block.resize(sizeof(value));
        std::memcpy(block.data(), &value, sizeof(value));
    }
    bool read(std::uint64_t address, void* destination, std::size_t size) {
        ++reads; bytes += size;
        const auto count = ++counts[address];
        const auto found = blocks.find(address);
        if (address == fail || found == blocks.end() || found->second.size() != size) return false;
        std::memcpy(destination, found->second.data(), size);
        if (address == change && count >= change_on_read)
            static_cast<unsigned char*>(destination)[0] ^= 0x10;
        return true;
    }
    cp::ReadBytes reader() {
        return [this](std::uint64_t address, void* destination, std::size_t size) {
            return read(address, destination, size);
        };
    }
};
Memory fixture(std::uint64_t field = 0x160, std::uint64_t value_offset = 0x10,
               std::size_t count = 2) {
    Memory memory;
    memory.put(owner + field, sentinel);
    memory.put(sentinel, std::array<std::uint64_t, 2>{{count ? nodes : sentinel,
        count ? nodes + (count - 1) * 0x100 : sentinel}});
    for (std::size_t i = 0; i < count; ++i) {
        const auto node = nodes + i * 0x100;
        memory.put(node, std::array<std::uint64_t, 2>{{
            i + 1 == count ? sentinel : node + 0x100, i ? node - 0x100 : sentinel}});
        memory.put(node + value_offset, objects + i * 0x100);
    }
    return memory;
}
}

int main() {
    auto memory = fixture();
    auto result = sc::active_levels(owner, memory.reader());
    check(result.status == sc::Status::candidate_collection && result.complete &&
          result.consistency_recheck_passed && result.values.size() == 2 &&
          result.values[1] == objects + 0x100, "active levels follow the proven circular pointer list");
    check(memory.reads == 12 && memory.bytes == 144,
          "only owner/sentinel and two narrow fields per node are read and rechecked");
    memory = fixture(0x108, 0x30);
    result = sc::levels(owner, memory.reader());
    check(result.complete && result.values[0] == objects && memory.counts[nodes + 0x10] == 0 &&
          memory.counts[nodes + 0x18] == 0 && memory.counts[nodes + 0x20] == 0,
          "world levels read payload at +30 without map key bytes");
    memory = fixture(0x100, 0x30);
    check(sc::sub_areas(owner, memory.reader()).complete,
          "subareas use the level+100 sentinel and +30 payload");
    memory = fixture(0xb8, 0x10);
    check(sc::entities(owner, memory.reader()).complete,
          "area entities use the area+b8 sentinel and +10 payload");
    memory = fixture(0x160, 0x10, 0);
    result = sc::active_levels(owner, memory.reader());
    check(result.complete && result.values.empty() && memory.reads == 4,
          "a circular empty sentinel is a complete empty collection");
    memory = fixture();
    result = sc::active_levels(owner, memory.reader(), 1);
    check(result.status == sc::Status::limit_reached && !result.complete &&
          result.consistency_recheck_passed && result.values.size() == 1,
          "cap preserves observed values and rechecks anchors but remains incomplete");
    memory = fixture();
    check(sc::active_levels(owner, memory.reader(), 0).status == sc::Status::limit_reached,
          "a zero cap never reads a nonempty node");
    memory = fixture(0x160, 0x10, 257);
    result = sc::active_levels(owner, memory.reader(), 9999);
    check(result.status == sc::Status::limit_reached && result.values.size() == 256 &&
          memory.reads == 1028, "caller cannot enlarge the absolute 256-node cap");
    memory = fixture(); memory.put(owner + 0x160, std::uint64_t{0});
    check(sc::active_levels(owner, memory.reader()).status == sc::Status::null_pointer,
          "null sentinel stops before a null memory read");
    memory = fixture(); memory.put(nodes + 0x10, std::uint64_t{0});
    check(sc::active_levels(owner, memory.reader()).status == sc::Status::null_pointer,
          "null element is a failed traversal, not an empty list");
    memory = fixture(); memory.put(nodes + 0x10, objects + 1);
    check(sc::active_levels(owner, memory.reader()).status == sc::Status::invalid_address,
          "unaligned values are rejected before any typed object access");
    memory = fixture(); memory.put(sentinel, std::array<std::uint64_t, 2>{{nodes + 1,nodes + 0x100}});
    check(sc::active_levels(owner, memory.reader()).status == sc::Status::invalid_address,
          "unaligned node is rejected");
    memory = fixture(); memory.fail = nodes + 0x10;
    check(sc::active_levels(owner, memory.reader()).status == sc::Status::read_failed,
          "a narrow payload read failure ends traversal");
    memory = fixture();
    memory.put(nodes + 0x100, std::array<std::uint64_t, 2>{{nodes,nodes}});
    check(sc::active_levels(owner, memory.reader()).status == sc::Status::list_cycle,
          "a cycle omitting the sentinel cannot hang");
    memory = fixture();
    memory.put(nodes, std::array<std::uint64_t, 2>{{nodes + 0x100,nodes + 0x100}});
    check(sc::active_levels(owner, memory.reader()).status == sc::Status::list_links_inconsistent,
          "inconsistent previous link is explicit");
    memory = fixture();
    memory.put(sentinel, std::array<std::uint64_t, 2>{{nodes,nodes}});
    check(sc::active_levels(owner, memory.reader()).status == sc::Status::list_links_inconsistent,
          "sentinel previous must agree with the last traversed node");
    for (const auto address : {nodes, nodes + 0x10, owner + 0x160, sentinel}) {
        memory = fixture(); memory.change = address;
        check(sc::active_levels(owner, memory.reader()).status == sc::Status::snapshot_changed,
              "node, payload, owner and sentinel mutations are detected");
    }
    memory = fixture(); result = sc::active_levels(owner, memory.reader());
    memory.put(owner + 0x160, sentinel + 0x1000);
    check(!sc::stable(result, memory.reader()) && !result.complete &&
          result.status == sc::Status::snapshot_changed,
          "parent may recheck owning field after other scene work");
    memory = fixture(); result = sc::active_levels(owner, memory.reader());
    memory.put(sentinel, std::array<std::uint64_t, 2>{{nodes,nodes}});
    check(!sc::stable(result, memory.reader()) && result.status == sc::Status::snapshot_changed,
          "parent may recheck sentinel anchors after other scene work");
    check(sc::active_levels(owner, {}).status == sc::Status::invalid_reader &&
          sc::enumerate(owner, 0x160, 0x20, {}).status == sc::Status::invalid_collection,
          "reader and payload layout prerequisites are explicit");
    memory = fixture();
    check(sc::active_levels(cp::max_user_address - 0x100, memory.reader()).status ==
          sc::Status::invalid_address && memory.reads == 0,
          "owner-range overflow is rejected before a memory read");
    if (failures) return EXIT_FAILURE;
    std::cout << "All scene collection checks passed\n";
    return EXIT_SUCCESS;
}
