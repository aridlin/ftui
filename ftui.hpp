// ftui.hpp — Tiny immediate-mode GUI for Windows
// Usage: #define FTUI_IMPLEMENTATION in exactly one .cpp before including this header.
// Optional defines (before FTUI_IMPLEMENTATION):
//   FTUI_KEEP_CONSOLE  — keeps the console window (useful during development)

#pragma once

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <string>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

// Win32 macro DrawText→DrawTextA renames the D2D vtable method the same way,
// so we keep the macro live and call DrawText() — the expansion matches the vtable.

// ============================================================
// Public API
// ============================================================

namespace ftui {

// --- Color / Style ---

struct Color { float r, g, b, a; };

struct Style {
    Color background;
    Color panel;
    Color text;
    Color text_dim;
    Color border;
    Color button;
    Color button_hover;
    Color button_active;
    Color input_bg;
    Color input_focus;

    float window_padding;
    float item_spacing;
    float item_height;
    float rounding;
    float border_width;
    float font_size;
};

// Built-in themes
Style default_dark_style();    // FTUI default — neutral dark
Style catppuccin_mocha_style();// Catppuccin Mocha
Style nord_style();            // Nord
Style gruvbox_dark_style();    // Gruvbox Dark
Style one_dark_style();        // One Dark (Atom)

void set_style(const Style& style);
const Style& get_style();

// --- Config / Lifecycle ---

struct Config {
    const wchar_t* title         = L"FTUI App";
    int            width         = 960;
    int            height        = 640;
    bool           resizable     = true;
    bool           center_window = true;
    HICON          icon          = nullptr; // optional HICON for window/taskbar
};

bool create_window(const Config& cfg = {});
bool pump();
void begin();
void end();
void shutdown();

// --- Widgets ---

void text(const char* label);
void separator();
void spacing(float px = 8.0f);
bool button(const char* label);

enum class InputFlags : unsigned {
    None     = 0,
    Password = 1 << 0,
    ReadOnly = 1 << 1,
};
inline InputFlags operator|(InputFlags a, InputFlags b) {
    return static_cast<InputFlags>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
inline bool operator&(InputFlags a, InputFlags b) {
    return (static_cast<unsigned>(a) & static_cast<unsigned>(b)) != 0;
}

bool input(const char* label, char* buffer, int buffer_size,
           InputFlags flags = InputFlags::None);

bool checkbox(const char* label, bool* value);
bool slider_float(const char* label, float* value, float min_v, float max_v);

struct ImageHandle { ID2D1Bitmap* bitmap = nullptr; };
void         image(ImageHandle* img, float width, float height);

// --- Image loading (WIC — supports PNG, JPEG, BMP, GIF, TIFF) ---
ImageHandle* load_image(const char* utf8_path);
void         free_image(ImageHandle* img);

// --- File dialog ---
struct FileFilter {
    const wchar_t* name; // e.g. L"PNG Images"
    const wchar_t* spec; // e.g. L"*.png;*.jpg"
};
// Shows the native Windows open-file dialog. Returns UTF-8 path or "" if cancelled.
std::string open_file_dialog(
    const wchar_t*    title        = L"Open File",
    const FileFilter* filters      = nullptr,
    int               filter_count = 0);

// --- Debug ---

struct DebugState {
    bool show_layout_rects = false;
    bool show_hovered_id   = false;
    bool show_active_id    = false;
    bool show_fps          = false;
    bool log_widget_calls  = false; // output via OutputDebugStringA (use DebugView)
};

DebugState& debug();

} // namespace ftui


// ============================================================
// Implementation
// ============================================================

#ifdef FTUI_IMPLEMENTATION

// Suppress the console window by default; define FTUI_KEEP_CONSOLE to opt out.
#ifndef FTUI_KEEP_CONSOLE
#pragma comment(linker, "/subsystem:windows /entry:mainCRTStartup")
#endif

#include <shobjidl.h>   // IFileOpenDialog
#include <wincodec.h>   // WIC image loading
#pragma comment(lib, "windowscodecs.lib")

namespace ftui {
namespace internal {

// ---- Debug log (uses OutputDebugStringA, visible in DebugView) ----

static void dbg(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
}

// ---- Helpers ------------------------------------------------

static std::wstring utf8_to_wide(const char* utf8) {
    if (!utf8 || !utf8[0]) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring out(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &out[0], len);
    return out;
}

static int hash_str(const char* s) {
    // FNV-1a 32-bit
    unsigned h = 2166136261u;
    for (; *s; ++s) h = (h ^ (unsigned char)*s) * 16777619u;
    return (int)(h & 0x7fffffff) + 1; // never 0
}

// Split "Visible Text##id_suffix" → visible part + full string for hashing
static void split_label(const char* label, char* visible, int vis_len, const char** hash_src) {
    const char* sep = strstr(label, "##");
    if (sep) {
        int n = (int)(sep - label);
        if (n >= vis_len) n = vis_len - 1;
        memcpy(visible, label, n);
        visible[n] = '\0';
    } else {
        int n = (int)strlen(label);
        if (n >= vis_len) n = vis_len - 1;
        memcpy(visible, label, n);
        visible[n] = '\0';
    }
    *hash_src = label;
}

// ---- Rect ---------------------------------------------------

struct Rect { float x, y, w, h; };

static bool rect_contains(Rect r, float px, float py) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

// ---- State structs -----------------------------------------

struct PlatformState {
    HWND      hwnd     = nullptr;
    HINSTANCE instance = nullptr;
    bool      running  = false;
    int       width    = 0;  // logical (DIP) dimensions
    int       height   = 0;
};

struct RendererState {
    ID2D1Factory*           d2d_factory      = nullptr;
    ID2D1HwndRenderTarget*  target           = nullptr;
    ID2D1SolidColorBrush*   brush            = nullptr;
    IDWriteFactory*         dwrite_factory   = nullptr;
    IDWriteTextFormat*      text_format      = nullptr;
    IDWriteRenderingParams* rendering_params = nullptr;
    IWICImagingFactory*     wic_factory      = nullptr;
    float                   dpi_scale        = 1.0f;
    bool                    drawing          = false;
    bool                    com_inited       = false;
};

struct InputState {
    float mouse_x          = 0;
    float mouse_y          = 0;
    bool  mouse_down       = false;
    bool  mouse_pressed    = false;
    bool  mouse_released   = false;
    bool  key_backspace    = false;
    bool  key_enter        = false;
    char  text_input[64]   = {};
    int   text_input_count = 0;
    bool  focused          = true;
};

struct UIContext {
    int   hot_id           = 0;
    int   active_id        = 0;
    int   focused_input_id = 0;
    int   frame_index      = 0;
    Rect  content_region   = {};
    float cursor_x         = 0;
    float cursor_y         = 0;
};

// ---- Globals -----------------------------------------------

static PlatformState g_platform;
static RendererState g_renderer;
static InputState    g_input;
static UIContext     g_ctx;
static DebugState    g_debug;
static Style         g_style;
static LARGE_INTEGER g_freq;
static LARGE_INTEGER g_last_time;
static float         g_fps        = 0.0f;
static int           g_fps_frames = 0;
static float         g_fps_accum  = 0.0f;

} // namespace internal

// ============================================================
// Themes
// ============================================================

Style default_dark_style() {
    Style s;
    s.background    = {0.055f, 0.055f, 0.059f, 1.0f};
    s.panel         = {0.090f, 0.094f, 0.102f, 1.0f};
    s.text          = {0.910f, 0.910f, 0.910f, 1.0f};
    s.text_dim      = {0.663f, 0.678f, 0.702f, 1.0f};
    s.border        = {0.169f, 0.176f, 0.192f, 1.0f};
    s.button        = {0.090f, 0.094f, 0.102f, 1.0f};
    s.button_hover  = {0.137f, 0.149f, 0.169f, 1.0f};
    s.button_active = {0.176f, 0.192f, 0.220f, 1.0f};
    s.input_bg      = {0.071f, 0.075f, 0.082f, 1.0f};
    s.input_focus   = {0.310f, 0.420f, 0.780f, 1.0f};
    s.window_padding = 20.0f;
    s.item_spacing   = 10.0f;
    s.item_height    = 36.0f;
    s.rounding       = 8.0f;
    s.border_width   = 1.0f;
    s.font_size      = 16.0f;
    return s;
}

// Catppuccin Mocha — https://github.com/catppuccin/catppuccin
Style catppuccin_mocha_style() {
    Style s;
    // Base #1e1e2e, Mantle #181825, Crust #11111b
    // Surface0 #313244, Surface1 #45475a
    // Text #cdd6f4, Subtext1 #bac2de
    // Blue #89b4fa, Overlay0 #6c7086
    s.background    = {0.118f, 0.118f, 0.180f, 1.0f}; // base
    s.panel         = {0.192f, 0.196f, 0.267f, 1.0f}; // surface0
    s.text          = {0.804f, 0.839f, 0.957f, 1.0f}; // text
    s.text_dim      = {0.729f, 0.761f, 0.871f, 1.0f}; // subtext1
    s.border        = {0.271f, 0.278f, 0.353f, 1.0f}; // surface1
    s.button        = {0.192f, 0.196f, 0.267f, 1.0f}; // surface0
    s.button_hover  = {0.271f, 0.278f, 0.353f, 1.0f}; // surface1
    s.button_active = {0.341f, 0.349f, 0.431f, 1.0f}; // surface2
    s.input_bg      = {0.149f, 0.149f, 0.220f, 1.0f}; // mantle-ish
    s.input_focus   = {0.537f, 0.706f, 0.980f, 1.0f}; // blue
    s.window_padding = 20.0f;
    s.item_spacing   = 10.0f;
    s.item_height    = 36.0f;
    s.rounding       = 8.0f;
    s.border_width   = 1.0f;
    s.font_size      = 16.0f;
    return s;
}

// Nord — https://www.nordtheme.com
Style nord_style() {
    Style s;
    // Polar Night: #2e3440 #3b4252 #434c5e #4c566a
    // Snow Storm:  #d8dee9 #e5e9f0 #eceff4
    // Frost:       #8fbcbb #88c0d0 #81a1c1 #5e81ac
    s.background    = {0.180f, 0.204f, 0.251f, 1.0f}; // #2e3440
    s.panel         = {0.231f, 0.259f, 0.322f, 1.0f}; // #3b4252
    s.text          = {0.847f, 0.871f, 0.914f, 1.0f}; // #d8dee9
    s.text_dim      = {0.596f, 0.635f, 0.702f, 1.0f}; // #4c566a lightened
    s.border        = {0.263f, 0.298f, 0.369f, 1.0f}; // #434c5e
    s.button        = {0.231f, 0.259f, 0.322f, 1.0f}; // #3b4252
    s.button_hover  = {0.263f, 0.298f, 0.369f, 1.0f}; // #434c5e
    s.button_active = {0.298f, 0.337f, 0.416f, 1.0f}; // #4c566a
    s.input_bg      = {0.200f, 0.227f, 0.282f, 1.0f};
    s.input_focus   = {0.533f, 0.753f, 0.816f, 1.0f}; // #88c0d0
    s.window_padding = 20.0f;
    s.item_spacing   = 10.0f;
    s.item_height    = 36.0f;
    s.rounding       = 6.0f;
    s.border_width   = 1.0f;
    s.font_size      = 16.0f;
    return s;
}

// Gruvbox Dark — https://github.com/morhetz/gruvbox
Style gruvbox_dark_style() {
    Style s;
    // bg #282828, bg1 #3c3836, bg2 #504945, bg3 #665c54, bg4 #7c6f64
    // fg #ebdbb2, fg1 #d5c4a1, fg2 #bdae93
    // accent yellow #d79921, orange #d65d0e, blue #458588
    s.background    = {0.157f, 0.157f, 0.157f, 1.0f}; // #282828
    s.panel         = {0.235f, 0.220f, 0.212f, 1.0f}; // #3c3836
    s.text          = {0.922f, 0.859f, 0.698f, 1.0f}; // #ebdbb2
    s.text_dim      = {0.835f, 0.769f, 0.576f, 1.0f}; // #d5c4a1
    s.border        = {0.314f, 0.294f, 0.282f, 1.0f}; // #504945
    s.button        = {0.235f, 0.220f, 0.212f, 1.0f}; // #3c3836
    s.button_hover  = {0.314f, 0.294f, 0.282f, 1.0f}; // #504945
    s.button_active = {0.400f, 0.373f, 0.329f, 1.0f}; // #665c54
    s.input_bg      = {0.196f, 0.188f, 0.188f, 1.0f};
    s.input_focus   = {0.271f, 0.522f, 0.533f, 1.0f}; // #458588
    s.window_padding = 20.0f;
    s.item_spacing   = 10.0f;
    s.item_height    = 36.0f;
    s.rounding       = 4.0f;
    s.border_width   = 1.0f;
    s.font_size      = 16.0f;
    return s;
}

// One Dark — Atom editor palette
Style one_dark_style() {
    Style s;
    // bg #282c34, panel #2c313a, line #353b45
    // text #abb2bf, text_bright #e5c07b
    // accent blue #61afef, green #98c379, red #e06c75
    s.background    = {0.157f, 0.173f, 0.204f, 1.0f}; // #282c34
    s.panel         = {0.173f, 0.192f, 0.227f, 1.0f}; // #2c313a
    s.text          = {0.671f, 0.698f, 0.749f, 1.0f}; // #abb2bf
    s.text_dim      = {0.435f, 0.467f, 0.522f, 1.0f}; // #6f7480
    s.border        = {0.208f, 0.231f, 0.271f, 1.0f}; // #353b45
    s.button        = {0.173f, 0.192f, 0.227f, 1.0f}; // #2c313a
    s.button_hover  = {0.208f, 0.231f, 0.271f, 1.0f}; // #353b45
    s.button_active = {0.247f, 0.278f, 0.329f, 1.0f}; // #404754
    s.input_bg      = {0.145f, 0.161f, 0.192f, 1.0f};
    s.input_focus   = {0.380f, 0.686f, 0.937f, 1.0f}; // #61afef
    s.window_padding = 20.0f;
    s.item_spacing   = 10.0f;
    s.item_height    = 36.0f;
    s.rounding       = 6.0f;
    s.border_width   = 1.0f;
    s.font_size      = 16.0f;
    return s;
}

void set_style(const Style& style) { internal::g_style = style; }
const Style& get_style()           { return internal::g_style; }
DebugState& debug()                { return internal::g_debug; }

namespace internal {

// ---- Color helpers -----------------------------------------

static D2D1_COLOR_F to_d2d(Color c) { return {c.r, c.g, c.b, c.a}; }

// ---- Renderer primitives -----------------------------------

static void set_brush(Color c)  { g_renderer.brush->SetColor(to_d2d(c)); }
static void clear_bg(Color c)   { g_renderer.target->Clear(to_d2d(c)); }

static D2D1_ROUNDED_RECT make_rr(Rect r, float radius) {
    D2D1_RECT_F rc = {r.x, r.y, r.x + r.w, r.y + r.h};
    return D2D1::RoundedRect(rc, radius, radius);
}

static void fill_round_rect(Rect r, float radius, Color c) {
    set_brush(c);
    g_renderer.target->FillRoundedRectangle(make_rr(r, radius), g_renderer.brush);
}

static void stroke_round_rect(Rect r, float radius, float thickness, Color c) {
    set_brush(c);
    g_renderer.target->DrawRoundedRectangle(make_rr(r, radius), g_renderer.brush, thickness);
}

static void fill_rect(Rect r, Color c) {
    set_brush(c);
    D2D1_RECT_F rc = {r.x, r.y, r.x + r.w, r.y + r.h};
    g_renderer.target->FillRectangle(rc, g_renderer.brush);
}

static void draw_line(float x0, float y0, float x1, float y1, float thickness, Color c) {
    set_brush(c);
    g_renderer.target->DrawLine({x0, y0}, {x1, y1}, g_renderer.brush, thickness);
}

static void draw_text_utf8(const char* utf8, Rect r, Color c) {
    std::wstring w = utf8_to_wide(utf8);
    if (w.empty()) return;
    set_brush(c);
    D2D1_RECT_F rc = {r.x, r.y, r.x + r.w, r.y + r.h};
    DWRITE_TRIMMING trim = {DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0};
    g_renderer.text_format->SetTrimming(&trim, nullptr);
    g_renderer.target->DrawText(w.c_str(), (UINT32)w.size(),
        g_renderer.text_format, rc, g_renderer.brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

static float measure_text_width(const char* utf8) {
    if (!utf8 || !utf8[0]) return 0.0f;
    std::wstring w = utf8_to_wide(utf8);
    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = g_renderer.dwrite_factory->CreateTextLayout(
        w.c_str(), (UINT32)w.size(), g_renderer.text_format,
        10000.0f, 1000.0f, &layout);
    if (FAILED(hr) || !layout) return 0.0f;
    DWRITE_TEXT_METRICS m;
    layout->GetMetrics(&m);
    layout->Release();
    return m.width;
}

static float text_line_height() { return g_style.font_size + 6.0f; }

// ---- Layout ------------------------------------------------

static Rect next_rect(float height) {
    Rect r = {g_ctx.cursor_x, g_ctx.cursor_y, g_ctx.content_region.w, height};
    g_ctx.cursor_y += height + g_style.item_spacing;
    return r;
}

// ---- Init / teardown helpers --------------------------------

static bool init_d2d() {
    D2D1_FACTORY_OPTIONS opts = {};
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory), &opts, (void**)&g_renderer.d2d_factory);
    if (FAILED(hr)) { dbg("ftui: D2D1CreateFactory failed\n"); return false; }

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), (IUnknown**)&g_renderer.dwrite_factory);
    if (FAILED(hr)) { dbg("ftui: DWriteCreateFactory failed\n"); return false; }

    return true;
}

static bool create_render_target() {
    RECT rc;
    GetClientRect(g_platform.hwnd, &rc);
    D2D1_SIZE_U phys_size = {(UINT32)(rc.right - rc.left), (UINT32)(rc.bottom - rc.top)};

    // Get DPI *before* creating the target so we can embed it in the properties.
    // With render target DPI == screen DPI, D2D's coordinate space becomes logical
    // DIPs — matching g_platform.width/height and the converted mouse coords.
    // (At 96 DPI the ratio is 1:1 so nothing changes on non-scaled displays.)
    UINT dpi = GetDpiForWindow(g_platform.hwnd);
    if (dpi == 0) dpi = 96;
    g_renderer.dpi_scale = (float)dpi / 96.0f;

    // Logical (DIP) dimensions for layout
    g_platform.width  = (int)(phys_size.width  / g_renderer.dpi_scale + 0.5f);
    g_platform.height = (int)(phys_size.height / g_renderer.dpi_scale + 0.5f);

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN),
        (float)dpi, (float)dpi);  // screen DPI → D2D coords == logical DIPs
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props =
        D2D1::HwndRenderTargetProperties(g_platform.hwnd, phys_size);

    HRESULT hr = g_renderer.d2d_factory->CreateHwndRenderTarget(
        props, hwnd_props, &g_renderer.target);
    if (FAILED(hr)) { dbg("ftui: CreateHwndRenderTarget failed\n"); return false; }

    hr = g_renderer.target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &g_renderer.brush);
    if (FAILED(hr)) { dbg("ftui: CreateSolidColorBrush failed\n"); return false; }

    // ClearType with natural symmetric rendering for maximum sharpness
    g_renderer.target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    IDWriteRenderingParams* default_params = nullptr;
    g_renderer.dwrite_factory->CreateRenderingParams(&default_params);
    if (default_params) {
        g_renderer.dwrite_factory->CreateCustomRenderingParams(
            default_params->GetGamma(),
            default_params->GetEnhancedContrast(),
            default_params->GetClearTypeLevel(),
            default_params->GetPixelGeometry(),
            DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC,
            &g_renderer.rendering_params);
        default_params->Release();
    }
    if (g_renderer.rendering_params)
        g_renderer.target->SetTextRenderingParams(g_renderer.rendering_params);

    return true;
}

static bool create_text_format() {
    if (g_renderer.text_format) {
        g_renderer.text_format->Release();
        g_renderer.text_format = nullptr;
    }
    HRESULT hr = g_renderer.dwrite_factory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        g_style.font_size,
        L"en-us",
        &g_renderer.text_format);
    if (FAILED(hr)) { dbg("ftui: CreateTextFormat failed\n"); return false; }
    g_renderer.text_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    g_renderer.text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g_renderer.text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    return true;
}

// Draw the FTUI logo geometry onto dc_rt at the given size.
// Matches ftui.svg: black bg, white upward triangle, orange diagonal + border, "FT"/"UI" at ±45°.
static HICON make_ftui_icon(int size) {
    if (!g_renderer.d2d_factory || !g_renderer.dwrite_factory) return nullptr;

    HDC screen_dc = GetDC(nullptr);
    HDC hdc = CreateCompatibleDC(screen_dc);
    ReleaseDC(nullptr, screen_dc);

    // 32bpp DIB with alpha channel
    BITMAPV5HEADER bmi = {};
    bmi.bV5Size        = sizeof(BITMAPV5HEADER);
    bmi.bV5Width       = size;
    bmi.bV5Height      = -size; // top-down
    bmi.bV5Planes      = 1;
    bmi.bV5BitCount    = 32;
    bmi.bV5Compression = BI_BITFIELDS;
    bmi.bV5RedMask     = 0x00FF0000;
    bmi.bV5GreenMask   = 0x0000FF00;
    bmi.bV5BlueMask    = 0x000000FF;
    bmi.bV5AlphaMask   = 0xFF000000;
    void* bits = nullptr;
    HBITMAP hbm = CreateDIBSection(hdc, (BITMAPINFO*)&bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbm) { DeleteDC(hdc); return nullptr; }
    HGDIOBJ old_obj = SelectObject(hdc, hbm);

    D2D1_RENDER_TARGET_PROPERTIES rt_props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ID2D1DCRenderTarget* dc_rt = nullptr;
    g_renderer.d2d_factory->CreateDCRenderTarget(&rt_props, &dc_rt);
    if (!dc_rt) { SelectObject(hdc, old_obj); DeleteObject(hbm); DeleteDC(hdc); return nullptr; }

    RECT bind_rc = {0, 0, size, size};
    dc_rt->BindDC(hdc, &bind_rc);

    ID2D1SolidColorBrush* br = nullptr;
    dc_rt->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1), &br);

    const float s      = (float)size;
    const float margin = s * 0.1114f;
    const float sw     = fmaxf(1.0f, s / 56.0f);
    const D2D1_COLOR_F orange = {1.0f, 0.510f, 0.0f, 1.0f}; // #FF8200

    dc_rt->BeginDraw();
    dc_rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));

    // White upward-pointing triangle
    {
        ID2D1PathGeometry* tri = nullptr;
        g_renderer.d2d_factory->CreatePathGeometry(&tri);
        ID2D1GeometrySink* sink = nullptr;
        tri->Open(&sink);
        sink->BeginFigure({s * 0.5f, margin}, D2D1_FIGURE_BEGIN_FILLED);
        D2D1_POINT_2F pts[2] = {{s - margin, s - margin}, {margin, s - margin}};
        sink->AddLines(pts, 2);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close(); sink->Release();
        br->SetColor(D2D1::ColorF(1, 1, 1, 1));
        dc_rt->FillGeometry(tri, br);
        tri->Release();
    }

    // Orange diagonal: top-left to bottom-right of inner square
    br->SetColor(orange);
    dc_rt->DrawLine({margin, margin}, {s - margin, s - margin}, br, sw * 1.5f);

    // Orange border square
    {
        D2D1_RECT_F border = {margin, margin, s - margin, s - margin};
        dc_rt->DrawRectangle(border, br, sw);
    }

    // "FT" (rotated +45°) and "UI" (rotated -45°) in orange
    // Try the SVG font first, fall back to bold sans-serif
    if (size >= 24) {
        const float font_size = s * 0.28f;
        IDWriteTextFormat* fmt = nullptr;
        const wchar_t* fonts[] = {L"Berlin Sans FB Demi", L"Trebuchet MS", L"Arial"};
        for (auto* fn : fonts) {
            if (SUCCEEDED(g_renderer.dwrite_factory->CreateTextFormat(
                    fn, nullptr, DWRITE_FONT_WEIGHT_BOLD,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    font_size, L"en-us", &fmt)))
                break;
        }
        if (fmt) {
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            br->SetColor(orange);

            D2D1_POINT_2F center = {s * 0.5f, s * 0.5f};

            // "FT" — upper region, rotated +45° around center
            dc_rt->SetTransform(D2D1::Matrix3x2F::Rotation(45.0f, center));
            {
                D2D1_RECT_F r = {0.0f, s * 0.08f, s, s * 0.52f};
                dc_rt->DrawText(L"FT", 2, fmt, r, br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            // "UI" — lower region, rotated -45° around center
            dc_rt->SetTransform(D2D1::Matrix3x2F::Rotation(-45.0f, center));
            {
                D2D1_RECT_F r = {0.0f, s * 0.48f, s, s * 0.92f};
                dc_rt->DrawText(L"UI", 2, fmt, r, br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            dc_rt->SetTransform(D2D1::Matrix3x2F::Identity());
            fmt->Release();
        }
    }

    dc_rt->EndDraw();
    br->Release();
    dc_rt->Release();

    // AND mask all-zeros → alpha from the color bitmap drives transparency
    int mask_stride = ((size + 31) / 32) * 4;
    std::vector<BYTE> mask_bits(mask_stride * size, 0);
    HBITMAP hbm_mask = CreateBitmap(size, size, 1, 1, mask_bits.data());
    ICONINFO ii = {TRUE, 0, 0, hbm_mask, hbm};
    HICON icon = CreateIconIndirect(&ii);

    SelectObject(hdc, old_obj);
    DeleteObject(hbm_mask);
    DeleteObject(hbm);
    DeleteDC(hdc);
    return icon;
}

static void release_render_target() {
    if (g_renderer.rendering_params) { g_renderer.rendering_params->Release(); g_renderer.rendering_params = nullptr; }
    if (g_renderer.brush)            { g_renderer.brush->Release();            g_renderer.brush = nullptr; }
    if (g_renderer.target)           { g_renderer.target->Release();           g_renderer.target = nullptr; }
}

// ---- WndProc -----------------------------------------------

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY:
        g_platform.running = false;
        PostQuitMessage(0);
        return 0;

    case WM_SIZE: {
        // lp gives physical pixels; convert to logical DIPs
        int phys_w = LOWORD(lp), phys_h = HIWORD(lp);
        g_platform.width  = (int)(phys_w / g_renderer.dpi_scale + 0.5f);
        g_platform.height = (int)(phys_h / g_renderer.dpi_scale + 0.5f);
        if (g_renderer.target && phys_w > 0 && phys_h > 0) {
            D2D1_SIZE_U s = {(UINT32)phys_w, (UINT32)phys_h};
            g_renderer.target->Resize(s);
        }
        return 0;
    }

    case WM_DPICHANGED: {
        UINT new_dpi = HIWORD(wp);
        g_renderer.dpi_scale = (float)new_dpi / 96.0f;
        if (g_renderer.target)
            g_renderer.target->SetDpi((float)new_dpi, (float)new_dpi);
        const RECT* r = (const RECT*)lp;
        SetWindowPos(hwnd, nullptr,
            r->left, r->top, r->right - r->left, r->bottom - r->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_MOUSEMOVE:
        g_input.mouse_x = GET_X_LPARAM(lp) / g_renderer.dpi_scale;
        g_input.mouse_y = GET_Y_LPARAM(lp) / g_renderer.dpi_scale;
        return 0;

    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        g_input.mouse_down    = true;
        g_input.mouse_pressed = true;
        g_input.mouse_x = GET_X_LPARAM(lp) / g_renderer.dpi_scale;
        g_input.mouse_y = GET_Y_LPARAM(lp) / g_renderer.dpi_scale;
        return 0;

    case WM_LBUTTONUP:
        ReleaseCapture();
        g_input.mouse_down     = false;
        g_input.mouse_released = true;
        g_input.mouse_x = GET_X_LPARAM(lp) / g_renderer.dpi_scale;
        g_input.mouse_y = GET_Y_LPARAM(lp) / g_renderer.dpi_scale;
        return 0;

    case WM_CHAR: {
        wchar_t wc = (wchar_t)wp;
        if (wc == L'\b') {
            g_input.key_backspace = true;
        } else if (wc == L'\r' || wc == L'\n') {
            g_input.key_enter = true;
        } else if (wc >= 32) {
            char buf[8] = {};
            int n = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, buf, sizeof(buf)-1, nullptr, nullptr);
            if (n > 0 && g_input.text_input_count + n < (int)sizeof(g_input.text_input)) {
                memcpy(g_input.text_input + g_input.text_input_count, buf, n);
                g_input.text_input_count += n;
            }
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_BACK)   g_input.key_backspace = true;
        if (wp == VK_RETURN) g_input.key_enter     = true;
        return 0;

    case WM_SETFOCUS:
        g_input.focused = true;
        return 0;

    case WM_KILLFOCUS:
        g_input.focused        = false;
        g_ctx.focused_input_id = 0;
        return 0;

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

} // namespace internal

// ---- Public lifecycle ---------------------------------------

bool create_window(const Config& cfg) {
    using namespace internal;

    // COM is needed for file dialogs and WIC image loading
    HRESULT com_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_renderer.com_inited = (com_hr == S_OK || com_hr == S_FALSE);

    // Enable per-monitor DPI awareness before window creation
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    g_style = default_dark_style();
    g_platform.instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = g_platform.instance;
    wc.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"FTUI_Window";
    wc.hIcon         = cfg.icon ? cfg.icon : LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    wc.hIconSm       = wc.hIcon;
    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            dbg("ftui: RegisterClassExW failed\n");
            return false;
        }
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!cfg.resizable) style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

    // Use AdjustWindowRectExForDpi if available (Windows 10+); fall back gracefully
    RECT rc = {0, 0, cfg.width, cfg.height};
    using AdjustFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    auto adjust_dpi = (AdjustFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi");
    if (adjust_dpi) {
        // Use system DPI as best estimate before window exists
        UINT sys_dpi = GetDpiForSystem();
        adjust_dpi(&rc, style, FALSE, 0, sys_dpi);
    } else {
        AdjustWindowRect(&rc, style, FALSE);
    }
    int w = rc.right - rc.left, h = rc.bottom - rc.top;

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (cfg.center_window) {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        x = (sw - w) / 2;
        y = (sh - h) / 2;
    }

    g_platform.hwnd = CreateWindowExW(0, L"FTUI_Window", cfg.title, style,
        x, y, w, h, nullptr, nullptr, g_platform.instance, nullptr);
    if (!g_platform.hwnd) { dbg("ftui: CreateWindowExW failed\n"); return false; }

    g_platform.width   = cfg.width;
    g_platform.height  = cfg.height;
    g_platform.running = true;

    if (!init_d2d())             return false;
    if (!create_render_target()) return false;  // also sets dpi_scale + logical width/height
    if (!create_text_format())   return false;

    // Apply icon — use the one from Config, or generate the FTUI logo geometry
    {
        HICON icon = cfg.icon ? cfg.icon : make_ftui_icon(256);
        HICON icon_sm = cfg.icon ? cfg.icon : make_ftui_icon(32);
        SendMessageW(g_platform.hwnd, WM_SETICON, ICON_BIG,   (LPARAM)icon);
        SendMessageW(g_platform.hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon_sm);
    }

    ShowWindow(g_platform.hwnd, SW_SHOW);
    UpdateWindow(g_platform.hwnd);

    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_last_time);

    return true;
}

bool pump() {
    using namespace internal;

    g_input.mouse_pressed    = false;
    g_input.mouse_released   = false;
    g_input.key_backspace    = false;
    g_input.key_enter        = false;
    g_input.text_input_count = 0;
    memset(g_input.text_input, 0, sizeof(g_input.text_input));

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { g_platform.running = false; return false; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return g_platform.running;
}

void begin() {
    using namespace internal;

    g_ctx.hot_id = 0;

    float pad = g_style.window_padding;
    g_ctx.content_region = {pad, pad,
        (float)g_platform.width  - 2.0f * pad,
        (float)g_platform.height - 2.0f * pad};
    g_ctx.cursor_x = g_ctx.content_region.x;
    g_ctx.cursor_y = g_ctx.content_region.y;

    // FPS tracking
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = (float)(now.QuadPart - g_last_time.QuadPart) / (float)g_freq.QuadPart;
    g_last_time = now;
    g_fps_accum += dt;
    g_fps_frames++;
    if (g_fps_accum >= 0.5f) {
        g_fps = (float)g_fps_frames / g_fps_accum;
        g_fps_frames = 0;
        g_fps_accum  = 0.0f;
    }

    if (!g_renderer.target) return;
    g_renderer.target->BeginDraw();
    g_renderer.drawing = true;
    clear_bg(g_style.background);
}

void end() {
    using namespace internal;
    if (!g_renderer.drawing) return;

    // Debug overlays (drawn last, on top)
    if (g_debug.show_fps) {
        char buf[64];
        snprintf(buf, sizeof(buf), "FPS: %.0f  frame: %d  DPI: %.0f%%",
            g_fps, g_ctx.frame_index, g_renderer.dpi_scale * 100.0f);
        float lh = text_line_height();
        Rect r = {4, 4, 280, lh};
        fill_rect(r, {0, 0, 0, 0.6f});
        draw_text_utf8(buf, r, g_style.text_dim);
    }

    if (g_debug.show_hovered_id || g_debug.show_active_id) {
        char buf[128];
        snprintf(buf, sizeof(buf), "hot=%d  active=%d  focused_input=%d",
            g_ctx.hot_id, g_ctx.active_id, g_ctx.focused_input_id);
        float lh = text_line_height();
        float oy = g_debug.show_fps ? lh + 6.0f : 4.0f;
        Rect r = {4, oy, 360, lh};
        fill_rect(r, {0, 0, 0, 0.6f});
        draw_text_utf8(buf, r, g_style.text_dim);
    }

    HRESULT hr = g_renderer.target->EndDraw();
    g_renderer.drawing = false;

    if (hr == D2DERR_RECREATE_TARGET) {
        release_render_target();
        create_render_target();
    }

    g_ctx.frame_index++;
}

void shutdown() {
    using namespace internal;

    release_render_target();
    if (g_renderer.wic_factory)    { g_renderer.wic_factory->Release();    g_renderer.wic_factory = nullptr; }
    if (g_renderer.text_format)    { g_renderer.text_format->Release();    g_renderer.text_format = nullptr; }
    if (g_renderer.dwrite_factory) { g_renderer.dwrite_factory->Release(); g_renderer.dwrite_factory = nullptr; }
    if (g_renderer.d2d_factory)    { g_renderer.d2d_factory->Release();    g_renderer.d2d_factory = nullptr; }
    if (g_platform.hwnd)           { DestroyWindow(g_platform.hwnd);       g_platform.hwnd = nullptr; }
    if (g_renderer.com_inited)     { CoUninitialize(); g_renderer.com_inited = false; }
}

// ============================================================
// Widgets
// ============================================================

void text(const char* label) {
    using namespace internal;
    if (!g_renderer.drawing) return;

    float h = text_line_height();
    Rect r = next_rect(h);

    if (g_debug.show_layout_rects) stroke_round_rect(r, 0, 1.0f, {0.2f, 0.8f, 0.2f, 0.5f});
    if (g_debug.log_widget_calls) dbg("[ftui] text id=%d \"%s\" %.0f,%.0f %.0fx%.0f\n",
        hash_str(label), label, r.x, r.y, r.w, r.h);

    draw_text_utf8(label, r, g_style.text);
}

void separator() {
    using namespace internal;
    if (!g_renderer.drawing) return;

    Rect r = next_rect(9.0f);
    if (g_debug.show_layout_rects) stroke_round_rect(r, 0, 1.0f, {0.2f, 0.8f, 0.2f, 0.5f});
    float mid_y = r.y + r.h * 0.5f;
    draw_line(r.x, mid_y, r.x + r.w, mid_y, 1.0f, g_style.border);
}

void spacing(float px) {
    using namespace internal;
    if (!g_renderer.drawing) return;
    g_ctx.cursor_y += px;
}

bool button(const char* label) {
    using namespace internal;
    if (!g_renderer.drawing) return false;

    char visible[256]; const char* hash_src;
    split_label(label, visible, sizeof(visible), &hash_src);
    int id = hash_str(hash_src);

    Rect r = next_rect(g_style.item_height);
    bool hovered = rect_contains(r, g_input.mouse_x, g_input.mouse_y);
    if (hovered) g_ctx.hot_id = id;
    if (hovered && g_input.mouse_pressed) g_ctx.active_id = id;

    bool clicked = false;
    if (g_ctx.active_id == id && g_input.mouse_released) {
        if (hovered) clicked = true;
        g_ctx.active_id = 0;
    }

    Color bg = (g_ctx.active_id == id && hovered) ? g_style.button_active
             : hovered                             ? g_style.button_hover
             :                                       g_style.button;

    fill_round_rect(r, g_style.rounding, bg);
    stroke_round_rect(r, g_style.rounding, g_style.border_width, g_style.border);

    Rect text_r = {r.x + 8, r.y, r.w - 16, r.h};
    g_renderer.text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    draw_text_utf8(visible, text_r, g_style.text);
    g_renderer.text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    if (g_debug.show_layout_rects) stroke_round_rect(r, g_style.rounding, 1.0f, {0.8f, 0.2f, 0.2f, 0.7f});
    if (g_debug.log_widget_calls) dbg("[ftui] button id=%d \"%s\" clicked=%d\n", id, label, clicked);

    return clicked;
}

bool input(const char* label, char* buffer, int buffer_size, InputFlags flags) {
    using namespace internal;
    if (!g_renderer.drawing) return false;

    char visible[256]; const char* hash_src;
    split_label(label, visible, sizeof(visible), &hash_src);
    int id = hash_str(hash_src);

    bool is_password = (flags & InputFlags::Password);
    bool is_readonly = (flags & InputFlags::ReadOnly);

    float label_h = text_line_height();
    float box_h   = g_style.item_height;
    float start_y = g_ctx.cursor_y;
    g_ctx.cursor_y += label_h + 4.0f + box_h + g_style.item_spacing;

    Rect label_r = {g_ctx.cursor_x, start_y,                     g_ctx.content_region.w, label_h};
    Rect box_r   = {g_ctx.cursor_x, start_y + label_h + 4.0f,   g_ctx.content_region.w, box_h};

    bool hovered = rect_contains(box_r, g_input.mouse_x, g_input.mouse_y);
    if (g_input.mouse_pressed) {
        if (hovered)                              g_ctx.focused_input_id = id;
        else if (g_ctx.focused_input_id == id)    g_ctx.focused_input_id = 0;
    }

    bool focused = (g_ctx.focused_input_id == id);
    bool changed = false;

    if (focused && !is_readonly) {
        if (g_input.key_backspace) {
            int len = (int)strlen(buffer);
            if (len > 0) {
                int i = len - 1;
                while (i > 0 && (buffer[i] & 0xC0) == 0x80) --i;
                buffer[i] = '\0';
                changed = true;
            }
        }
        if (g_input.text_input_count > 0) {
            int len   = (int)strlen(buffer);
            int space = buffer_size - 1 - len;
            int n     = g_input.text_input_count < space ? g_input.text_input_count : space;
            if (n > 0) {
                memcpy(buffer + len, g_input.text_input, n);
                buffer[len + n] = '\0';
                changed = true;
            }
        }
    }

    // Label
    draw_text_utf8(visible, label_r, g_style.text_dim);

    // Box
    Color border_c = focused ? g_style.input_focus : g_style.border;
    fill_round_rect(box_r, g_style.rounding, g_style.input_bg);
    stroke_round_rect(box_r, g_style.rounding,
        focused ? 1.5f : g_style.border_width, border_c);

    // Text inside box
    Rect text_r = {box_r.x + 10, box_r.y, box_r.w - 20, box_r.h};
    if (buffer[0]) {
        if (is_password) {
            int chars = 0;
            for (int i = 0, len = (int)strlen(buffer); i < len; ) {
                unsigned char c = (unsigned char)buffer[i];
                i += (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                chars++;
            }
            std::string dots(chars, '\xe2'); // use bullet-ish: just dots for simplicity
            std::string mask(chars, '*');
            draw_text_utf8(mask.c_str(), text_r, g_style.text);
        } else {
            draw_text_utf8(buffer, text_r, g_style.text);
        }
    }

    // Blinking caret
    if (focused) {
        bool caret_on = ((g_ctx.frame_index / 30) & 1) == 0;
        if (caret_on) {
            const char* disp_buf = buffer;
            std::string mask;
            if (is_password) {
                int chars = 0;
                for (int i = 0, len = (int)strlen(buffer); i < len; ) {
                    unsigned char c = (unsigned char)buffer[i];
                    i += (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                    chars++;
                }
                mask = std::string(chars, '*');
                disp_buf = mask.c_str();
            }
            float tw = measure_text_width(disp_buf);
            float cx = text_r.x + tw + 1.0f;
            draw_line(cx, box_r.y + 6.0f, cx, box_r.y + box_r.h - 6.0f, 1.5f, g_style.text);
        }
    }

    if (g_debug.show_layout_rects) {
        stroke_round_rect(box_r,   g_style.rounding, 1.0f, {0.2f, 0.2f, 0.9f, 0.7f});
        stroke_round_rect(label_r, 0,                1.0f, {0.2f, 0.8f, 0.2f, 0.5f});
    }
    if (g_debug.log_widget_calls) dbg("[ftui] input id=%d \"%s\" focused=%d changed=%d\n",
        id, label, focused, changed);

    return changed;
}

bool checkbox(const char* label, bool* value) {
    using namespace internal;
    if (!g_renderer.drawing || !value) return false;

    char visible[256]; const char* hash_src;
    split_label(label, visible, sizeof(visible), &hash_src);
    int id = hash_str(hash_src);

    float h = g_style.item_height;
    Rect r = next_rect(h);

    float box_size = h - 12.0f;
    Rect box_r  = {r.x, r.y + (h - box_size) * 0.5f, box_size, box_size};
    Rect text_r = {r.x + box_size + 10.0f, r.y, r.w - box_size - 10.0f, h};

    bool hovered = rect_contains(r, g_input.mouse_x, g_input.mouse_y);
    if (hovered) g_ctx.hot_id = id;
    if (hovered && g_input.mouse_pressed) g_ctx.active_id = id;

    bool changed = false;
    if (g_ctx.active_id == id && g_input.mouse_released) {
        if (hovered) { *value = !*value; changed = true; }
        g_ctx.active_id = 0;
    }

    fill_round_rect(box_r, 4.0f, hovered ? g_style.button_hover : g_style.input_bg);
    stroke_round_rect(box_r, 4.0f, g_style.border_width,
        *value ? g_style.input_focus : g_style.border);

    if (*value) {
        float m = 4.0f;
        Rect tick = {box_r.x + m, box_r.y + m, box_r.w - 2*m, box_r.h - 2*m};
        fill_round_rect(tick, 2.0f, g_style.input_focus);
    }

    draw_text_utf8(visible, text_r, g_style.text);

    if (g_debug.show_layout_rects) stroke_round_rect(r, 0, 1.0f, {0.8f, 0.8f, 0.2f, 0.5f});
    return changed;
}

bool slider_float(const char* label, float* value, float min_v, float max_v) {
    using namespace internal;
    if (!g_renderer.drawing || !value) return false;

    char visible[256]; const char* hash_src;
    split_label(label, visible, sizeof(visible), &hash_src);
    int id = hash_str(hash_src);

    float label_h = text_line_height();
    float box_h   = g_style.item_height;
    float start_y = g_ctx.cursor_y;
    g_ctx.cursor_y += label_h + 4.0f + box_h + g_style.item_spacing;

    Rect label_r = {g_ctx.cursor_x, start_y,                   g_ctx.content_region.w, label_h};
    Rect track_r = {g_ctx.cursor_x, start_y + label_h + 4.0f, g_ctx.content_region.w, box_h};

    bool hovered = rect_contains(track_r, g_input.mouse_x, g_input.mouse_y);
    if (hovered) g_ctx.hot_id = id;
    if (hovered && g_input.mouse_pressed) g_ctx.active_id = id;

    bool changed = false;
    if (g_ctx.active_id == id) {
        if (g_input.mouse_down || g_input.mouse_released) {
            float t = (g_input.mouse_x - track_r.x) / track_r.w;
            t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
            float nv = min_v + t * (max_v - min_v);
            if (nv != *value) { *value = nv; changed = true; }
        }
        if (g_input.mouse_released) g_ctx.active_id = 0;
    }

    float t = (max_v > min_v) ? (*value - min_v) / (max_v - min_v) : 0.0f;

    float ty = track_r.y + track_r.h * 0.5f - 3.0f;
    Rect track_bg   = {track_r.x, ty, track_r.w,     6.0f};
    Rect track_fill = {track_r.x, ty, track_r.w * t, 6.0f};
    fill_round_rect(track_bg,   3.0f, g_style.input_bg);
    stroke_round_rect(track_bg, 3.0f, g_style.border_width, g_style.border);
    if (track_fill.w > 0) fill_round_rect(track_fill, 3.0f, g_style.input_focus);

    float tx = track_r.x + track_r.w * t - 9.0f;
    float tty = track_r.y + track_r.h * 0.5f - 9.0f;
    Rect thumb = {tx, tty, 18.0f, 18.0f};
    fill_round_rect(thumb, 9.0f, hovered ? g_style.button_hover : g_style.panel);
    stroke_round_rect(thumb, 9.0f, g_style.border_width,
        (g_ctx.active_id == id) ? g_style.input_focus : g_style.border);

    char lbuf[256];
    snprintf(lbuf, sizeof(lbuf), "%s  %.2f", visible, *value);
    draw_text_utf8(lbuf, label_r, g_style.text_dim);

    if (g_debug.show_layout_rects) stroke_round_rect(track_r, 0, 1.0f, {0.8f, 0.2f, 0.8f, 0.5f});
    return changed;
}

void image(ImageHandle* img, float width, float height) {
    using namespace internal;
    if (!g_renderer.drawing) return;

    Rect r = next_rect(height);
    if (width < r.w) { r.x += (r.w - width) * 0.5f; r.w = width; }

    if (img && img->bitmap) {
        D2D1_RECT_F dst = {r.x, r.y, r.x + r.w, r.y + r.h};
        g_renderer.target->DrawBitmap(img->bitmap, dst);
    } else {
        fill_round_rect(r, g_style.rounding, g_style.panel);
        stroke_round_rect(r, g_style.rounding, g_style.border_width, g_style.border);
        Rect tr = {r.x, r.y + height * 0.5f - text_line_height() * 0.5f, r.w, text_line_height()};
        g_renderer.text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        draw_text_utf8("image", tr, g_style.text_dim);
        g_renderer.text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    if (g_debug.show_layout_rects) stroke_round_rect(r, g_style.rounding, 1.0f, {0.5f, 0.5f, 0.9f, 0.7f});
}

// ============================================================
// Image loading
// ============================================================

ImageHandle* load_image(const char* utf8_path) {
    using namespace internal;
    if (!g_renderer.target || !utf8_path || !utf8_path[0]) return nullptr;

    // Lazily create WIC factory
    if (!g_renderer.wic_factory) {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_renderer.wic_factory));
        if (FAILED(hr)) { dbg("ftui: WICImagingFactory failed\n"); return nullptr; }
    }

    std::wstring wpath = internal::utf8_to_wide(utf8_path);

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = g_renderer.wic_factory->CreateDecoderFromFilename(
        wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) { dbg("ftui: load_image — cannot open \"%s\"\n", utf8_path); return nullptr; }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    decoder->Release();
    if (FAILED(hr)) return nullptr;

    // Convert to 32bpp pre-multiplied BGRA (D2D's native pixel format)
    IWICFormatConverter* converter = nullptr;
    g_renderer.wic_factory->CreateFormatConverter(&converter);
    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut);
    frame->Release();
    if (FAILED(hr)) { converter->Release(); return nullptr; }

    ID2D1Bitmap* bitmap = nullptr;
    hr = g_renderer.target->CreateBitmapFromWicBitmap(converter, nullptr, &bitmap);
    converter->Release();
    if (FAILED(hr)) { dbg("ftui: CreateBitmapFromWicBitmap failed\n"); return nullptr; }

    ImageHandle* handle = new ImageHandle();
    handle->bitmap = bitmap;
    return handle;
}

void free_image(ImageHandle* img) {
    if (!img) return;
    if (img->bitmap) { img->bitmap->Release(); img->bitmap = nullptr; }
    delete img;
}

// ============================================================
// File dialog
// ============================================================

std::string open_file_dialog(const wchar_t* title, const FileFilter* filters, int filter_count) {
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) return "";

    if (title) dialog->SetTitle(title);

    if (filters && filter_count > 0) {
        std::vector<COMDLG_FILTERSPEC> specs(filter_count);
        for (int i = 0; i < filter_count; i++) {
            specs[i].pszName = filters[i].name;
            specs[i].pszSpec = filters[i].spec;
        }
        dialog->SetFileTypes((UINT)filter_count, specs.data());
        dialog->SetFileTypeIndex(1);
    }

    std::string result;
    hr = dialog->Show(internal::g_platform.hwnd);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR wpath = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wpath))) {
                int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
                if (len > 1) {
                    result.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, &result[0], len, nullptr, nullptr);
                }
                CoTaskMemFree(wpath);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

} // namespace ftui

#endif // FTUI_IMPLEMENTATION
