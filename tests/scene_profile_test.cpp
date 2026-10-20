#include "aim/scene_profile.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
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
constexpr std::uint64_t active_sentinel = engine + 0x71000;
constexpr std::uint64_t subarea_sentinel = engine + 0x72000;
constexpr std::uint64_t nodes = engine + 0x80000;
constexpr std::uint64_t scenario = engine + 0x100000;
constexpr std::uint64_t objects = engine + 0x200000;
constexpr std::uint64_t object_stride = 0x400;
constexpr std::uint64_t scenario_vtable = 0x04fa1ce8;
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
        if (address == fail_address) return false;
        auto found = blocks.upper_bound(address);
        if (found == blocks.begin()) return false;
        --found;
        const auto offset = address - found->first;
        if (offset > found->second.size() || bytes > found->second.size() - offset) return false;
        std::memcpy(destination, found->second.data() + offset, bytes);
        if (address == change_address && count >= change_on_read)
            static_cast<unsigned char*>(destination)[0] ^= 0x10;
        return true;
    }
};
void collection(Memory& memory, std::uint64_t owner, std::uint64_t field,
                std::uint64_t head, std::uint64_t node_base,
                const std::vector<std::uint64_t>& values, std::uint64_t value_offset = 0x10) {
    memory.put(owner + field, head);
    memory.put(head, std::array<std::uint64_t, 2>{{values.empty() ? head : node_base,
        values.empty() ? head : node_base + (values.size() - 1) * 0x100}});
    for (std::size_t n = 0; n < values.size(); ++n) {
        const auto node = node_base + n * 0x100;
        memory.put(node, std::array<std::uint64_t, 2>{{
            n + 1 == values.size() ? head : node + 0x100,
            n ? node - 0x100 : head}});
        // Map keys/string fields deliberately have no readable fixture bytes.
        memory.put(node + value_offset, values[n]);
    }
}
void entity(Memory& memory, std::uint64_t address, std::uint64_t aslr_base = base) {
    memory.put(address, aslr_base + sp::entity_vtables[2]);
    memory.put(address + 0xa1, std::uint8_t{0});
    memory.put(address + 0x7c, std::array<float, 3>{{10, 20, -30}});
    constexpr auto max = std::numeric_limits<std::int32_t>::max();
    memory.put(address + 0x1ac, std::array<std::int32_t, 3>{{max, max, max}});
}
void empty_level(Memory& memory, std::uint64_t address, std::uint64_t root_area,
                 std::uint64_t head, std::uint64_t aslr_base = base) {
    memory.put(address, aslr_base + sp::level_vtables[0]);
    memory.put(address + 0xf0, root_area);
    collection(memory, address, 0x100, head, head + 0x1000, {}, 0x30);
}
void empty_area(Memory& memory, std::uint64_t address, std::uint64_t head,
                std::uint64_t aslr_base = base) {
    memory.put(address, aslr_base + sp::area_vtable_rva);
    collection(memory, address, 0xb8, head, head + 0x1000, {});
}
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
    memory.put(camera + 0x98, std::uint64_t{0}); // Actual reported detached-camera state.
    memory.put(gameplay + 0x60, scenario);
    memory.put(scenario, aslr_base + scenario_vtable);
    memory.put(scenario + 0xd60, world);
    memory.put(world, aslr_base + sp::world_vtable_rva);
    memory.put(world + 0x148, level);
    collection(memory, world, 0x160, active_sentinel, engine + 0x900000, {});
    empty_level(memory, level, area, subarea_sentinel, aslr_base);
    memory.put(area, aslr_base + sp::area_vtable_rva);
    std::vector<std::uint64_t> values;
    for (std::size_t n = 0; n < count; ++n) {
        const auto object = objects + n * object_stride;
        values.push_back(object);
        entity(memory, object, aslr_base);
    }
    collection(memory, area, 0xb8, sentinel, nodes, values);
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
          "a consistent small collection is complete with an explicit candidate label");
    check(result.camera_geometry_read && result.viewport_width == 1920 &&
          result.viewport_height == 1080 && result.origin[2] == 30,
          "camera origin and actual viewport fields are decoded");
    check(result.entities.size() == 2 && result.entities[0].native_entity_type_known &&
          result.entities[0].skeleton_checked && !result.entities[0].skeleton_present,
          "known native entity's clear skeleton tag means absent");
    check(result.entities.size() == 2 && result.entities[0].position_checked &&
          result.entities[0].position.x == 10 && result.entities[0].position.y == 20 &&
          result.entities[0].position.z == -30,
          "known native entity uses the verified split-translation position decoder");
    check(result.read_count == memory.reads && result.read_count <= sp::max_reads &&
          result.read_bytes <= sp::max_bytes, "camera, collections and positions share one read budget");
    check(memory.read_counts[camera + 0x98] == 0,
          "detached camera.World is never read to locate the active scene");
    memory = fixture(); memory.fail_address = camera + 0x98;
    check(run(memory).collection_complete, "unreadable detached camera.World cannot block scenario.ActiveWorld");
    memory = fixture(0);
    result = run(memory);
    check(result.collection_complete && result.entities.empty(), "a circular empty sentinel is complete and empty");
    memory = fixture(2, 0x7ff600000000);
    check(run(memory, 0x7ff600000000).collection_complete, "all root and native type guards relocate with ASLR");
    memory = fixture(); memory.put(level, base + sp::level_vtables[1]);
    check(run(memory).collection_complete, "statically proven ILevelPacked is accepted");
    memory = fixture(); memory.put(camera, base + cp::rdata_begin_rva + 0x100);
    result = run(memory);
    check(result.status == sp::Status::camera_type_unrecognized && !result.camera_geometry_read,
          "a camera vtable range alone does not authorize geometry reads");
    memory = fixture(); memory.put(gameplay + 0x60, std::uint64_t{0});
    result = run(memory);
    check(result.status == sp::Status::null_pointer && !result.collection_complete,
          "null scenario is unavailable rather than an empty scene");
    memory = fixture(); memory.put(scenario + 0xd60, std::uint64_t{0});
    result = run(memory);
    check(result.status == sp::Status::null_pointer && !result.collection_complete,
          "null active world is unavailable rather than an empty scene");
    for (const auto object : {scenario, world, level, area}) {
        memory = fixture(); memory.put(object, base + cp::rdata_begin_rva);
        result = run(memory);
        check(result.status == sp::Status::vtable_mismatch,
              "each root/scene object requires its supported native type");
        if (object == scenario)
            check(memory.read_counts[scenario + 0xd60] == 0,
                  "generic IComponent scenario is not read as ClientScenario before its exact guard");
    }
    memory = fixture(); memory.put(camera + 0x408, std::array<std::uint16_t, 2>{{0,1080}});
    check(run(memory).status == sp::Status::invalid_camera_geometry, "zero viewport is rejected");
    memory = fixture();
    memory.put(camera + 0x7c, std::array<float, 3>{{0,std::numeric_limits<float>::quiet_NaN(),0}});
    check(run(memory).status == sp::Status::invalid_camera_geometry, "nonfinite origin is rejected");
    memory = fixture(); memory.put(objects, base + cp::rdata_begin_rva + 0x100);
    result = run(memory);
    check(result.collection_complete && result.entities.size() == 2 &&
          !result.entities[0].native_entity_type_known && !result.entities[0].skeleton_checked &&
          !result.entities[0].position_checked && memory.read_counts[objects + 0xa1] == 0 &&
          memory.read_counts[objects + 0x7c] == 0 && memory.read_counts[objects + 0x1ac] == 0,
          "unknown native types are summarized without skeleton or position field reads");
    memory = fixture(); memory.put(objects, base + cp::rdata_end_rva);
    check(run(memory).status == sp::Status::entity_vtable_outside_rdata,
          "entity vtable upper boundary is exclusive");
    memory = fixture(); memory.put(objects + 0xa1, std::uint8_t{0x83});
    const std::uint64_t components = engine + 0x1000000;
    memory.put(objects + 0x40, components);
    memory.put(components + 3 * 8, engine + 0x1100000);
    result = run(memory);
    check(result.collection_complete && result.entities.size() == 2 && result.entities[0].skeleton_present &&
          memory.read_counts[components + 3 * 8] == 1 && memory.read_counts[engine + 0x1100000] == 0,
          "tagged skeleton component is located but never dereferenced");
    memory = fixture();
    memory.put(objects + 0x1ac, std::array<std::int32_t, 3>{{100,-10,25}});
    result = run(memory);
    check(result.collection_complete && result.entities.size() == 2 &&
          result.entities[0].position.x == 110 && result.entities[0].position.y == 10 &&
          result.entities[0].position.z == -5,
          "integer world translation is added without discarding non-sentinel values");
    memory = fixture();
    memory.put(objects + 0x7c, std::array<float, 3>{{0,std::numeric_limits<float>::quiet_NaN(),0}});
    check(run(memory).status == sp::Status::invalid_entity_position, "nonfinite typed entity position is explicit");
    memory = fixture(); memory.fail_address = nodes;
    check(run(memory).status == sp::Status::read_failed, "unreadable node stops enumeration");
    memory = fixture();
    memory.put(sentinel, std::array<std::uint64_t, 2>{{nodes + 1,nodes + 0x100}});
    check(run(memory).status == sp::Status::invalid_address, "unaligned collection node is rejected before reading");
    memory = fixture();
    memory.put(nodes + 0x100, std::array<std::uint64_t, 2>{{nodes,nodes}});
    check(run(memory).status == sp::Status::list_cycle, "a cycle omitting the sentinel stops traversal");
    memory = fixture(); memory.put(nodes, std::array<std::uint64_t, 2>{{nodes + 0x100,nodes + 0x100}});
    check(run(memory).status == sp::Status::list_links_inconsistent, "inconsistent previous links are explicit");
    memory = fixture(); memory.change_address = nodes;
    check(run(memory).status == sp::Status::snapshot_changed, "a node changed during inspection is rejected");
    memory = fixture(); memory.change_address = gameplay + 0x60;
    check(run(memory).status == sp::Status::snapshot_changed, "scenario changes are found by final root rechecks");
    memory = fixture(); memory.change_address = scenario + 0xd60;
    check(run(memory).status == sp::Status::snapshot_changed, "active world changes are found by final root rechecks");
    memory = fixture(); memory.change_address = player + 0xd88;
    result = run(memory);
    check(result.status == sp::Status::snapshot_changed &&
          std::string(result.stage) == "camera_root_recheck" && !result.camera_geometry_read,
          "camera swaps cannot mix an old camera sample with new geometry");
    memory = fixture(); memory.change_address = sentinel;
    check(run(memory).status == sp::Status::snapshot_changed, "entity collection sentinel mutation is detected");
    memory = fixture(); memory.change_address = active_sentinel;
    check(run(memory).status == sp::Status::snapshot_changed, "active-level sentinel mutation is detected");
    memory = fixture(); memory.change_address = active_sentinel; memory.change_on_read = 3;
    result = run(memory);
    check(result.status == sp::Status::snapshot_changed &&
          std::string(result.stage) == "collection_final_recheck",
          "active-level mutations after their initial traversal are checked again at the end");
    memory = fixture(); memory.change_address = world + 0x148;
    check(run(memory).status == sp::Status::snapshot_changed,
          "optional default-level ownership is anchored even when active levels are stable");
    memory = fixture(); memory.change_address = level + 0xf0;
    check(run(memory).status == sp::Status::snapshot_changed,
          "optional level root-area ownership is anchored through the full scene traversal");
    memory = fixture(257);
    result = run(memory);
    check(result.status == sp::Status::node_limit_reached && result.entities.size() == 256 &&
          !result.collection_complete && !result.consistency_recheck_passed &&
          result.read_count <= sp::max_reads && result.read_bytes <= sp::max_bytes,
          "large entity collections are explicitly partial and respect all fixed budgets");
    memory = fixture(); result = run(memory, base, 1);
    check(result.status == sp::Status::node_limit_reached && result.entities.size() == 1,
          "caller can reduce the global entity traversal limit");

    constexpr auto extra_level = engine + 0x400000;
    constexpr auto extra_area = engine + 0x500000;
    constexpr auto extra_object = engine + 0x600000;
    memory = fixture();
    memory.put(world + 0x148, std::uint64_t{0});
    collection(memory, world, 0x160, active_sentinel, engine + 0x900000, {level});
    result = run(memory);
    check(result.collection_complete && result.entities.size() == 2,
          "active levels are discovered when the optional default level is null");
    memory = fixture();
    memory.put(level + 0xf0, std::uint64_t{0});
    collection(memory, level, 0x100, subarea_sentinel, engine + 0x910000, {area}, 0x30);
    result = run(memory);
    check(result.collection_complete && result.entities.size() == 2,
          "sub-areas are discovered when the optional root area is null");
    memory = fixture();
    collection(memory, world, 0x160, active_sentinel, engine + 0x900000, {level,extra_level});
    empty_level(memory, extra_level, area, engine + 0x920000);
    collection(memory, level, 0x100, subarea_sentinel, engine + 0x930000, {area,extra_area}, 0x30);
    empty_area(memory, extra_area, engine + 0x940000);
    collection(memory, extra_area, 0xb8, engine + 0x940000, engine + 0x950000,
               {objects,extra_object});
    entity(memory, extra_object);
    result = run(memory);
    check(result.collection_complete && result.entities.size() == 3,
          "shared default/active levels, root/sub-areas and entity pointers are deduplicated");
    memory = fixture();
    std::vector<std::uint64_t> many_levels;
    for (std::size_t n = 0; n < 17; ++n) {
        const auto pointer = extra_level + n * 0x1000;
        many_levels.push_back(pointer);
        empty_level(memory, pointer, std::uint64_t{0}, engine + 0x1000000 + n * 0x1000);
    }
    collection(memory, world, 0x160, active_sentinel, engine + 0x900000, many_levels);
    result = run(memory);
    check(!result.collection_complete && result.status == sp::Status::node_limit_reached &&
          result.read_count <= sp::max_reads && result.read_bytes <= sp::max_bytes,
          "active-level discovery is capped rather than silently declared complete");
    memory = fixture();
    std::vector<std::uint64_t> many_areas;
    for (std::size_t n = 0; n < 33; ++n) {
        const auto pointer = extra_area + n * 0x1000;
        many_areas.push_back(pointer);
        empty_area(memory, pointer, engine + 0x1100000 + n * 0x1000);
    }
    collection(memory, level, 0x100, subarea_sentinel, engine + 0x900000, many_areas, 0x30);
    result = run(memory);
    check(!result.collection_complete && result.status == sp::Status::node_limit_reached &&
          result.read_count <= sp::max_reads && result.read_bytes <= sp::max_bytes,
          "sub-area discovery is capped rather than silently declared complete");
    memory = fixture(1);
    std::vector<std::uint64_t> repeated_areas;
    for (std::size_t n = 0; n < 10; ++n) {
        const auto pointer = extra_area + n * 0x1000;
        const auto head = engine + 0x2000000 + n * 0x20000;
        repeated_areas.push_back(pointer);
        memory.put(pointer, base + sp::area_vtable_rva);
        collection(memory, pointer, 0xb8, head, head + 0x1000,
                   std::vector<std::uint64_t>(200, objects));
    }
    collection(memory, level, 0x100, subarea_sentinel, engine + 0x900000, repeated_areas, 0x30);
    result = run(memory);
    check(result.status == sp::Status::node_limit_reached && !result.collection_complete &&
          result.entity_nodes_observed == sp::max_nodes && result.entities.size() == 1 &&
          result.read_count == memory.reads && result.read_count <= sp::max_reads &&
          result.read_bytes <= sp::max_bytes,
          "duplicate entity pointers cannot evade the global raw-node cap");
    memory = fixture(256);
    std::vector<std::uint64_t> shared_areas;
    for (std::size_t n = 0; n < 32; ++n) {
        const auto pointer = engine + 0x3000000 + n * 0x1000;
        const auto head = engine + 0x4000000 + n * 0x20000;
        shared_areas.push_back(pointer);
        memory.put(pointer, base + sp::area_vtable_rva);
        std::vector<std::uint64_t> values;
        for (std::size_t j = 0; j < 8; ++j)
            values.push_back(objects + (n * 8 + j) * object_stride);
        collection(memory, pointer, 0xb8, head, head + 0x1000, values);
    }
    std::vector<std::uint64_t> active;
    for (std::size_t n = 0; n < 16; ++n) {
        const auto pointer = n == 0 ? level : engine + 0x500000 + n * 0x1000;
        const auto head = engine + 0x1000000 + n * 0x20000;
        if (n) active.push_back(pointer);
        empty_level(memory, pointer, 0, head);
        collection(memory, pointer, 0x100, head, head + 0x1000, shared_areas, 0x30);
    }
    collection(memory, world, 0x160, active_sentinel, engine + 0x900000, active);
    result = run(memory);
    check(result.status == sp::Status::read_budget_exceeded && !result.collection_complete &&
          result.read_count == sp::max_reads && result.read_count == memory.reads &&
          result.read_bytes <= sp::max_bytes && result.entity_nodes_observed <= sp::max_nodes,
          "valid overlapping level/area collections still share the global read budget");
    check(sp::probe(base, cp::image_size - 1, {}).status == sp::Status::invalid_module &&
          sp::probe(base, cp::image_size, {}).status == sp::Status::invalid_reader,
          "module and reader prerequisites stop before any collection read");
    if (failures) { std::cerr << failures << " scene profile check(s) failed\n"; return EXIT_FAILURE; }
    std::cout << "All scene profile checks passed\n";
    return EXIT_SUCCESS;
}
