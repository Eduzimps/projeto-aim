#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include "aim/core.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kClassName[] = L"ZyonPreviewWindow";
constexpr COLORREF kBackground = RGB(12, 14, 21);
constexpr COLORREF kPanel = RGB(18, 21, 31);
constexpr COLORREF kRaised = RGB(26, 30, 43);
constexpr COLORREF kLine = RGB(43, 47, 65);
constexpr COLORREF kText = RGB(235, 238, 249);
constexpr COLORREF kMuted = RGB(139, 147, 171);
constexpr COLORREF kPurple = RGB(158, 125, 255);
constexpr COLORREF kGreen = RGB(73, 231, 158);
constexpr COLORREF kAmber = RGB(255, 204, 123);

struct Settings {
    bool aim_enabled = true;
    bool boxes = true;
    bool health = true;
    bool fov_visible = true;
    bool head = true;
    int fov = 155;
    int strength = 55;
};

enum class ControlId {
    None, AimTab, VisualTab, Aim, FovVisible, Fov, Strength,
    Head, Torso, Boxes, Health, Reset
};
struct Control { ControlId id; RECT rect; };
struct PreviewTarget {
    std::uint64_t id;
    double x, top, bottom, half_width, health;
    aim::Vec2 aim_point;
};

struct App {
    HWND window = nullptr;
    int width = 1120;
    int height = 760;
    double scale = 1;
    Settings settings;
    std::wstring settings_path;
    bool settings_saved = false;
    bool panel_visible = true;
    bool right_down = false;
    bool pointer_in_preview = false;
    bool visual_tab = false;
    POINT mouse{-100, -100};
    ControlId dragging = ControlId::None;
    ControlId keyboard_focus = ControlId::None;
    std::vector<Control> controls;
    std::vector<PreviewTarget> targets;
    aim::Vec2 reticle{};
    std::optional<std::uint64_t> selected;
    aim::AimController controller;
    bool reticle_initialized = false;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point previous = started;
    HFONT small = nullptr;
    HFONT normal = nullptr;
    HFONT medium = nullptr;
    HFONT title = nullptr;
    HFONT logo = nullptr;

    int u(double value) const { return static_cast<int>(std::lround(value * scale)); }
    RECT preview() const {
        return {panel_visible ? u(325) : u(24), u(106), width - u(24), height - u(66)};
    }
    RECT scene() const {
        RECT r = preview();
        r.left += u(16); r.right -= u(16);
        r.top += u(70); r.bottom -= u(76);
        return r;
    }
};
App g;

bool contains(RECT r, POINT p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

void fill(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void rounded(HDC dc, RECT rect, int radius, COLORREF color, COLORREF border) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, old_brush); SelectObject(dc, old_pen);
    DeleteObject(brush); DeleteObject(pen);
}

void line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr); LineTo(dc, x2, y2);
    SelectObject(dc, old); DeleteObject(pen);
}

void ellipse(HDC dc, RECT rect, COLORREF color, int thickness = 1, bool solid = false) {
    HPEN pen = CreatePen(PS_SOLID, thickness, color);
    HBRUSH brush = solid ? CreateSolidBrush(color) : static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    Ellipse(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, old_pen); SelectObject(dc, old_brush);
    DeleteObject(pen); if (solid) DeleteObject(brush);
}

void text(HDC dc, std::wstring value, RECT rect, HFONT font, COLORREF color,
          UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX) {
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &rect, flags);
    SelectObject(dc, old);
}

void label(HDC dc, const std::wstring& value, int x, int y, int w, int h,
           HFONT font = nullptr, COLORREF color = kText, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX) {
    text(dc, value, {x, y, x + w, y + h}, font ? font : g.normal, color, flags);
}

HFONT make_font(int size, int weight) {
    return CreateFontW(-g.u(size), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void rebuild_fonts() {
    for (HFONT font : {g.small, g.normal, g.medium, g.title, g.logo}) {
        if (font) DeleteObject(font);
    }
    g.small = make_font(11, FW_NORMAL);
    g.normal = make_font(13, FW_NORMAL);
    g.medium = make_font(13, FW_SEMIBOLD);
    g.title = make_font(22, FW_SEMIBOLD);
    g.logo = make_font(26, FW_BOLD);
}

void load_settings() {
    wchar_t base[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, base))) return;
    std::wstring dir = std::wstring(base) + L"\\Zyon";
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return;
    g.settings_path = dir + L"\\settings.ini";
    auto value = [&](const wchar_t* key, int fallback) {
        return static_cast<int>(GetPrivateProfileIntW(L"ZyonPreview", key, fallback, g.settings_path.c_str()));
    };
    g.settings.aim_enabled = value(L"aim_enabled", 1) != 0;
    g.settings.boxes = value(L"boxes", 1) != 0;
    g.settings.health = value(L"health", 1) != 0;
    g.settings.fov_visible = value(L"fov_visible", 1) != 0;
    g.settings.head = value(L"head", 1) != 0;
    g.settings.fov = std::clamp(value(L"fov", 155), 60, 280);
    g.settings.strength = std::clamp(value(L"strength", 55), 1, 100);
}

void save_settings() {
    if (g.settings_path.empty()) return;
    bool ok = true;
    auto write = [&](const wchar_t* key, int value) {
        std::wstring s = std::to_wstring(value);
        ok = WritePrivateProfileStringW(L"ZyonPreview", key, s.c_str(), g.settings_path.c_str()) && ok;
    };
    write(L"aim_enabled", g.settings.aim_enabled);
    write(L"boxes", g.settings.boxes); write(L"health", g.settings.health);
    write(L"fov_visible", g.settings.fov_visible); write(L"head", g.settings.head);
    write(L"fov", g.settings.fov); write(L"strength", g.settings.strength);
    g.settings_saved = ok;
}

void register_control(ControlId id, RECT r) { g.controls.push_back({id, r}); }

void button(HDC dc, ControlId id, RECT r, const wchar_t* caption, bool active = false) {
    const bool hover = contains(r, g.mouse);
    rounded(dc, r, g.u(10), active ? RGB(53, 41, 82) : (hover ? RGB(35, 39, 56) : kRaised),
            active || g.keyboard_focus == id ? kPurple : kLine);
    text(dc, caption, r, g.medium, active ? RGB(216, 202, 255) : kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    register_control(id, r);
}

void toggle(HDC dc, ControlId id, int y, const wchar_t* name, const wchar_t* description, bool enabled) {
    const int x = g.u(44), right = g.u(280);
    label(dc, name, x, y, g.u(185), g.u(23), g.medium);
    label(dc, description, x, y + g.u(25), g.u(240), g.u(20), g.small, kMuted);
    RECT track{right - g.u(42), y + g.u(2), right, y + g.u(24)};
    rounded(dc, track, g.u(22), enabled ? kPurple : RGB(51, 55, 72),
            g.keyboard_focus == id ? kText : (enabled ? kPurple : RGB(51, 55, 72)));
    int cx = enabled ? track.right - g.u(11) : track.left + g.u(11);
    ellipse(dc, {cx - g.u(7), track.top + g.u(4), cx + g.u(7), track.bottom - g.u(4)}, RGB(245, 244, 255), 1, true);
    register_control(id, {x - g.u(4), y - g.u(2), right + g.u(4), y + g.u(48)});
}

void slider(HDC dc, ControlId id, int y, const wchar_t* caption, int value, int min, int max,
            const wchar_t* suffix, const wchar_t* low, const wchar_t* high) {
    const int x = g.u(44), right = g.u(280);
    label(dc, caption, x, y, g.u(160), g.u(24), g.medium);
    label(dc, std::to_wstring(value) + suffix, right - g.u(72), y, g.u(72), g.u(24), g.medium,
          kPurple, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    RECT track{x, y + g.u(34), right, y + g.u(40)};
    rounded(dc, track, g.u(6), RGB(48, 52, 70), RGB(48, 52, 70));
    const double progress = double(value - min) / double(max - min);
    int knob = track.left + static_cast<int>(std::lround((track.right - track.left) * progress));
    if (knob > track.left) rounded(dc, {track.left, track.top, knob, track.bottom}, g.u(6), kPurple, kPurple);
    ellipse(dc, {knob - g.u(7), track.top - g.u(4), knob + g.u(7), track.bottom + g.u(4)},
            RGB(224, 211, 255), g.u(1), true);
    if (g.keyboard_focus == id) ellipse(dc, {knob - g.u(11), track.top - g.u(8), knob + g.u(11), track.bottom + g.u(8)}, kPurple);
    label(dc, low, x, y + g.u(49), g.u(100), g.u(18), g.small, kMuted);
    label(dc, high, right - g.u(100), y + g.u(49), g.u(100), g.u(18), g.small, kMuted,
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    register_control(id, {x - g.u(8), y + g.u(22), right + g.u(8), y + g.u(52)});
}

void draw_panel(HDC dc) {
    RECT panel{g.u(24), g.u(106), g.u(304), g.height - g.u(66)};
    rounded(dc, panel, g.u(18), kPanel, kLine);
    button(dc, ControlId::AimTab, {g.u(40), g.u(122), g.u(160), g.u(162)}, L"Mira", !g.visual_tab);
    button(dc, ControlId::VisualTab, {g.u(168), g.u(122), g.u(288), g.u(162)}, L"Visual", g.visual_tab);
    if (!g.visual_tab) {
        label(dc, L"ASSISTÊNCIA DE MIRA", g.u(44), g.u(184), g.u(240), g.u(22), g.small, kMuted);
        toggle(dc, ControlId::Aim, g.u(220), L"Suavização", L"Movimenta somente o retículo da prévia", g.settings.aim_enabled);
        slider(dc, ControlId::Fov, g.u(290), L"Raio do FOV", g.settings.fov, 60, 280, L" px", L"Mais restrito", L"Mais amplo");
        slider(dc, ControlId::Strength, g.u(380), L"Força da atração", g.settings.strength, 1, 100, L"%", L"Suave", L"Forte");
        label(dc, L"Ponto de referência", g.u(44), g.u(470), g.u(230), g.u(24), g.medium);
        button(dc, ControlId::Head, {g.u(44), g.u(508), g.u(158), g.u(544)}, L"Cabeça", g.settings.head);
        button(dc, ControlId::Torso, {g.u(166), g.u(508), g.u(280), g.u(544)}, L"Tronco", !g.settings.head);
        label(dc, L"Segure o botão direito na prévia.", g.u(44), g.u(560), g.u(245), g.u(26), g.small, kMuted);
    } else {
        label(dc, L"ELEMENTOS DA PRÉVIA", g.u(44), g.u(184), g.u(240), g.u(22), g.small, kMuted);
        toggle(dc, ControlId::Boxes, g.u(220), L"Caixas ESP", L"Contorno de cada alvo de demonstração", g.settings.boxes);
        toggle(dc, ControlId::Health, g.u(292), L"Barra de vida", L"Indicador vertical ao lado da caixa", g.settings.health);
        toggle(dc, ControlId::FovVisible, g.u(364), L"Círculo do FOV", L"Exibe o alcance em pixels", g.settings.fov_visible);
        rounded(dc, {g.u(44), g.u(456), g.u(280), g.u(558)}, g.u(10), RGB(25, 30, 40), kLine);
        label(dc, L"Dados de demonstração", g.u(56), g.u(468), g.u(210), g.u(24), g.medium, kGreen);
        label(dc, L"Os alvos e os valores de vida\nsão gerados nesta janela.", g.u(56), g.u(498), g.u(210), g.u(46),
              g.small, kMuted, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    }
    const int reset_y = std::max(g.u(602), static_cast<int>(panel.bottom) - g.u(64));
    if (reset_y + g.u(38) < panel.bottom - g.u(10))
        button(dc, ControlId::Reset, {g.u(44), reset_y, g.u(280), reset_y + g.u(38)}, L"Restaurar padrões");
}

void make_targets(double elapsed) {
    RECT s = g.scene();
    const double width = s.right - s.left, height = s.bottom - s.top;
    g.targets.clear();
    const std::array<double, 3> horizontal{0.25, 0.53, 0.79};
    const std::array<double, 3> health{0.92, 0.68, 0.39};
    for (int i = 0; i < 3; ++i) {
        double h = std::min(g.u(186 - i * 15), static_cast<int>(height * 0.66));
        double x = s.left + width * horizontal[i] + std::sin(elapsed * (0.43 + i * 0.08) + i * 1.8) * width * 0.075;
        double bottom = s.top + height * (0.83 - i * 0.035);
        double top = bottom - h;
        double aim_y = top + h * (g.settings.head ? 0.10 : 0.39);
        g.targets.push_back({static_cast<std::uint64_t>(i + 1), x, top, bottom, h * 0.225, health[i], {x, aim_y}});
    }
}

aim::AimConfig aim_config() {
    aim::AimConfig config;
    config.enabled = g.settings.aim_enabled;
    config.fov_radius_px = g.u(g.settings.fov);
    config.strength = g.settings.strength / 100.0;
    config.response_rate = 12;
    config.max_speed_px_s = g.u(1600);
    config.max_acceleration_px_s2 = g.u(12000);
    config.retention_bias_px = g.u(12);
    return config;
}

void update() {
    const auto now = std::chrono::steady_clock::now();
    double dt = std::clamp(std::chrono::duration<double>(now - g.previous).count(), 0.001, 0.05);
    g.previous = now;
    double elapsed = std::chrono::duration<double>(now - g.started).count();
    RECT scene = g.scene();
    if (scene.right <= scene.left || scene.bottom <= scene.top) return;
    aim::Vec2 center{(scene.left + scene.right) / 2.0, (scene.top + scene.bottom) / 2.0};
    if (!g.reticle_initialized) { g.reticle = center; g.reticle_initialized = true; }
    make_targets(elapsed);
    std::vector<aim::Target> candidates;
    for (const auto& target : g.targets) candidates.push_back({target.id, target.aim_point, true, true, true});
    const auto config = aim_config();
    const auto target = aim::select_target(candidates, g.reticle, config, g.selected);
    const auto selected = target ? std::optional<std::uint64_t>(target->id) : std::nullopt;
    if (selected != g.selected) g.controller.reset();
    g.selected = selected;
    if (g.right_down && config.enabled && target) {
        aim::Vec2 movement = g.controller.step({target->screen.x - g.reticle.x, target->screen.y - g.reticle.y}, dt, config);
        g.reticle.x += movement.x;
        g.reticle.y += movement.y;
    } else {
        g.controller.reset();
        const double decay = 1.0 - std::exp(-dt * 3.5);
        g.reticle.x += (center.x - g.reticle.x) * decay;
        g.reticle.y += (center.y - g.reticle.y) * decay;
    }
    g.reticle.x = std::clamp(g.reticle.x, double(scene.left + g.u(6)), double(scene.right - g.u(6)));
    g.reticle.y = std::clamp(g.reticle.y, double(scene.top + g.u(6)), double(scene.bottom - g.u(6)));
}

void draw_target(HDC dc, const PreviewTarget& t) {
    const int x = static_cast<int>(std::lround(t.x));
    const int top = static_cast<int>(std::lround(t.top));
    const int bottom = static_cast<int>(std::lround(t.bottom));
    const int h = bottom - top;
    const int half = static_cast<int>(std::lround(t.half_width));
    const bool selected = g.selected && *g.selected == t.id;
    const COLORREF tint = selected ? RGB(151, 123, 214) : RGB(97, 112, 143);
    // Simple human-shaped figures make the synthetic nature of the scene visible.
    ellipse(dc, {x - h / 13, top + h / 24, x + h / 13, top + h / 5}, RGB(57, 66, 85), 1, true);
    rounded(dc, {x - h / 9, top + h / 5, x + h / 9, top + h * 3 / 5}, g.u(16), RGB(43, 51, 68), RGB(61, 72, 94));
    line(dc, x - h / 10, top + h * 7 / 25, x - h / 5, top + h * 3 / 5, tint, std::max(1, h / 23));
    line(dc, x + h / 10, top + h * 7 / 25, x + h / 5, top + h * 3 / 5, tint, std::max(1, h / 23));
    line(dc, x - h / 20, top + h * 3 / 5, x - h / 9, bottom - h / 30, RGB(58, 68, 89), std::max(1, h / 15));
    line(dc, x + h / 20, top + h * 3 / 5, x + h / 9, bottom - h / 30, RGB(58, 68, 89), std::max(1, h / 15));
    if (g.settings.boxes) {
        const COLORREF box_color = selected ? RGB(186, 157, 255) : RGB(126, 158, 197);
        HPEN pen = CreatePen(PS_SOLID, 1, box_color);
        HGDIOBJ old_pen = SelectObject(dc, pen);
        HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, x - half, top, x + half, bottom);
        SelectObject(dc, old_pen); SelectObject(dc, old_brush); DeleteObject(pen);
        const int corner = std::max(g.u(6), half / 3);
        for (int sign : {-1, 1}) {
            int edge = x + sign * half;
            line(dc, edge, top, edge - sign * corner, top, box_color, g.u(2));
            line(dc, edge, top, edge, top + corner, box_color, g.u(2));
            line(dc, edge, bottom, edge - sign * corner, bottom, box_color, g.u(2));
            line(dc, edge, bottom, edge, bottom - corner, box_color, g.u(2));
        }
    }
    if (g.settings.health) {
        const int bar_x = x - half - g.u(9);
        const auto fraction = aim::health_fraction(t.health * 100, 100).value_or(0);
        fill(dc, {bar_x - g.u(2), top - g.u(1), bar_x + g.u(4), bottom + g.u(1)}, RGB(7, 12, 14));
        fill(dc, {bar_x, top, bar_x + g.u(2), bottom}, RGB(31, 62, 52));
        fill(dc, {bar_x, bottom - static_cast<int>(h * fraction), bar_x + g.u(2), bottom}, kGreen);
        label(dc, std::to_wstring(static_cast<int>(t.health * 100)), bar_x - g.u(26), top - g.u(3), g.u(22), g.u(18), g.small, kGreen,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    label(dc, L"ALVO 0" + std::to_wstring(t.id), x - g.u(60), top - g.u(27), g.u(120), g.u(20), g.small,
          selected ? RGB(209, 191, 255) : kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (selected) {
        const int ax = static_cast<int>(t.aim_point.x), ay = static_cast<int>(t.aim_point.y);
        ellipse(dc, {ax - g.u(4), ay - g.u(4), ax + g.u(4), ay + g.u(4)}, kPurple, g.u(1));
        if (g.right_down && g.settings.aim_enabled)
            line(dc, static_cast<int>(g.reticle.x), static_cast<int>(g.reticle.y), ax, ay, RGB(93, 73, 128));
    }
}

void draw_preview(HDC dc) {
    const RECT r = g.preview(), scene = g.scene();
    rounded(dc, r, g.u(18), RGB(16, 20, 30), kLine);
    label(dc, L"Campo de demonstração", r.left + g.u(22), r.top + g.u(14), g.u(310), g.u(29), g.medium);
    label(dc, L"ALVOS SINTÉTICOS", r.right - g.u(186), r.top + g.u(18), g.u(162), g.u(22), g.small, kMuted,
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    line(dc, r.left + g.u(20), r.top + g.u(55), r.right - g.u(20), r.top + g.u(55), kLine);

    const int saved = SaveDC(dc);
    IntersectClipRect(dc, scene.left, scene.top, scene.right, scene.bottom);
    const int horizon = scene.top + (scene.bottom - scene.top) * 2 / 3;
    for (int i = 0; i < 24; ++i) {
        int y1 = scene.top + (scene.bottom - scene.top) * i / 24;
        int y2 = scene.top + (scene.bottom - scene.top) * (i + 1) / 24;
        fill(dc, {scene.left, y1, scene.right, y2}, RGB(15 + i / 5, 20 + i / 4, 31 + i / 3));
    }
    line(dc, scene.left, horizon, scene.right, horizon, RGB(36, 46, 64));
    const int vanish_x = (scene.left + scene.right) / 2;
    for (int i = -6; i <= 6; ++i)
        line(dc, vanish_x + i * g.u(22), horizon, vanish_x + i * g.u(110), scene.bottom, RGB(28, 37, 52));
    for (int i = 1; i <= 5; ++i) {
        const int y = horizon + static_cast<int>((scene.bottom - horizon) * i * i / 25.0);
        line(dc, scene.left, y, scene.right, y, RGB(28, 37, 52));
    }
    for (const auto& target : g.targets) draw_target(dc, target);
    const int cx = static_cast<int>(std::lround(g.reticle.x));
    const int cy = static_cast<int>(std::lround(g.reticle.y));
    if (g.settings.fov_visible) {
        const int radius = g.u(g.settings.fov);
        HPEN pen = CreatePen(PS_DOT, 1, RGB(105, 85, 151));
        HGDIOBJ old_pen = SelectObject(dc, pen);
        HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);
        SelectObject(dc, old_pen); SelectObject(dc, old_brush); DeleteObject(pen);
    }
    const COLORREF reticle_color = g.right_down && g.settings.aim_enabled ? kGreen : RGB(238, 232, 255);
    line(dc, cx - g.u(11), cy, cx - g.u(4), cy, reticle_color, g.u(1));
    line(dc, cx + g.u(4), cy, cx + g.u(11), cy, reticle_color, g.u(1));
    line(dc, cx, cy - g.u(11), cx, cy - g.u(4), reticle_color, g.u(1));
    line(dc, cx, cy + g.u(4), cx, cy + g.u(11), reticle_color, g.u(1));
    ellipse(dc, {cx - g.u(1), cy - g.u(1), cx + g.u(2), cy + g.u(2)}, reticle_color, 1, true);
    RestoreDC(dc, saved);

    const int hint_width = std::min(g.u(430), static_cast<int>(r.right - r.left) - g.u(40));
    const int hint_x = (r.left + r.right - hint_width) / 2;
    RECT hint{hint_x, r.bottom - g.u(62), hint_x + hint_width, r.bottom - g.u(25)};
    rounded(dc, hint, g.u(10), g.right_down ? RGB(29, 49, 43) : RGB(27, 30, 44),
            g.right_down ? RGB(53, 102, 78) : kLine);
    const wchar_t* message = !g.settings.aim_enabled ? L"Suavização desativada · ative na aba Mira" :
        (g.right_down ? L"TESTANDO · movimento restrito ao retículo da prévia" : L"Segure o botão direito nesta área para testar");
    text(dc, message, hint, g.small, g.right_down ? kGreen : kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void draw(HDC dc) {
    g.controls.clear();
    fill(dc, {0, 0, g.width, g.height}, kBackground);
    rounded(dc, {g.u(24), g.u(23), g.u(68), g.u(67)}, g.u(14), RGB(48, 35, 75), RGB(66, 49, 101));
    label(dc, L"Z", g.u(24), g.u(21), g.u(44), g.u(46), g.logo, RGB(205, 180, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    label(dc, L"ZYON", g.u(82), g.u(20), g.u(160), g.u(32), g.title);
    label(dc, L"Laboratório visual", g.u(83), g.u(51), g.u(180), g.u(22), g.small, kMuted);
    const int badge_width = g.u(353);
    RECT badge{g.width - badge_width - g.u(24), g.u(28), g.width - g.u(24), g.u(63)};
    rounded(dc, badge, g.u(10), RGB(42, 34, 27), RGB(76, 58, 37));
    text(dc, L"PRÉVIA VISUAL · SEM CONEXÃO COM O JOGO", badge, g.small, kAmber, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (g.panel_visible) draw_panel(dc);
    draw_preview(dc);
    line(dc, g.u(24), g.height - g.u(46), g.width - g.u(24), g.height - g.u(46), kLine);
    label(dc, L"INS  painel     1 / 2  abas     TAB  navegar", g.u(24), g.height - g.u(39), g.u(380), g.u(25), g.small, kMuted);
    label(dc, g.settings_saved ? L"Configuração salva neste computador" : L"Configuração local · sem integração com jogo",
          g.width - g.u(340), g.height - g.u(39), g.u(316), g.u(25), g.small, kMuted,
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

ControlId hit(POINT p) {
    for (const auto& control : g.controls) if (contains(control.rect, p)) return control.id;
    return ControlId::None;
}

void adjust_slider(ControlId id, int x) {
    double ratio = std::clamp((x - g.u(44)) / double(g.u(236)), 0.0, 1.0);
    if (id == ControlId::Fov) g.settings.fov = static_cast<int>(std::lround(60 + ratio * 220));
    if (id == ControlId::Strength) g.settings.strength = static_cast<int>(std::lround(1 + ratio * 99));
}

void activate(ControlId id) {
    switch (id) {
        case ControlId::AimTab: g.visual_tab = false; break;
        case ControlId::VisualTab: g.visual_tab = true; break;
        case ControlId::Aim: g.settings.aim_enabled = !g.settings.aim_enabled; g.controller.reset(); break;
        case ControlId::FovVisible: g.settings.fov_visible = !g.settings.fov_visible; break;
        case ControlId::Boxes: g.settings.boxes = !g.settings.boxes; break;
        case ControlId::Health: g.settings.health = !g.settings.health; break;
        case ControlId::Head: g.settings.head = true; g.selected.reset(); g.controller.reset(); break;
        case ControlId::Torso: g.settings.head = false; g.selected.reset(); g.controller.reset(); break;
        case ControlId::Reset: g.settings = Settings{}; g.selected.reset(); g.controller.reset(); break;
        default: return;
    }
    save_settings();
}

void stop_interaction() {
    g.right_down = false;
    g.dragging = ControlId::None;
    g.controller.reset();
    if (GetCapture() == g.window) ReleaseCapture();
}

void keyboard(WPARAM key) {
    if (key == VK_INSERT) {
        g.panel_visible = !g.panel_visible;
        g.reticle_initialized = false; g.keyboard_focus = ControlId::None;
    } else if (key == L'1') {
        g.visual_tab = false; g.panel_visible = true;
    } else if (key == L'2') {
        g.visual_tab = true; g.panel_visible = true;
    } else if (key == VK_ESCAPE) {
        stop_interaction();
    } else if (key == VK_TAB && !g.controls.empty()) {
        auto it = std::find_if(g.controls.begin(), g.controls.end(), [](const Control& c) { return c.id == g.keyboard_focus; });
        int index = it == g.controls.end() ? -1 : static_cast<int>(it - g.controls.begin());
        int delta = GetKeyState(VK_SHIFT) < 0 ? -1 : 1;
        int count = static_cast<int>(g.controls.size());
        g.keyboard_focus = g.controls[(index + delta + count) % count].id;
    } else if (key == VK_SPACE || key == VK_RETURN) {
        activate(g.keyboard_focus);
    } else if (key == VK_LEFT || key == VK_RIGHT) {
        const int step = key == VK_LEFT ? -1 : 1;
        if (g.keyboard_focus == ControlId::Fov) g.settings.fov = std::clamp(g.settings.fov + step * 5, 60, 280);
        if (g.keyboard_focus == ControlId::Strength) g.settings.strength = std::clamp(g.settings.strength + step, 1, 100);
        save_settings();
    }
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE:
            g.window = window; load_settings(); rebuild_fonts(); save_settings();
            SetTimer(window, 1, 16, nullptr); return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize = {g.u(990), g.u(760)};
            return 0;
        }
        case WM_SIZE:
            g.width = LOWORD(lparam); g.height = HIWORD(lparam);
            g.reticle_initialized = false; g.controller.reset();
            InvalidateRect(window, nullptr, FALSE); return 0;
        case WM_DPICHANGED: {
            g.scale = HIWORD(wparam) / 96.0; rebuild_fonts();
            const RECT* suggested = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            g.reticle_initialized = false; return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; HDC dc = BeginPaint(window, &ps);
            if (g.width > 0 && g.height > 0) {
                HDC buffer = CreateCompatibleDC(dc);
                HBITMAP bitmap = CreateCompatibleBitmap(dc, g.width, g.height);
                HGDIOBJ old = SelectObject(buffer, bitmap);
                draw(buffer);
                BitBlt(dc, 0, 0, g.width, g.height, buffer, 0, 0, SRCCOPY);
                SelectObject(buffer, old); DeleteObject(bitmap); DeleteDC(buffer);
            }
            EndPaint(window, &ps); return 0;
        }
        case WM_TIMER:
            if (wparam == 1 && !IsIconic(window)) { update(); InvalidateRect(window, nullptr, FALSE); }
            return 0;
        case WM_MOUSEMOVE: {
            g.mouse = {static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
            g.pointer_in_preview = contains(g.scene(), g.mouse);
            if (g.dragging != ControlId::None) adjust_slider(g.dragging, g.mouse.x);
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE: g.mouse = {-100, -100}; return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(window);
            POINT point{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
            ControlId id = hit(point); g.keyboard_focus = id;
            if (id == ControlId::Fov || id == ControlId::Strength) {
                g.dragging = id; SetCapture(window); adjust_slider(id, point.x);
            } else activate(id);
            return 0;
        }
        case WM_LBUTTONUP:
            if (g.dragging != ControlId::None) { g.dragging = ControlId::None; ReleaseCapture(); save_settings(); }
            return 0;
        case WM_RBUTTONDOWN: {
            POINT point{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
            if (contains(g.scene(), point)) { SetFocus(window); g.right_down = true; SetCapture(window); }
            return 0;
        }
        case WM_RBUTTONUP:
            g.right_down = false; g.controller.reset();
            if (g.dragging == ControlId::None && GetCapture() == window) ReleaseCapture();
            return 0;
        case WM_CAPTURECHANGED: g.right_down = false; g.dragging = ControlId::None; g.controller.reset(); return 0;
        case WM_KILLFOCUS: stop_interaction(); save_settings(); return 0;
        case WM_KEYDOWN: keyboard(wparam); InvalidateRect(window, nullptr, FALSE); return 0;
        case WM_SETCURSOR:
            if (LOWORD(lparam) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, hit(g.mouse) != ControlId::None ? IDC_HAND : IDC_ARROW)); return TRUE;
            }
            break;
        case WM_CLOSE: DestroyWindow(window); return 0;
        case WM_DESTROY:
            KillTimer(window, 1); save_settings();
            for (HFONT font : {g.small, g.normal, g.medium, g.title, g.logo}) if (font) DeleteObject(font);
            PostQuitMessage(0); return 0;
        default: break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    // Resolve DPI awareness dynamically so older supported Windows versions still start.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using SetAwareness = BOOL(WINAPI*)(HANDLE);
    SetAwareness awareness = nullptr;
    const FARPROC address = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    static_assert(sizeof(awareness) == sizeof(address), "Windows function pointer size");
    std::memcpy(&awareness, &address, sizeof(awareness));
    if (awareness) awareness(reinterpret_cast<HANDLE>(-4));
    else SetProcessDPIAware();
    HDC screen = GetDC(nullptr);
    g.scale = GetDeviceCaps(screen, LOGPIXELSX) / 96.0;
    ReleaseDC(nullptr, screen);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc); wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc; wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon; wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return 1;
    HWND window = CreateWindowExW(0, kClassName, L"ZYON — Laboratório visual | Prévia independente",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  g.u(1160), g.u(820), nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show); UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
