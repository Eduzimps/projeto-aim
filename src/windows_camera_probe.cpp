#include "aim/windows_probe_support.hpp"
#include <iostream>
#include <locale>
#include <vector>

namespace cp = aim::camera_profile;
namespace wp = aim::windows_probe;
namespace {

struct Sample {
    cp::ProbeResult result;
    DWORD error = 0;
};
struct Report : wp::IdentityReport {
    std::vector<Sample> samples;
};

Report collect() {
    Report report;
    wp::Session session;
    if (!wp::open_client(session, report)) return report;
    wp::MemoryReader memory{session.process.value, 0};
    const cp::ReadBytes reader = [&](std::uint64_t address, void* destination, std::size_t bytes) {
        return memory(address, destination, bytes);
    };
    unsigned successful = 0;
    const ULONGLONG start = GetTickCount64();
    for (unsigned index = 0; index < 3; ++index) {
        const ULONGLONG scheduled = start + index * 400;
        const ULONGLONG now = GetTickCount64();
        if (now < scheduled) Sleep(static_cast<DWORD>(scheduled - now));
        if (GetTickCount64() - start > 1000) break;
        memory.error = 0;
        Sample sample{cp::probe(session.base, session.mapped_size, reader), 0};
        sample.error = memory.error;
        if (sample.result.status == cp::ProbeStatus::candidate_sample) ++successful;
        report.samples.push_back(sample);
    }
    report.status = successful == 3 ? "candidate_samples_read" :
        (successful ? "candidate_samples_partial" : "camera_candidate_unavailable");
    return report;
}

std::string json_report(const Report& report) {
    std::ostringstream json;
    json.imbue(std::locale::classic());
    json << std::boolalpha << std::setprecision(std::numeric_limits<float>::max_digits10);
    json << "{\n  \"format\":\"zyon-camera-candidate-v1\",\n"
         << "  \"validation\":\"static_candidate_not_runtime_verified\",\n"
         << "  \"status\":\"" << report.status << "\",\n"
         << "  \"win32_error\":" << report.error << ",\n"
         << "  \"crypto_status\":" << report.crypto_error << ",\n"
         << "  \"profile\":{\"sha256\":\"" << cp::file_sha256
         << "\",\"file_size\":" << cp::file_size << ",\"image_size\":" << cp::image_size
         << ",\"machine\":\"AMD64\"},\n"
         << "  \"file_size_matched\":" << report.file_size_matched << ",\n"
         << "  \"file_sha256_matched\":" << report.file_sha256_matched << ",\n"
         << "  \"mapped_image_size_matched\":" << report.image_size_matched << ",\n"
         << "  \"observed_file_sha256\":\"" << report.observed_sha256 << "\",\n"
         << "  \"samples\":[";
    for (std::size_t index = 0; index < report.samples.size(); ++index) {
        const auto& sample = report.samples[index];
        const auto& result = sample.result;
        if (index) json << ',';
        json << "\n    {\"index\":" << index + 1 << ",\"status\":\"" << cp::status_name(result.status)
             << "\",\"stage\":\"" << result.stage << "\",\"win32_error\":" << sample.error
             << ",\"root_vtables_matched\":" << result.root_vtables_matched
             << ",\"camera_vtable_in_rdata\":" << result.camera_vtable_in_rdata;
        if (result.status == cp::ProbeStatus::candidate_sample) {
            json << ",\"view43\":[";
            for (std::size_t n = 0; n < result.values.view43.size(); ++n) {
                if (n) json << ',';
                json << result.values.view43[n];
            }
            json << "],\"fov_degrees\":" << result.values.fov_degrees
                 << ",\"near_plane\":" << result.values.near_plane
                 << ",\"far_plane\":" << result.values.far_plane;
        }
        json << '}';
    }
    json << "\n  ],\n  \"limitations\":\"Camera candidate only; no full camera type, projection convention or entity validation.\"\n}\n";
    return json.str();
}

} // namespace

int main() {
    Report report;
    try { report = collect(); } catch (...) { report.status = "internal_error"; }
    const std::string json = json_report(report);
    DWORD save_error = 0;
    const bool saved = wp::save_report(json, L"BloodStrike-camera", save_error);
    std::cout << json;
    if (!saved) {
        std::cerr << "Desktop report could not be saved. Win32 error: " << save_error << '\n';
        return 2;
    }
    return report.status == std::string("candidate_samples_read") ? 0 : 1;
}
