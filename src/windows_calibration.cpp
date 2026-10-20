#include "aim/windows_probe_support.hpp"
#include "aim/calibration_frame.hpp"

#include <string>
#include <cstring>

namespace wp = aim::windows_probe;
namespace sp = aim::scene_profile;
namespace cal = aim::calibration;
namespace {
constexpr wchar_t window_class[] = L"ZyonCalibrationWindow";
constexpr COLORREF marker_color = RGB(169, 130, 255);
constexpr ULONGLONG duration_ms = 120000;
constexpr UINT refresh_interval_ms = 16;
constexpr ULONGLONG max_sample_ms = 50;

void draw_text(HDC dc, HFONT font, RECT rect, const std::wstring& text, COLORREF color) {
    const auto previous = SelectObject(dc, font);
    SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, previous);
}

// Black is the overlay's transparency key. Rendering never copies game pixels.
void paint_frame(HDC dc, int width, int height, const cal::Frame& frame, HFONT font) {
    RECT client{0,0,width,height};
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    HBRUSH panel = CreateSolidBrush(RGB(19,22,33));
    RECT hud{16,16,560,108}; FillRect(dc,&hud,panel); DeleteObject(panel);
    draw_text(dc,font,{30,21,545,45},L"ZYON · Calibração de posições",marker_color);
    std::wstring status;
    if (frame.state == cal::State::unavailable) status = L"Leitura indisponível; aguardando nova amostra.";
    else if (frame.state == cal::State::viewport_mismatch) status = L"Área da câmera diferente da janela; pontos suspensos.";
    else status = std::to_wstring(frame.markers.size()) + L" pontos visíveis · " +
        std::to_wstring(frame.candidates) + L" candidatos" + (frame.partial ? L" · recorte parcial" : L"");
    draw_text(dc,font,{30,47,545,70},status,RGB(230,233,245));
    draw_text(dc,font,{30,74,545,98},L"Teste de 120 s · INSERT oculta · END fecha · Sem vida/equipe",RGB(163,171,194));
    HPEN pen = CreatePen(PS_SOLID,2,marker_color);
    const auto old_pen = SelectObject(dc,pen);
    const auto old_brush = SelectObject(dc,GetStockObject(NULL_BRUSH));
    for (const auto& marker : frame.markers) {
        const int x = static_cast<int>(std::lround(marker.pixel.x));
        const int y = static_cast<int>(std::lround(marker.pixel.y));
        Ellipse(dc,x-6,y-6,x+7,y+7);
        MoveToEx(dc,x-9,y,nullptr); LineTo(dc,x+10,y);
        MoveToEx(dc,x,y-9,nullptr); LineTo(dc,x,y+10);
        draw_text(dc,font,{x+12,y-12,x+100,y+12},L"C"+std::to_wstring(marker.number),marker_color);
    }
    SelectObject(dc,old_brush); SelectObject(dc,old_pen); DeleteObject(pen);
}

struct WindowSearch { DWORD pid; HWND window = nullptr; std::uint64_t area = 0; };
BOOL CALLBACK find_game_window(HWND window, LPARAM argument) {
    auto& search = *reinterpret_cast<WindowSearch*>(argument);
    DWORD pid = 0; GetWindowThreadProcessId(window,&pid);
    if (pid != search.pid || !IsWindowVisible(window) || GetWindow(window,GW_OWNER)) return TRUE;
    RECT r{}; if (!GetClientRect(window,&r) || r.right <= 0 || r.bottom <= 0) return TRUE;
    const auto area = static_cast<std::uint64_t>(r.right) * static_cast<std::uint64_t>(r.bottom);
    if (area > search.area) { search.area = area; search.window = window; }
    return TRUE;
}

struct App {
    wp::Session session;
    HWND overlay = nullptr;
    HWND game = nullptr;
    HFONT font = nullptr;
    ULONGLONG started = 0;
    bool hidden = false, insert_down = false;
    cal::Frame frame;

    void tick() {
        if (GetTickCount64()-started >= duration_ms || (GetAsyncKeyState(VK_END)&0x8000)) {
            DestroyWindow(overlay); return;
        }
        DWORD exit_code = 0;
        if (!GetExitCodeProcess(session.process.value,&exit_code) || exit_code != STILL_ACTIVE) {
            DestroyWindow(overlay); return;
        }
        const bool insert = (GetAsyncKeyState(VK_INSERT)&0x8000) != 0;
        if (insert && !insert_down) hidden = !hidden;
        insert_down = insert;
        if (!IsWindow(game)) {
            WindowSearch search{GetProcessId(session.process.value)};
            EnumWindows(find_game_window,reinterpret_cast<LPARAM>(&search)); game = search.window;
        }
        if (hidden || !game || IsIconic(game) || GetForegroundWindow() != game) {
            ShowWindow(overlay,SW_HIDE); return;
        }
        RECT r{}; POINT origin{};
        if (!GetClientRect(game,&r) || !ClientToScreen(game,&origin) ||
            !cal::supported_viewport(r.right,r.bottom)) {
            ShowWindow(overlay,SW_HIDE); return;
        }
        wp::MemoryReader memory{session.process.value,0};
        const ULONGLONG start = GetTickCount64();
        const aim::camera_profile::ReadBytes reader = [&](auto address, void* destination, auto bytes) {
            if (GetTickCount64()-start >= max_sample_ms) return false;
            return memory(address,destination,bytes);
        };
        try {
            auto scene = sp::probe(session.base,session.mapped_size,reader);
            if (cal::refresh_camera(scene,session.base,session.mapped_size,reader) &&
                GetTickCount64()-start < max_sample_ms)
                frame = cal::make_frame(scene,r.right,r.bottom);
            else frame = {};
        }
        catch (...) { frame = {}; }
        SetWindowPos(overlay,HWND_TOPMOST,origin.x,origin.y,r.right,r.bottom,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(overlay,nullptr,FALSE);
        // Present now instead of waiting behind the next timer message.
        UpdateWindow(overlay);
    }
};
App* app = nullptr;

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_TIMER: if (app) app->tick(); return 0;
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(window,&ps);
        RECT r{}; GetClientRect(window,&r);
        if (app && cal::supported_viewport(r.right,r.bottom)) {
            HDC buffer = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc,r.right,r.bottom);
            if (buffer && bitmap) {
                const auto old = SelectObject(buffer,bitmap);
                paint_frame(buffer,r.right,r.bottom,app->frame,app->font);
                BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);
                SelectObject(buffer,old);
            }
            if (bitmap) DeleteObject(bitmap);
            if (buffer) DeleteDC(buffer);
        }
        EndPaint(window,&ps); return 0;
    }
    case WM_DESTROY: KillTimer(window,1); PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window,message,wparam,lparam);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using SetDpi = BOOL (WINAPI*)(HANDLE);
    const auto entry = GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext");
    SetDpi dpi = nullptr;
    static_assert(sizeof(dpi) == sizeof(entry), "Windows function pointer representation");
    std::memcpy(&dpi,&entry,sizeof(dpi));
    if (dpi) dpi(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
    else SetProcessDPIAware();
    App state; wp::IdentityReport identity;
    if (!wp::open_client(state.session,identity)) {
        const wchar_t* message = L"Abra o jogo no treinamento. O cliente precisa corresponder à versão analisada.";
        if (identity.error == ERROR_ACCESS_DENIED) message = L"O Windows recusou o acesso de leitura ao jogo.";
        MessageBoxW(nullptr,message,L"Zyon · Calibração",MB_OK|MB_ICONINFORMATION); return 1;
    }
    state.font = CreateFontW(-14,0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    WNDCLASSW type{}; type.hInstance = instance; type.lpfnWndProc = window_proc; type.lpszClassName = window_class;
    if (!RegisterClassW(&type)) { if (state.font) DeleteObject(state.font); return 1; }
    app = &state;
    state.overlay = CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
        window_class,L"ZYON · Calibração de posições",WS_POPUP,0,0,800,600,nullptr,nullptr,instance,nullptr);
    if (!state.overlay) { if (state.font) DeleteObject(state.font); app = nullptr; return 1; }
    if (!SetLayeredWindowAttributes(state.overlay,RGB(0,0,0),255,LWA_COLORKEY)) {
        DestroyWindow(state.overlay);
        if (state.font) DeleteObject(state.font);
        app = nullptr; return 1;
    }
    state.started = GetTickCount64();
    if (!SetTimer(state.overlay,1,refresh_interval_ms,nullptr)) DestroyWindow(state.overlay);
    MSG message{};
    while (GetMessageW(&message,nullptr,0,0)>0) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (state.font) DeleteObject(state.font);
    app = nullptr;
    return 0;
}
