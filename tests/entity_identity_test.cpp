#include "aim/entity_identity.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

namespace ei = aim::entity_identity;
namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
constexpr std::uint64_t base = 0x140000000, skeleton = 0x20000000000, data = skeleton + 0x10001;
struct Memory {
    std::map<std::uint64_t, std::vector<unsigned char>> blocks;
    std::map<std::uint64_t, unsigned> counts;
    std::uint64_t fail_address = 0, change_address = 0;
    template<class T> void put(std::uint64_t address, const T& value) {
        auto& b = blocks[address]; b.resize(sizeof(value)); std::memcpy(b.data(), &value, sizeof(value));
    }
    bool read(std::uint64_t address, void* destination, std::size_t bytes) {
        ++counts[address]; const auto it = blocks.find(address);
        if (address == fail_address || it == blocks.end() || it->second.size() != bytes) return false;
        std::memcpy(destination, it->second.data(), bytes);
        if (address == change_address && counts[address] > 1) static_cast<unsigned char*>(destination)[0] ^= 1;
        return true;
    }
};
Memory fixture(const std::string& path, bool heap = false) {
    Memory m; m.put(skeleton, base + ei::skeleton_vtable_rva);
    std::array<unsigned char,32> header{};
    const std::uint64_t size = path.size(), capacity = heap ? 255 : 15;
    if (heap) {
        std::memcpy(header.data(), &data, 8);
        m.blocks[data] = std::vector<unsigned char>(path.begin(), path.end());
    } else if (path.size() <= 15) std::memcpy(header.data(), path.data(), path.size());
    std::memcpy(header.data()+16, &size, 8); std::memcpy(header.data()+24, &capacity, 8);
    m.put(skeleton+0x240, header); return m;
}
ei::Resource run(Memory& m) {
    return ei::read_resource(base, skeleton, [&](auto a, void* d, auto n) { return m.read(a,d,n); });
}
}
int main() {
    auto m = fixture("body.skel"); auto result = run(m);
    check(result.status == ei::Status::resource_read && result.path == "body.skel" &&
          m.counts[skeleton+0x240] == 2, "inline technical resource is read and metadata rechecked");
    m = fixture("actors/humanoid/body.skel", true); result = run(m);
    check(result.status == ei::Status::resource_read && result.path == "actors/humanoid/body.skel" &&
          m.counts[data] == 1, "heap asset bytes are bounded and need not be aligned");
    m = fixture("body\\a.skel");
    check(run(m).path == "body/a.skel", "relative asset separators are normalized for JSON");
    m = fixture(""); check(run(m).status == ei::Status::empty_resource, "empty resource is explicit");
    m = fixture("body.skel"); m.put(skeleton, base + 0x4fa2390); result = run(m);
    check(result.status == ei::Status::unsupported_skeleton_type && m.counts[skeleton+0x240] == 0,
          "a secondary or unsupported vtable does not authorize a typed field read");
    for (const auto path : {"C:/Users/test/model.skel", "/absolute/model.skel", "../body.skel",
                            "actors/../body.skel", "actors/\"bad\".skel", "actors/bad\n.skel"}) {
        m = fixture(path, true); result = run(m);
        check(result.status == ei::Status::nontechnical_resource && result.path.empty(),
              "absolute paths and arbitrary/control strings are not emitted");
    }
    m = fixture(std::string(193, 'a'), true); result = run(m);
    check(result.status == ei::Status::resource_too_long && m.counts[data] == 0,
          "overlong resource is rejected before reading characters");
    m = fixture("body.skel"); auto header = m.blocks[skeleton+0x240];
    const std::uint64_t invalid_capacity = 0; std::memcpy(header.data()+24, &invalid_capacity, 8);
    m.blocks[skeleton+0x240] = header;
    check(run(m).status == ei::Status::invalid_resource_layout, "invalid size/capacity layout is rejected");
    m = fixture("body.skel"); m.change_address = skeleton+0x240;
    check(run(m).status == ei::Status::snapshot_changed, "changed metadata does not emit an asset name");
    m = fixture("actors/body.skel", true); m.fail_address = data;
    check(run(m).status == ei::Status::read_failed, "unreadable asset buffer is explicit");
    check(ei::read_resource(base, 0, {}).status == ei::Status::invalid_address,
          "missing reader and invalid address stop before reading");
    if (failures) return EXIT_FAILURE;
    std::cout << "All entity identity checks passed\n";
}
