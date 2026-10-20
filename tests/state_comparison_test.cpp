#include "aim/state_comparison.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>

namespace sc = aim::state_comparison;
namespace sp = aim::scene_profile;
namespace {
constexpr std::uint64_t base=0x140000000, object=0x20000000000;
int failures=0;
void check(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
template<class T> void put(sc::Sample& sample, std::size_t offset, T value) {
    std::memcpy(sample.data.data()+offset,&value,sizeof(value));
}
sp::Result scene() {
    sp::Result result;
    result.status=sp::Status::node_limit_reached;
    result.scene_guards_matched=result.consistency_recheck_passed=result.local_role_present=true;
    result.camera_geometry_read=true;
    result.viewport_width=1600; result.viewport_height=900;
    result.camera_probe.values.view43={{1,0,0,0,1,0,0,0,1,0,0,0}};
    result.camera_probe.values.fov_degrees=90;
    result.camera_probe.values.near_plane=.1f; result.camera_probe.values.far_plane=3000;
    sp::EntitySummary entity;
    entity.object_address=object; entity.vtable_rva=sc::generic_vtable_rva;
    entity.native_entity_type_known=entity.skeleton_present=entity.position_checked=true;
    entity.character_controller_checked=entity.character_controller_present=true;
    entity.resource.observed_vtable_rva=aim::entity_identity::actor_vtable_rva;
    entity.position={0,0,-10}; result.entities.push_back(entity);
    return result;
}
}
int main() {
    auto s=scene();
    check(sc::select(s)==object,"one centered candidate can be selected");
    auto other=s.entities.front(); other.object_address+=0x1000; other.position.x=.1;
    s.entities.push_back(other);
    check(sc::select(s)==0,"overlapping candidates require a clearer selection");
    s=scene(); s.entities[0].position.x=10;
    check(sc::select(s)==0,"distant screen candidates cannot be selected");
    s=scene(); s.entities[0].is_local_role=true;
    check(sc::select(s)==0,"local reference is never selected");
    s=scene(); s.consistency_recheck_passed=false;
    check(sc::select(s)==0,"an unstable collection cannot select a target");

    sc::Sample memory; put(memory,0,base+sc::generic_vtable_rva);
    std::size_t reads=0, bytes_read=0;
    bool change_type=false;
    const aim::camera_profile::ReadBytes reader=[&](auto address,void* destination,auto bytes) {
        ++reads; bytes_read+=bytes;
        if (address!=object || bytes>sc::prefix_bytes) return false;
        std::memcpy(destination,memory.data.data(),bytes);
        if (change_type && reads%3==0) static_cast<unsigned char*>(destination)[0]^=8;
        return true;
    };
    s=scene(); auto sample=sc::capture(s,base,object,reader);
    check(sc::captured(sample) && reads==3 && bytes_read==sc::prefix_bytes+16 &&
          s.read_count==reads && s.read_bytes==bytes_read,
          "only a guarded native prefix is captured with shared read accounting");
    s=scene(); s.read_count=sp::max_reads; const auto before=reads;
    check(!sc::captured(sc::capture(s,base,object,reader)) && reads==before,
          "exhausted budget issues no remote reads");
    s=scene(); s.read_bytes=sp::max_bytes-8;
    check(!sc::captured(sc::capture(s,base,object,reader)) && s.read_bytes==sp::max_bytes,
          "byte budget cannot be exceeded by prefix capture");
    s=scene(); s.entities.clear();
    check(std::string(sc::capture(s,base,object,reader).status)=="not_observed_in_partial_collection",
          "missing target in a prefix is not labeled dead or destroyed");
    s.collection_complete=true;
    check(std::string(sc::capture(s,base,object,reader).status)=="not_observed_in_collection",
          "missing target in a complete scene still makes no death claim");
    reads=0; change_type=true; s=scene();
    sample=sc::capture(s,base,object,reader);
    check(!sc::captured(sample) && sample.object==0 && sample.data==sc::Sample{}.data,
          "type change during capture clears the captured bytes");

    std::array<sc::Sample,3> samples;
    for (auto& phase:samples) { phase.status="captured"; phase.object=object; }
    put(samples[0],0x90,std::uint32_t{100});
    put(samples[2],0x90,std::uint32_t{100});
    // Native pointers and known position changes must not become field candidates.
    put(samples[0],0x40,object+0x1000);
    put(samples[0],0x7c,20.0f);
    put(samples[1],0x1ac,std::uint32_t{12});
    const auto report=sc::json_report(samples);
    check(report.find("\"offset\":144")!=std::string::npos &&
          report.find("\"u32\":[100,0,100]")!=std::string::npos,
          "a changed scalar is reported as observations, without a health label");
    check(report.find("\"offset\":64,")==std::string::npos &&
          report.find("\"offset\":124,")==std::string::npos &&
          report.find("\"offset\":428,")==std::string::npos &&
          report.find(std::to_string(object))==std::string::npos,
          "pointer-shaped slots, translations and remote addresses are not emitted");
    samples[2].object+=0x1000;
    check(sc::json_report(samples).find("\"changes\":[]")!=std::string::npos,
          "different native references are never compared as the same allocation");
    samples[2].object=object; samples[1].status="not_captured";
    check(sc::json_report(samples).find("\"changes\":[]")!=std::string::npos,
          "incomplete capture cannot emit a three-phase comparison");
    for (auto& phase:samples) { phase.status="captured"; phase.data={}; }
    for(std::size_t offset=8;offset<sc::prefix_bytes;offset+=4) {
        put(samples[0],offset,1.0f); put(samples[1],offset,2.0f); put(samples[2],offset,1.0f);
    }
    check(sc::json_report(samples).find("\"changes_truncated\":true")!=std::string::npos,
          "highly dynamic prefixes cannot create unlimited report entries");
    if(failures) return EXIT_FAILURE;
    std::cout << "All state comparison checks passed\n";
}
