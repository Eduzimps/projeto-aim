// This test modifies %LOCALAPPDATA%\Zyon\settings.ini. Run it only inside a
// dedicated, disposable Wine prefix, with ZYON_UI_TEST=1. The runner must launch
// the preview first and pass the executable's Windows path as the sole argument.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>

namespace {

int checks = 0;

void check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

HWND preview_window() {
    return FindWindowW(L"ZyonPreviewWindow", nullptr);
}

HWND wait_for_preview(DWORD timeout_ms) {
    const ULONGLONG started = GetTickCount64();
    do {
        if (HWND window = preview_window()) {
            return window;
        }
        Sleep(50);
    } while (GetTickCount64() - started < timeout_ms);
    return nullptr;
}

void refresh(HWND window) {
    InvalidateRect(window, nullptr, FALSE);
    UpdateWindow(window);
}

void click(HWND window, int x, int y) {
    SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    refresh(window);
}

void key(HWND window, WPARAM key_code) {
    SendMessageW(window, WM_KEYDOWN, key_code, 0);
    refresh(window);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    wchar_t test_mode[2]{};
    const DWORD test_mode_length = GetEnvironmentVariableW(
        L"ZYON_UI_TEST", test_mode, static_cast<DWORD>(std::size(test_mode)));
    if (test_mode_length != 1 || test_mode[0] != L'1') {
        std::cerr << "Refusing UI test: set ZYON_UI_TEST=1 in a dedicated, "
                     "disposable Wine prefix.\n";
        return 2;
    }
    if (argc != 2) {
        std::cerr << "Usage: windows_ui_smoke.exe <Windows path to preview.exe>\n";
        return 2;
    }

    check(argc == 2, "preview executable path provided");
    HWND window = wait_for_preview(30000);
    check(window != nullptr, "initial preview window appeared within 30 seconds");

    wchar_t appdata[MAX_PATH]{};
    check(SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
                                   SHGFP_TYPE_CURRENT, appdata)),
          "local application data directory available");
    const std::wstring ini_path = std::wstring(appdata) + L"\\Zyon\\settings.ini";
    const auto value = [&](const wchar_t* name) {
        return GetPrivateProfileIntW(L"ZyonPreview", name, 9999, ini_path.c_str());
    };
    const auto check_defaults = [&]() {
        check(value(L"fov") == 155 && value(L"strength") == 55 &&
                  value(L"head") == 1 && value(L"boxes") == 1 &&
                  value(L"health") == 1 && value(L"fov_visible") == 1 &&
                  value(L"aim_enabled") == 1,
              "default settings saved");
    };

    RECT client{};
    GetClientRect(window, &client);
    check(client.right >= 990 && client.bottom >= 700, "initial client size usable");
    key(window, L'1');
    click(window, 120, std::max(602, static_cast<int>(client.bottom) - 130) + 19);
    check_defaults();

    click(window, 280, 327);
    check(value(L"fov") == 280, "FOV slider maximum saved");
    click(window, 44, 417);
    check(value(L"strength") == 1, "strength slider minimum saved");
    key(window, VK_RIGHT);
    check(value(L"strength") == 2, "keyboard adjusts the selected slider");
    click(window, 220, 526);
    check(value(L"head") == 0, "torso selection saved");

    key(window, L'2');
    click(window, 260, 230);
    check(value(L"boxes") == 0, "box toggle saved");
    click(window, 260, 302);
    check(value(L"health") == 0, "health toggle saved");
    click(window, 260, 374);
    check(value(L"fov_visible") == 0, "FOV visibility toggle saved");

    key(window, VK_INSERT);
    key(window, VK_INSERT);
    check(IsWindow(window) != FALSE, "panel toggle leaves the preview usable");
    key(window, L'1');
    SendMessageW(window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(760, 400));
    Sleep(300);
    check(IsWindow(window) != FALSE, "internal right-button preview remains responsive");
    SendMessageW(window, WM_RBUTTONUP, 0, MAKELPARAM(760, 400));

    SetWindowPos(window, nullptr, 0, 0, 1040, 800, SWP_NOMOVE | SWP_NOZORDER);
    GetClientRect(window, &client);
    check(client.right >= 990, "resize leaves the preview usable");
    SendMessageW(window, WM_CLOSE, 0, 0);
    check(preview_window() == nullptr, "preview closes cleanly");

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(argv[1], nullptr, nullptr, nullptr, FALSE, 0, nullptr,
                         nullptr, &startup, &process) != FALSE,
          "preview restarts");
    WaitForInputIdle(process.hProcess, 5000);
    window = wait_for_preview(30000);
    check(window != nullptr, "restarted preview window appeared within 30 seconds");
    Sleep(80);
    refresh(window);

    check(value(L"fov") == 280 && value(L"strength") == 2 && value(L"head") == 0,
          "saved values survive restart");
    key(window, L'2');
    click(window, 260, 230);
    check(value(L"boxes") == 1, "preview reloaded disabled boxes from disk");
    click(window, 260, 302);
    check(value(L"health") == 1, "preview reloaded disabled health from disk");
    click(window, 260, 374);
    check(value(L"fov_visible") == 1, "preview reloaded disabled FOV visibility from disk");

    key(window, L'1');
    GetClientRect(window, &client);
    click(window, 120, std::max(602, static_cast<int>(client.bottom) - 130) + 19);
    check_defaults();

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    std::cout << checks << " UI checks passed\n";
    return EXIT_SUCCESS;
}
