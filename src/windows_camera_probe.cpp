#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <bcrypt.h>

#include "aim/camera_profile.hpp"

#include <array>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace cp = aim::camera_profile;
namespace {

struct Handle {
    HANDLE value;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (valid()) CloseHandle(value); }
    bool valid() const { return value && value != INVALID_HANDLE_VALUE; }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct HashHandles {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    ~HashHandles() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
};
struct Sample {
    cp::ProbeResult result;
    DWORD error = 0;
};
struct Report {
    const char* status = "internal_error";
    DWORD error = 0;
    ULONG crypto_error = 0;
    bool file_size_matched = false;
    bool file_sha256_matched = false;
    bool image_size_matched = false;
    std::string observed_sha256;
    std::vector<Sample> samples;
};

bool find_client(DWORD& pid, Report& report) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) {
        report.status = "process_snapshot_failed"; report.error = GetLastError(); return false;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    unsigned matches = 0;
    if (!Process32FirstW(snapshot.value, &entry)) {
        report.status = "process_enumeration_failed"; report.error = GetLastError(); return false;
    }
    do {
        if (_wcsicmp(entry.szExeFile, L"BloodStrike.exe") == 0) { pid = entry.th32ProcessID; ++matches; }
    } while (Process32NextW(snapshot.value, &entry));
    const DWORD error = GetLastError();
    if (error != ERROR_NO_MORE_FILES) {
        report.status = "process_enumeration_failed"; report.error = error; return false;
    }
    if (matches != 1) {
        report.status = matches == 0 ? "client_not_running" : "multiple_clients_found";
        return false;
    }
    return true;
}

bool main_module(DWORD pid, MODULEENTRY32W& module, Report& report) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (!snapshot.valid()) {
        report.status = "module_snapshot_failed"; report.error = GetLastError(); return false;
    }
    module.dwSize = sizeof(module);
    if (!Module32FirstW(snapshot.value, &module)) {
        report.status = "module_enumeration_failed"; report.error = GetLastError(); return false;
    }
    do {
        if (_wcsicmp(module.szModule, L"BloodStrike.exe") == 0) return true;
    } while (Module32NextW(snapshot.value, &module));
    report.error = GetLastError();
    report.status = "main_module_not_found";
    return false;
}

bool verify_file(const wchar_t* path, Report& report) {
    Handle file(CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!file.valid()) {
        report.status = "client_file_open_failed"; report.error = GetLastError(); return false;
    }
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file.value, &length)) {
        report.status = "client_file_size_failed"; report.error = GetLastError(); return false;
    }
    report.file_size_matched = length.QuadPart >= 0 &&
        static_cast<std::uint64_t>(length.QuadPart) == cp::file_size;
    if (!report.file_size_matched) { report.status = "build_file_size_mismatch"; return false; }
    HashHandles handles;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&handles.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status >= 0) status = BCryptCreateHash(handles.algorithm, &handles.hash, nullptr, 0, nullptr, 0, 0);
    if (status < 0) {
        report.status = "sha256_initialization_failed"; report.crypto_error = static_cast<ULONG>(status); return false;
    }
    std::array<UCHAR, 65536> buffer{};
    std::uint64_t bytes_hashed = 0;
    for (;;) {
        DWORD bytes = 0;
        if (!ReadFile(file.value, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr)) {
            report.status = "client_file_read_failed"; report.error = GetLastError(); return false;
        }
        if (!bytes) break;
        bytes_hashed += bytes;
        status = BCryptHashData(handles.hash, buffer.data(), bytes, 0);
        if (status < 0) {
            report.status = "sha256_update_failed"; report.crypto_error = static_cast<ULONG>(status); return false;
        }
    }
    std::array<UCHAR, 32> digest{};
    status = BCryptFinishHash(handles.hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    if (status < 0) {
        report.status = "sha256_finalization_failed"; report.crypto_error = static_cast<ULONG>(status); return false;
    }
    std::ostringstream hex;
    hex << std::hex << std::setfill('0');
    for (UCHAR byte : digest) hex << std::setw(2) << static_cast<unsigned>(byte);
    report.observed_sha256 = hex.str();
    report.file_sha256_matched = bytes_hashed == cp::file_size && report.observed_sha256 == cp::file_sha256;
    if (!report.file_sha256_matched) { report.status = "build_sha256_mismatch"; return false; }
    return true;
}

Report collect() {
    static_assert(sizeof(void*) == 8, "Build this diagnostic for AMD64");
    Report report;
    DWORD pid = 0;
    if (!find_client(pid, report)) return report;
    MODULEENTRY32W module{};
    if (!main_module(pid, module, report)) return report;
    report.image_size_matched = module.modBaseSize == cp::image_size;
    if (!verify_file(module.szExePath, report)) return report;
    if (!report.image_size_matched) { report.status = "mapped_image_size_mismatch"; return report; }
    // No write, operation, thread, injection, debugging, or input permissions.
    Handle process(OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process.valid()) {
        report.error = GetLastError();
        report.status = report.error == ERROR_ACCESS_DENIED ? "access_denied" : "process_open_failed";
        return report;
    }
    // Hashing takes time. Reject a recycled PID before making any memory read.
    std::wstring opened_path(32768, L'\0');
    DWORD path_length = static_cast<DWORD>(opened_path.size());
    if (!QueryFullProcessImageNameW(process.value, 0, &opened_path[0], &path_length)) {
        report.status = "process_identity_query_failed"; report.error = GetLastError(); return report;
    }
    opened_path.resize(path_length);
    if (_wcsicmp(opened_path.c_str(), module.szExePath) != 0) {
        report.status = "process_identity_changed"; return report;
    }
    const std::uint64_t base = reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
    DWORD read_error = 0;
    const cp::ReadBytes reader = [&](std::uint64_t address, void* destination, std::size_t bytes) {
        SIZE_T received = 0;
        if (!ReadProcessMemory(process.value, reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(address)),
                               destination, bytes, &received)) {
            read_error = GetLastError(); return false;
        }
        if (received != bytes) { read_error = ERROR_PARTIAL_COPY; return false; }
        return true;
    };
    unsigned successful = 0;
    const ULONGLONG start = GetTickCount64();
    for (unsigned index = 0; index < 3; ++index) {
        const ULONGLONG scheduled = start + index * 400;
        const ULONGLONG now = GetTickCount64();
        if (now < scheduled) Sleep(static_cast<DWORD>(scheduled - now));
        if (GetTickCount64() - start > 1000) break;
        read_error = 0;
        Sample sample{cp::probe(base, module.modBaseSize, reader), 0};
        sample.error = read_error;
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

bool save_report(const std::string& json, DWORD& error) {
    wchar_t desktop[MAX_PATH]{};
    const HRESULT result = SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr,
                                          SHGFP_TYPE_CURRENT, desktop);
    if (FAILED(result)) { error = HRESULT_CODE(result); return false; }
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER stamp{};
    stamp.LowPart = time.dwLowDateTime; stamp.HighPart = time.dwHighDateTime;
    const std::wstring path = std::wstring(desktop) + L"\\BloodStrike-camera-" +
        std::to_wstring(stamp.QuadPart) + L"-" + std::to_wstring(GetCurrentProcessId()) + L".json";
    Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) { error = GetLastError(); return false; }
    std::size_t written = 0;
    while (written < json.size()) {
        DWORD bytes = 0;
        if (!WriteFile(file.value, json.data() + written, static_cast<DWORD>(json.size() - written), &bytes, nullptr)) {
            error = GetLastError(); return false;
        }
        if (!bytes) { error = ERROR_WRITE_FAULT; return false; }
        written += bytes;
    }
    return true;
}

} // namespace

int main() {
    Report report;
    try { report = collect(); } catch (...) { report.status = "internal_error"; }
    const std::string json = json_report(report);
    DWORD save_error = 0;
    const bool saved = save_report(json, save_error);
    std::cout << json;
    if (!saved) {
        std::cerr << "Desktop report could not be saved. Win32 error: " << save_error << '\n';
        return 2;
    }
    return report.status == std::string("candidate_samples_read") ? 0 : 1;
}
