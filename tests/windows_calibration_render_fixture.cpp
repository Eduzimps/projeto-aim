// Render synthetic markers offscreen. No game or desktop pixels are read.
#define wWinMain zyon_production_calibration_main
#include "../src/windows_calibration.cpp"
#undef wWinMain
#include <iostream>

int main() {
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen,800,600);
    ReleaseDC(nullptr,screen);
    if (!memory || !bitmap) return 1;
    const auto previous = SelectObject(memory,bitmap);
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    cal::Frame frame;
    frame.state = cal::State::ready; frame.candidates = 1;
    frame.markers.push_back({{700,450},1});
    const auto before = GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for (unsigned n=0;n<100;++n) paint_frame(memory,800,600,frame,font);
    const bool marker = GetPixel(memory,700,450) == marker_color;
    const bool background = GetPixel(memory,799,599) == RGB(0,0,0);
    const auto after = GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    const bool transparent_hit = window_proc(nullptr,WM_NCHITTEST,0,0) == HTTRANSPARENT;
    const bool no_activate = window_proc(nullptr,WM_MOUSEACTIVATE,0,0) == MA_NOACTIVATE;
    SelectObject(memory,previous); DeleteObject(bitmap); DeleteDC(memory);
    const bool resource_counts_available = before != 0 && after != 0;
    if (!marker || !background || !transparent_hit || !no_activate ||
        (resource_counts_available && after > before+1)) {
        std::cerr << "Calibration render check failed " << marker << background << transparent_hit
                  << no_activate << " GDI " << before << '/' << after << '\n'; return 1;
    }
    std::cout << "Calibration drawing and transparency callbacks passed; GDI accounting "
              << (resource_counts_available ? "passed" : "unavailable") << '\n';
}
