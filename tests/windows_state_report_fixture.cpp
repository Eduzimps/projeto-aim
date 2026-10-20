#include "aim/state_comparison.hpp"
#include <iostream>
int main() {
    namespace sc=aim::state_comparison;
    std::array<sc::Sample,3> samples;
    for (auto& sample:samples) { sample.status="captured"; sample.object=0x20000000000; }
    const std::array<float,3> values{{100,0,100}};
    const float companion=1;
    for (std::size_t n=0;n<3;++n) {
        std::memcpy(samples[n].data.data()+0x90,&values[n],4);
        std::memcpy(samples[n].data.data()+0x94,&companion,4);
        std::memcpy(samples[n].data.data()+0x9c,&companion,4);
    }
    const std::uint32_t not_finite=0x7fc00000;
    std::memcpy(samples[1].data.data()+0x98,&not_finite,4);
    std::cout << sc::json_report(samples);
}
