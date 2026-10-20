#include "aim/scene_profile.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace sp = aim::scene_profile;
namespace cp = aim::camera_profile;
namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
constexpr std::uint64_t base = 0x140000000;
constexpr std::uint64_t engine = 0x20000000000;
constexpr std::uint64_t gameplay = engine + 0x10000;
constexpr std::uint64_t player = engine + 0x20000;
constexpr std::uint64_t camera = engine + 0x30000;
constexpr std::uint64_t world = engine + 0x40000;
constexpr std::uint64_t level = engine + 0x50000;
constexpr std::uint64_t area = engine + 0x60000;
constexpr std::uint64_t sentinel = engine + 0x70000;
constexpr std::uint64_t nodes = engine + 0x80000;
constexpr std::uint64_t objects = engine + 0x200000;
struct Memory {
    std::map<std::uint64_t, std::vector<unsigned char>> blocks;
    std::map<std::uint64_t, std::size_t> read_counts;
    std::size_t reads = 0;
    std::uint64_t fail_address = 0, change_address = 0;
    std::size_t change_on_read = 2;
    template<class T> void put(std::uint64_t address, const T& value) {
        auto& bytes = blocks[address]; bytes.resize(sizeof(T));
        std::memcpy(bytes.data(), &value, sizeof(T));
    }
    bool read(std::uint64_t address, void* destination, std::size_t bytes) {
        ++reads;
        const auto count = ++read_counts[address];
        const auto found = blocks.find(address);
        if (address == fail_address || found == blocks.end() || found->second.size() != bytes) return false;
        std::memcpy(destination, found->second.data(), bytes);
        if (address == change_address && count >= change_on_read)
            static_cast<unsigned char*>(destination)[0] ^= 0x10;
        return true;
    }
};
Memory fixture(std::size_t count = 2, std::uint64_t aslr_base = base) {
    Memory memory;
    memory.put(aslr_base + cp::engine_global_rva, engine);
    memory.put(engine, aslr_base + cp::engine_vtable_rva);
    memory.put(engine + 0x58, gameplay);
    memory.put(gameplay, aslr_base + cp::gameplay_vtable_rva);
    memory.put(gameplay + 0x58, player);
    memory.put(player, aslr_base + cp::player_vtable_rva);
    memory.put(player + 0xd88, camera);
    memory.put(camera, aslr_base + sp::camera_vtable_rva);
    memory.put(camera + 0x41c, std::array<float, 12>{{1,0,0,0,1,0,0,0,1,0,0,0}});
    memory.put(camera + 0x454, std::array<float, 3>{{90,.1f,3000}});
    memory.put(camera + 0x7c, std::array<float, 3>{{10,20,30}});
    memory.put(camera + 0x408, std::array<std::uint16_t, 2>{{1920,1080}});
    memory.put(camera + 0x98, world);
    memory.put(world, aslr_base + sp::world_vtable_rva);
    memory.put(world + 0x148, level);
    memory.put(level, aslr_base + sp::level_vtables[0]);
    memory.put(level + 0xf0, area);
    memory.put(area, aslr_base + sp::area_vtable_rva);
    memory.put(area + 0xb8, sentinel);
    memory.put(sentinel, std::array<std::uint64_t, 2>{{count ? nodes : sentinel,
                                                   count ? nodes + (count - 1) * 0x100 : sentinel}});
    for (std::size_t n = 0; n < count; ++n) {
        const auto node = nodes + n * 0x100;
        const auto object = objects + n * 0x100;
        memory.put(node, std::array<std::uint64_t, 3>{{n + 1 == count ? sentinel : node + 0x100,
                                                    n ? node - 0x100 : sentinel, object}});
        memory.put(object, aslr_base + sp::entity_vtables[2]);
        memory.put(object + 0xa1, std::uint8_t{0});
    }
    return memory;
}
sp::Result run(Memory& memory, std::uint64_t aslr_base = base, std::size_t limit = sp::max_nodes) {
    return sp::probe(aslr_base, cp::image_size,
        [&](std::uint64_t address, void* destination, std::size_t bytes) {
            return memory.read(address, destination, bytes);
        }, limit);
}
} // namespace

int main() {
    auto memory = fixture();
    auto result = run(memory);
    check(result.status == sp::Status::candidate_collection && result.collection_complete &&
          result.consistency_recheck_passed && result.entities.size() == 2,
          "a consistent small list is complete, with an explicit candidate label");
    check(result.camera_geometry_read && result.viewport_width == 1920 &&
          result.viewport_height == 1080 && result.origin[2] == 30,
          "camera origin and actual viewport fields are decoded");
    check(result.entities[0].native_entity_type_known && result.entities[0].skeleton_checked &&
          !result.entities[0].skeleton_present, "high-bit clear skeleton tag means absent");
    check(result.read_count == memory.reads && result.read_count <= sp::max_reads &&
          result.read_bytes <= sp::max_bytes, "all profile reads, including camera reads, use one budget");
    memory = fixture(0);
    result = run(memory);
    check(result.collection_complete && result.entities.empty(), "a circular empty sentinel is a complete empty collection");
    memory = fixture(2, 0x7ff600000000);
    check(run(memory, 0x7ff600000000).collection_complete, "root and native vtable guards relocate with ASLR");
    memory = fixture();
    memory.put(level, base + sp::level_vtables[1]);
    check(run(memory).collection_complete, "statically proven ILevelPacked is accepted");
    memory = fixture();
    memory.put(camera, base + cp::rdata_begin_rva + 0x100);
    result = run(memory);
    check(result.status == sp::Status::camera_type_unrecognized && !result.camera_geometry_read,
          "a camera vtable range alone does not authorize inherited scene field reads");
    memory = fixture();
    memory.put(camera + 0x98, std::uint64_t{0});
    result = run(memory);
    check(result.status == sp::Status::null_pointer && std::string(result.stage) == "world_vtable" &&
          !result.collection_complete, "a detached camera is unavailable, not an empty world");
    for (const auto object : {world, level, area}) {
        memory = fixture(); memory.put(object, base + cp::rdata_begin_rva);
        check(run(memory).status == sp::Status::vtable_mismatch, "each scene object requires its known native type");
    }
    memory = fixture();
    memory.put(camera + 0x408, std::array<std::uint16_t, 2>{{0,1080}});
    check(run(memory).status == sp::Status::invalid_camera_geometry, "zero viewport is rejected");
    memory = fixture();
    memory.put(camera + 0x7c, std::array<float, 3>{{0,std::numeric_limits<float>::quiet_NaN(),0}});
    check(run(memory).status == sp::Status::invalid_camera_geometry, "nonfinite origin is rejected");
    memory = fixture();
    memory.put(objects, base + cp::rdata_begin_rva + 0x100);
    result = run(memory);
    check(result.collection_complete && !result.entities[0].native_entity_type_known &&
          !result.entities[0].skeleton_checked && memory.read_counts[objects + 0xa1] == 0,
          "unknown native vtable is summarized without typed component reads");
    memory = fixture();
    memory.put(objects, base + cp::rdata_end_rva);
    check(run(memory).status == sp::Status::entity_vtable_outside_rdata, "entity vtable upper boundary is exclusive");
    memory = fixture();
    memory.put(objects + 0xa1, std::uint8_t{0x83});
    const std::uint64_t components = engine + 0x100000;
    memory.put(objects + 0x40, components);
    memory.put(components + 3 * 8, engine + 0x110000);
    result = run(memory);
    check(result.collection_complete && result.entities[0].skeleton_present &&
          memory.read_counts[components + 3 * 8] == 1 && memory.read_counts[engine + 0x110000] == 0,
          "tagged skeleton component is located but never dereferenced");
    memory = fixture(); memory.fail_address = nodes;
    check(run(memory).status == sp::Status::read_failed, "unreadable node stops traversal");
    memory = fixture();
    memory.put(sentinel, std::array<std::uint64_t, 2>{{nodes + 1,nodes + 0x100}});
    check(run(memory).status == sp::Status::invalid_address, "unaligned node is rejected before reading");
    memory = fixture();
    memory.put(nodes + 0x100, std::array<std::uint64_t, 3>{{nodes,nodes,objects + 0x100}});
    check(run(memory).status == sp::Status::list_cycle, "a cycle that omits the sentinel stops traversal");
    memory = fixture();
    memory.put(nodes, std::array<std::uint64_t, 3>{{nodes + 0x100,nodes + 0x100,objects}});
    check(run(memory).status == sp::Status::list_links_inconsistent, "inconsistent previous links are explicit");
    memory = fixture(); memory.change_address = nodes;
    check(run(memory).status == sp::Status::snapshot_changed, "a node changed during inspection is rejected");
    memory = fixture(); memory.change_address = camera + 0x98;
    check(run(memory).status == sp::Status::snapshot_changed, "world root changes detected by the final recheck");
    memory = fixture(); memory.change_address = player + 0xd88;
    result = run(memory);
    check(result.status == sp::Status::snapshot_changed &&
          std::string(result.stage) == "camera_root_recheck" && !result.camera_geometry_read,
          "a different camera between initial sample and scene traversal cannot mix their fields");
    memory = fixture(); memory.change_address = sentinel;
    check(run(memory).status == sp::Status::snapshot_changed, "sentinel mutation is detected");
    memory = fixture(257);
    result = run(memory);
    check(result.status == sp::Status::node_limit_reached && result.entities.size() == 256 &&
          !result.collection_complete && !result.consistency_recheck_passed &&
          result.read_count <= sp::max_reads && result.read_bytes <= sp::max_bytes,
          "a large collection is explicitly partial and obeys fixed limits");
    memory = fixture();
    result = run(memory, base, 1);
    check(result.status == sp::Status::node_limit_reached && result.entities.size() == 1,
          "caller may reduce but cannot enlarge the traversal limit");
    check(sp::probe(base, cp::image_size - 1, {}).status == sp::Status::invalid_module &&
          sp::probe(base, cp::image_size, {}).status == sp::Status::invalid_reader,
          "module and reader prerequisites stop before traversal");
    if (failures) return EXIT_FAILURE;
    std::cout << "All scene profile checks passed\n";
    return EXIT_SUCCESS;
}
