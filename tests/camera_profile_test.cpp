#include "aim/camera_profile.hpp"

#include <cstring>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace cp = aim::camera_profile;
namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
struct Memory {
    std::map<std::uint64_t, std::vector<unsigned char>> blocks;
    std::uint64_t fail_address = 0;
    std::vector<std::uint64_t> reads;
    template<class T> void put(std::uint64_t address, const T& value) {
        static_assert(std::is_trivially_copyable<T>::value, "Raw memory fixture");
        auto& bytes = blocks[address];
        bytes.resize(sizeof(T));
        std::memcpy(bytes.data(), &value, sizeof(T));
    }
    bool read(std::uint64_t address, void* destination, std::size_t bytes) {
        reads.push_back(address);
        const auto found = blocks.find(address);
        if (address == fail_address || found == blocks.end() || found->second.size() != bytes) return false;
        std::memcpy(destination, found->second.data(), bytes);
        return true;
    }
};
constexpr std::uint64_t base = 0x0000000140000000;
constexpr std::uint64_t engine = 0x0000020000000000;
constexpr std::uint64_t gameplay = engine + 0x10000;
constexpr std::uint64_t player = engine + 0x20000;
constexpr std::uint64_t camera = engine + 0x30000;
Memory fixture(std::uint64_t aslr_base = base) {
    Memory memory;
    memory.put(aslr_base + cp::engine_global_rva, engine);
    memory.put(engine, aslr_base + cp::engine_vtable_rva);
    memory.put(engine + 0x58, gameplay);
    memory.put(gameplay, aslr_base + cp::gameplay_vtable_rva);
    memory.put(gameplay + 0x58, player);
    memory.put(player, aslr_base + cp::player_vtable_rva);
    memory.put(player + 0xd88, camera);
    memory.put(camera, aslr_base + cp::rdata_begin_rva + 0x100);
    memory.put(camera + 0x41c, std::array<float, 12>{{1, 0, 0, 0, 1, 0, 0, 0, 1, 20, 30, 40}});
    memory.put(camera + 0x454, std::array<float, 3>{{90.0f, 0.1f, 5000.0f}});
    return memory;
}
cp::ProbeResult run(Memory& memory, std::uint64_t aslr_base = base) {
    return cp::probe(aslr_base, cp::image_size,
        [&](std::uint64_t address, void* destination, std::size_t bytes) {
            return memory.read(address, destination, bytes);
        });
}
} // namespace

int main() {
    auto memory = fixture();
    auto result = run(memory);
    check(result.status == cp::ProbeStatus::candidate_sample && result.root_vtables_matched &&
          result.camera_vtable_in_rdata, "known chain produces a labelled candidate sample");
    check(result.values.view43[9] == 20.0f && result.values.fov_degrees == 90.0f &&
          result.values.far_plane == 5000.0f, "only specified camera fields are decoded");
    check(memory.reads.size() == 10, "probe performs exactly ten narrow reads");
    constexpr std::uint64_t shifted_base = 0x00007ff600000000;
    memory = fixture(shifted_base);
    check(run(memory, shifted_base).status == cp::ProbeStatus::candidate_sample,
          "all expected vtables and global addresses relocate with ASLR");
    check(cp::probe(base, cp::image_size - 1, {}).status == cp::ProbeStatus::invalid_module,
          "wrong mapped image size rejected before any read");
    check(cp::probe(cp::max_user_address - 10, cp::image_size, {}).status == cp::ProbeStatus::invalid_module,
          "module range overflow rejected");
    check(cp::probe(base, cp::image_size, {}).status == cp::ProbeStatus::invalid_reader,
          "missing reader rejected");
    std::uint64_t address = 0;
    check(!cp::checked_address(cp::max_user_address, 1, 8, address) &&
          !cp::checked_address(cp::max_user_address, 0, 8, address) &&
          !cp::checked_address(1, 0, 8, address), "checked address rejects overflow and low pointers");

    memory = fixture();
    memory.put(base + cp::engine_global_rva, std::uint64_t{0});
    result = run(memory);
    check(result.status == cp::ProbeStatus::null_pointer && memory.reads.size() == 1,
          "null root stops without dereferencing null");
    memory = fixture();
    memory.put(engine + 0x58, cp::max_user_address);
    check(run(memory).status == cp::ProbeStatus::invalid_address,
          "out of bounds chained object is not read");
    for (const auto object : {engine, gameplay, player}) {
        memory = fixture();
        memory.put(object, std::uint64_t{base});
        check(run(memory).status == cp::ProbeStatus::vtable_mismatch,
              "each exact root vtable guard rejects a mismatch");
    }
    memory = fixture();
    memory.put(camera, base + cp::rdata_end_rva);
    check(run(memory).status == cp::ProbeStatus::camera_vtable_outside_rdata,
          "camera vtable upper boundary is exclusive");
    memory = fixture();
    memory.fail_address = camera + 0x41c;
    result = run(memory);
    check(result.status == cp::ProbeStatus::read_failed && std::string(result.stage) == "camera_view43",
          "read failure reports exact stage without continuing");
    memory = fixture();
    std::array<float, 12> bad_view{};
    bad_view[5] = std::numeric_limits<float>::quiet_NaN();
    memory.put(camera + 0x41c, bad_view);
    check(run(memory).status == cp::ProbeStatus::nonfinite_view, "nonfinite camera value rejected");
    for (const auto parameters : {std::array<float, 3>{{0, .1f, 100}},
                                 std::array<float, 3>{{180, .1f, 100}},
                                 std::array<float, 3>{{90, 0, 100}},
                                 std::array<float, 3>{{90, 10, 10}},
                                 std::array<float, 3>{{90, 10, 1}},
                                 std::array<float, 3>{{90, .1f, std::numeric_limits<float>::infinity()}}}) {
        memory = fixture();
        memory.put(camera + 0x454, parameters);
        check(run(memory).status == cp::ProbeStatus::invalid_projection_parameters,
              "invalid FOV or depth is rejected");
    }
    if (failures) { std::cerr << failures << " camera profile check(s) failed\n"; return EXIT_FAILURE; }
    std::cout << "All camera profile checks passed\n";
    return EXIT_SUCCESS;
}
