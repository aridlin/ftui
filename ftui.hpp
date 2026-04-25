// ftui.hpp — Tiny immediate-mode GUI for Windows and Linux
// Single-header, no external dependencies beyond platform SDKs.
//
// Usage: #define FTUI_IMPLEMENTATION in exactly one .cpp before including.
//
// Windows: cl main.cpp /link d2d1.lib dwrite.lib gdi32.lib ole32.lib uuid.lib user32.lib
//          (MSVC auto-links via #pragma comment)
// Linux:   g++ main.cpp -o app -DFTUI_IMPLEMENTATION \
//            $(pkg-config --cflags --libs cairo x11) -std=c++17
//
// Optional defines before FTUI_IMPLEMENTATION:
//   FTUI_KEEP_CONSOLE    — keep console window on Windows
//   FTUI_LINUX_FONT "x"  — override default "sans-serif" on Linux

#pragma once
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <string>
#include <vector>
#include <functional>

// ============================================================
// Public API
// ============================================================

namespace ftui {

struct Color { float r, g, b, a; };

struct Style {
    Color background, panel, text, text_dim, border;
    Color button, button_hover, button_active;
    Color input_bg, input_focus;
    float window_padding, item_spacing, item_height;
    float rounding, border_width, font_size;
};

Style default_dark_style();
Style catppuccin_mocha_style();
Style nord_style();
Style gruvbox_dark_style();
Style one_dark_style();

void         set_style(const Style& s);
const Style& get_style();

struct Config {
    const char* title         = "FTUI App"; // UTF-8; converted internally on Windows
    int         width         = 960;
    int         height        = 640;
    bool        resizable     = true;
    bool        center_window = true;
    void*       icon          = nullptr;    // HICON on Windows; ignored on Linux
};

bool create_window(const Config& cfg = {});
bool pump();
void begin();
void end();
void shutdown();
void set_quit_on_ctrl_q(bool enabled);

void text(const char* label);
void separator();
void spacing(float px = 8.0f);

enum class InputFlags : unsigned { Default = 0, Password = 1 << 0, ReadOnly = 1 << 1 };
inline InputFlags operator|(InputFlags a, InputFlags b) {
    return static_cast<InputFlags>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
inline bool operator&(InputFlags a, InputFlags b) {
    return (static_cast<unsigned>(a) & static_cast<unsigned>(b)) != 0;
}

bool input(const char* label, char* buffer, int buffer_size,
           InputFlags flags = InputFlags::Default, bool* enter_pressed = nullptr);

// Multi-line text input. Enter inserts newline; Tab cycles focus.
bool text_area(const char* label, char* buffer, int buffer_size, int rows = 5);

bool checkbox(const char* label, bool* value);
bool slider_float(const char* label, float* value, float min_v, float max_v);
bool button(const char* label);

// Horizontal tab bar. Returns true when selected changes.
bool tabs(const char* const* labels, int count, int* selected);

void row(int cols, std::function<void()> fn);

// Opaque image handle — platform-specific payload in _impl.
struct ImageHandle { void* _impl = nullptr; };
void         image(ImageHandle* img, float width, float height);
ImageHandle* load_image(const char* utf8_path);
void         free_image(ImageHandle* img);

// File dialog — UTF-8 strings on both platforms.
// On Linux: requires zenity; returns "" if not available or cancelled.
struct FileFilter {
    const char* name; // e.g. "PNG Images"
    const char* spec; // e.g. "*.png;*.jpg"  (Windows wildcard; Linux space-sep)
};
std::string open_file_dialog(const char* title = "Open File",
                              const FileFilter* filters = nullptr,
                              int filter_count = 0);

void open_child_window(const Config& cfg, std::function<void()> fn);

struct DebugState {
    bool show_layout_rects = false;
    bool show_hovered_id   = false;
    bool show_active_id    = false;
    bool show_fps          = false;
    bool log_widget_calls  = false;
};
DebugState& debug();

} // namespace ftui


// ============================================================
// Implementation
// ============================================================

#ifdef FTUI_IMPLEMENTATION

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <shobjidl.h>
#include <wincodec.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#ifndef FTUI_KEEP_CONSOLE
#pragma comment(linker, "/subsystem:windows /entry:mainCRTStartup")
#endif

#elif defined(__linux__)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xresource.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <time.h>
#include <unistd.h>

#endif

namespace ftui {
namespace internal {

// ---- Shared utilities -----------------------------------------------

static int hash_str(const char* s) {
    unsigned h = 2166136261u;
    for (; *s; ++s) h = (h ^ (unsigned char)*s) * 16777619u;
    return (int)(h & 0x7fffffff) + 1;
}

static void split_label(const char* label, char* visible, int vis_len, const char** hash_src) {
    const char* sep = strstr(label, "##");
    int n = sep ? (int)(sep - label) : (int)strlen(label);
    if (n >= vis_len) n = vis_len - 1;
    memcpy(visible, label, n); visible[n] = '\0';
    *hash_src = label;
}

struct Rect { float x, y, w, h; };
static bool rect_contains(Rect r, float px, float py) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

static int utf8_advance(const char* s, int pos) {
    if (!s[pos]) return pos;
    unsigned char c = (unsigned char)s[pos];
    return pos + (c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4);
}
static int utf8_retreat(const char* s, int pos) {
    if (pos == 0) return 0;
    do { --pos; } while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80);
    return pos;
}
static int utf8_char_count(const char* s, int byte_offset) {
    int n = 0;
    for (int i = 0; i < byte_offset && s[i]; ) {
        unsigned char c = (unsigned char)s[i];
        i += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; n++;
    }
    return n;
}

// ---- Shared state structs -------------------------------------------

struct InputState {
    float mouse_x = 0, mouse_y = 0;
    bool  mouse_down = false, mouse_pressed = false, mouse_released = false;
    bool  key_backspace = false, key_enter = false;
    bool  key_tab = false, key_shift_tab = false;
    bool  key_left = false, key_right = false;
    bool  key_up   = false, key_down  = false;
    bool  key_ctrl_c = false, key_ctrl_v = false;
    bool  shift_held = false, ctrl_held = false;
    char  text_input[64] = {};
    int   text_input_count = 0;
    bool  focused = true;
};

struct RowContext {
    bool  active = false; int cols = 0;
    float cell_w = 0, gap = 0, start_x = 0, start_y = 0;
    int   col_index = 0; float row_height = 0;
};

struct UIContext {
    int  hot_id = 0, active_id = 0, focused_input_id = 0, frame_index = 0;
    Rect content_region = {};
    float cursor_x = 0, cursor_y = 0;
    RowContext row_ctx;
    std::vector<int> tab_stops, tab_stops_prev;
};

struct CmdState { bool active = false; char buf[16] = {}; int len = 0; };

// ---- Shared globals -------------------------------------------------

static InputState g_input;
static UIContext  g_ctx;
static DebugState g_debug;
static Style      g_style;
static float      g_fps = 0, g_fps_accum = 0;
static int        g_fps_frames = 0;
static bool       g_shortcuts_enabled = true;
static CmdState   g_cmd;
static float      g_scroll_y = 0, g_content_height = 0;
static bool       g_sb_dragging = false;
static float      g_sb_drag_mouse_y = 0, g_sb_drag_scroll0 = 0;
static int        g_text_cursor_id = 0, g_text_cursor = 0, g_text_sel_anchor = 0;
static int        g_ta_cursor_id = 0, g_ta_cursor = 0, g_ta_sel_anchor = 0;
static float      g_ta_scroll_y = 0;
static bool       g_drawing = false;

// ---- Forward declarations of platform-specific functions -----------
// (defined in the platform blocks below; used by shared widget code)

static void        dbg(const char* fmt, ...);
static float       measure_text_width(const char* utf8);
static int         byte_from_x(const char* utf8, float rel_x);
static void        fill_round_rect(Rect r, float radius, Color c);
static void        stroke_round_rect(Rect r, float radius, float thickness, Color c);
static void        fill_rect(Rect r, Color c);
static void        draw_line(float x0, float y0, float x1, float y1, float thickness, Color c);
static void        draw_text_utf8(const char* utf8, Rect r, Color c);
static void        draw_text_utf8_centered(const char* utf8, Rect r, Color c);
static void        draw_image_handle(ImageHandle* img, Rect r);
static void        clipboard_set(const char* utf8);
static std::string clipboard_get();
static void        push_clip(Rect r);
static void        pop_clip();

// ---- Shared implementations (call platform fns above) ---------------

static float text_line_height() { return g_style.font_size + 6.0f; }

static float measure_text_at(const char* utf8, int byte_len) {
    if (byte_len <= 0 || !utf8 || !utf8[0]) return 0.0f;
    std::string sub(utf8, utf8 + byte_len);
    return measure_text_width(sub.c_str());
}

static Rect next_rect(float height) {
    if (g_ctx.row_ctx.active) {
        auto& rc = g_ctx.row_ctx;
        float x = rc.start_x + rc.col_index * (rc.cell_w + rc.gap);
        Rect r = {x, rc.start_y, rc.cell_w, height};
        rc.col_index++;
        if (height > rc.row_height) rc.row_height = height;
        return r;
    }
    Rect r = {g_ctx.cursor_x, g_ctx.cursor_y, g_ctx.content_region.w, height};
    g_ctx.cursor_y += height + g_style.item_spacing;
    return r;
}

static void cmd_clear() { g_cmd.active = false; g_cmd.len = 0; g_cmd.buf[0] = '\0'; }

static void apply_cmd_theme() {
    if      (strcmp(g_cmd.buf, "td") == 0) g_style = default_dark_style();
    else if (strcmp(g_cmd.buf, "tc") == 0) g_style = catppuccin_mocha_style();
    else if (strcmp(g_cmd.buf, "tn") == 0) g_style = nord_style();
    else if (strcmp(g_cmd.buf, "tg") == 0) g_style = gruvbox_dark_style();
    else if (strcmp(g_cmd.buf, "to") == 0) g_style = one_dark_style();
}

} // namespace internal

// ---- Public helpers (before platform split) -------------------------

void set_quit_on_ctrl_q(bool e) { internal::g_shortcuts_enabled = e; }
void set_style(const Style& s)  { internal::g_style = s; }
const Style& get_style()         { return internal::g_style; }
DebugState&  debug()             { return internal::g_debug; }

// ============================================================
// Themes
// ============================================================

Style default_dark_style() {
    Style s;
    s.background = {0.055f,0.055f,0.059f,1}; s.panel = {0.090f,0.094f,0.102f,1};
    s.text = {0.910f,0.910f,0.910f,1}; s.text_dim = {0.663f,0.678f,0.702f,1};
    s.border = {0.169f,0.176f,0.192f,1}; s.button = {0.090f,0.094f,0.102f,1};
    s.button_hover = {0.137f,0.149f,0.169f,1}; s.button_active = {0.176f,0.192f,0.220f,1};
    s.input_bg = {0.071f,0.075f,0.082f,1}; s.input_focus = {0.310f,0.420f,0.780f,1};
    s.window_padding=20; s.item_spacing=10; s.item_height=36; s.rounding=8; s.border_width=1; s.font_size=16;
    return s;
}
Style catppuccin_mocha_style() {
    Style s;
    s.background = {0.118f,0.118f,0.180f,1}; s.panel = {0.192f,0.196f,0.267f,1};
    s.text = {0.804f,0.839f,0.957f,1}; s.text_dim = {0.729f,0.761f,0.871f,1};
    s.border = {0.271f,0.278f,0.353f,1}; s.button = {0.192f,0.196f,0.267f,1};
    s.button_hover = {0.271f,0.278f,0.353f,1}; s.button_active = {0.341f,0.349f,0.431f,1};
    s.input_bg = {0.149f,0.149f,0.220f,1}; s.input_focus = {0.537f,0.706f,0.980f,1};
    s.window_padding=20; s.item_spacing=10; s.item_height=36; s.rounding=8; s.border_width=1; s.font_size=16;
    return s;
}
Style nord_style() {
    Style s;
    s.background = {0.180f,0.204f,0.251f,1}; s.panel = {0.231f,0.259f,0.322f,1};
    s.text = {0.847f,0.871f,0.914f,1}; s.text_dim = {0.596f,0.635f,0.702f,1};
    s.border = {0.263f,0.298f,0.369f,1}; s.button = {0.231f,0.259f,0.322f,1};
    s.button_hover = {0.263f,0.298f,0.369f,1}; s.button_active = {0.298f,0.337f,0.416f,1};
    s.input_bg = {0.200f,0.227f,0.282f,1}; s.input_focus = {0.533f,0.753f,0.816f,1};
    s.window_padding=20; s.item_spacing=10; s.item_height=36; s.rounding=6; s.border_width=1; s.font_size=16;
    return s;
}
Style gruvbox_dark_style() {
    Style s;
    s.background = {0.157f,0.157f,0.157f,1}; s.panel = {0.235f,0.220f,0.212f,1};
    s.text = {0.922f,0.859f,0.698f,1}; s.text_dim = {0.835f,0.769f,0.576f,1};
    s.border = {0.314f,0.294f,0.282f,1}; s.button = {0.235f,0.220f,0.212f,1};
    s.button_hover = {0.314f,0.294f,0.282f,1}; s.button_active = {0.400f,0.373f,0.329f,1};
    s.input_bg = {0.196f,0.188f,0.188f,1}; s.input_focus = {0.271f,0.522f,0.533f,1};
    s.window_padding=20; s.item_spacing=10; s.item_height=36; s.rounding=4; s.border_width=1; s.font_size=16;
    return s;
}
Style one_dark_style() {
    Style s;
    s.background = {0.157f,0.173f,0.204f,1}; s.panel = {0.173f,0.192f,0.227f,1};
    s.text = {0.671f,0.698f,0.749f,1}; s.text_dim = {0.435f,0.467f,0.522f,1};
    s.border = {0.208f,0.231f,0.271f,1}; s.button = {0.173f,0.192f,0.227f,1};
    s.button_hover = {0.208f,0.231f,0.271f,1}; s.button_active = {0.247f,0.278f,0.329f,1};
    s.input_bg = {0.145f,0.161f,0.192f,1}; s.input_focus = {0.380f,0.686f,0.937f,1};
    s.window_padding=20; s.item_spacing=10; s.item_height=36; s.rounding=6; s.border_width=1; s.font_size=16;
    return s;
}


// ============================================================
// Windows Implementation
// ============================================================
#ifdef _WIN32

namespace internal {

static std::wstring utf8_to_wide(const char* u) {
    if (!u || !u[0]) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, u, -1, nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, u, -1, &w[0], n);
    return w;
}

struct PlatformState {
    HWND hwnd = nullptr; HINSTANCE instance = nullptr;
    bool running = false; int width = 0, height = 0;
};
struct RendererState {
    ID2D1Factory*           d2d_factory      = nullptr;
    ID2D1HwndRenderTarget*  target           = nullptr;
    ID2D1SolidColorBrush*   brush            = nullptr;
    IDWriteFactory*         dwrite_factory   = nullptr;
    IDWriteTextFormat*      text_format      = nullptr;
    IDWriteRenderingParams* rendering_params = nullptr;
    IWICImagingFactory*     wic_factory      = nullptr;
    float dpi_scale = 1.0f; bool com_inited = false;
};

static PlatformState  g_platform;
static RendererState  g_renderer;
static LARGE_INTEGER  g_freq, g_last_time;

static void dbg(const char* fmt, ...) {
    char buf[512]; va_list a; va_start(a, fmt); vsnprintf(buf, sizeof(buf), fmt, a); va_end(a);
    OutputDebugStringA(buf);
}

static D2D1_COLOR_F tod(Color c) { return {c.r, c.g, c.b, c.a}; }
static void set_brush(Color c)   { g_renderer.brush->SetColor(tod(c)); }
static void clear_bg(Color c)    { g_renderer.target->Clear(tod(c)); }

static void fill_round_rect(Rect r, float rad, Color c) {
    set_brush(c);
    D2D1_RECT_F rc = {r.x, r.y, r.x+r.w, r.y+r.h};
    g_renderer.target->FillRoundedRectangle(D2D1::RoundedRect(rc, rad, rad), g_renderer.brush);
}
static void stroke_round_rect(Rect r, float rad, float thick, Color c) {
    set_brush(c);
    D2D1_RECT_F rc = {r.x, r.y, r.x+r.w, r.y+r.h};
    g_renderer.target->DrawRoundedRectangle(D2D1::RoundedRect(rc, rad, rad), g_renderer.brush, thick);
}
static void fill_rect(Rect r, Color c) {
    set_brush(c);
    D2D1_RECT_F rc = {r.x, r.y, r.x+r.w, r.y+r.h};
    g_renderer.target->FillRectangle(rc, g_renderer.brush);
}
static void draw_line(float x0, float y0, float x1, float y1, float thick, Color c) {
    set_brush(c); g_renderer.target->DrawLine({x0,y0}, {x1,y1}, g_renderer.brush, thick);
}
static void push_clip(Rect r) {
    D2D1_RECT_F rc = {r.x, r.y, r.x+r.w, r.y+r.h};
    g_renderer.target->PushAxisAlignedClip(rc, D2D1_ANTIALIAS_MODE_ALIASED);
}
static void pop_clip() { g_renderer.target->PopAxisAlignedClip(); }

static void draw_text_utf8(const char* utf8, Rect r, Color c) {
    std::wstring w = utf8_to_wide(utf8);
    if (w.empty()) return;
    set_brush(c);
    D2D1_RECT_F rc = {r.x, r.y, r.x+r.w, r.y+r.h};
    DWRITE_TRIMMING trim = {DWRITE_TRIMMING_GRANULARITY_NONE,0,0};
    g_renderer.text_format->SetTrimming(&trim, nullptr);
    g_renderer.target->DrawText(w.c_str(),(UINT32)w.size(),g_renderer.text_format,rc,g_renderer.brush,D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
static void draw_text_utf8_centered(const char* utf8, Rect r, Color c) {
    g_renderer.text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    draw_text_utf8(utf8, r, c);
    g_renderer.text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
}

static float measure_text_width(const char* utf8) {
    if (!utf8 || !utf8[0]) return 0.0f;
    std::wstring w = utf8_to_wide(utf8);
    IDWriteTextLayout* lay = nullptr;
    if (FAILED(g_renderer.dwrite_factory->CreateTextLayout(w.c_str(),(UINT32)w.size(),g_renderer.text_format,10000,1000,&lay))||!lay) return 0;
    DWRITE_TEXT_METRICS m; lay->GetMetrics(&m); lay->Release(); return m.width;
}
static int byte_from_x(const char* utf8, float rel_x) {
    if (!utf8||!utf8[0]||rel_x<=0) return 0;
    std::wstring w = utf8_to_wide(utf8);
    IDWriteTextLayout* lay = nullptr;
    if (FAILED(g_renderer.dwrite_factory->CreateTextLayout(w.c_str(),(UINT32)w.size(),g_renderer.text_format,10000,1000,&lay))||!lay) return (int)strlen(utf8);
    BOOL trail=0,inside=0; DWRITE_HIT_TEST_METRICS m{};
    lay->HitTestPoint(rel_x,0,&trail,&inside,&m);
    UINT32 wpos = m.textPosition + (trail?1u:0u);
    if (wpos > (UINT32)w.size()) wpos=(UINT32)w.size();
    lay->Release();
    const char* p = utf8; UINT32 wc=0;
    while (*p && wc < wpos) {
        unsigned char ch=(unsigned char)*p;
        int sl = ch<0x80?1:ch<0xE0?2:ch<0xF0?3:4;
        wc += (sl==4)?2:1; p+=sl;
    }
    return (int)(p-utf8);
}

static void draw_image_handle(ImageHandle* img, Rect r) {
    auto* bmp = img ? static_cast<ID2D1Bitmap*>(img->_impl) : nullptr;
    if (!bmp) return;
    D2D1_RECT_F dst = {r.x, r.y, r.x+r.w, r.y+r.h};
    g_renderer.target->DrawBitmap(bmp, dst);
}

static void clipboard_set(const char* utf8) {
    if (!g_platform.hwnd||!utf8) return;
    std::wstring w = utf8_to_wide(utf8);
    size_t n = (w.size()+1)*sizeof(wchar_t);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE,n);
    if (!hg) return;
    memcpy(GlobalLock(hg),w.c_str(),n); GlobalUnlock(hg);
    if (OpenClipboard(g_platform.hwnd)) { EmptyClipboard(); SetClipboardData(CF_UNICODETEXT,hg); CloseClipboard(); }
    else GlobalFree(hg);
}
static std::string clipboard_get() {
    if (!g_platform.hwnd||!OpenClipboard(g_platform.hwnd)) return "";
    std::string result;
    HGLOBAL hg = GetClipboardData(CF_UNICODETEXT);
    if (hg) {
        wchar_t* w=(wchar_t*)GlobalLock(hg);
        if (w) { int n=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);
                 if (n>1){result.resize(n-1);WideCharToMultiByte(CP_UTF8,0,w,-1,&result[0],n,nullptr,nullptr);}
                 GlobalUnlock(hg); }
    }
    CloseClipboard(); return result;
}

static bool init_d2d() {
    D2D1_FACTORY_OPTIONS opts = {};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,__uuidof(ID2D1Factory),&opts,(void**)&g_renderer.d2d_factory))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),(IUnknown**)&g_renderer.dwrite_factory))) return false;
    return true;
}
static bool create_render_target() {
    RECT rc; GetClientRect(g_platform.hwnd, &rc);
    D2D1_SIZE_U psz = {(UINT32)(rc.right-rc.left),(UINT32)(rc.bottom-rc.top)};
    UINT dpi = GetDpiForWindow(g_platform.hwnd); if (!dpi) dpi=96;
    g_renderer.dpi_scale = (float)dpi/96.0f;
    g_platform.width  = (int)(psz.width  / g_renderer.dpi_scale + 0.5f);
    g_platform.height = (int)(psz.height / g_renderer.dpi_scale + 0.5f);
    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN,D2D1_ALPHA_MODE_UNKNOWN),(float)dpi,(float)dpi);
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwp = D2D1::HwndRenderTargetProperties(g_platform.hwnd,psz);
    if (FAILED(g_renderer.d2d_factory->CreateHwndRenderTarget(rtp,hwp,&g_renderer.target))) return false;
    if (FAILED(g_renderer.target->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),&g_renderer.brush))) return false;
    g_renderer.target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    IDWriteRenderingParams* dp=nullptr; g_renderer.dwrite_factory->CreateRenderingParams(&dp);
    if (dp) { g_renderer.dwrite_factory->CreateCustomRenderingParams(dp->GetGamma(),dp->GetEnhancedContrast(),dp->GetClearTypeLevel(),dp->GetPixelGeometry(),DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC,&g_renderer.rendering_params); dp->Release(); }
    if (g_renderer.rendering_params) g_renderer.target->SetTextRenderingParams(g_renderer.rendering_params);
    return true;
}
static bool create_text_format() {
    if (g_renderer.text_format) { g_renderer.text_format->Release(); g_renderer.text_format=nullptr; }
    if (FAILED(g_renderer.dwrite_factory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,g_style.font_size,L"en-us",&g_renderer.text_format))) return false;
    g_renderer.text_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    g_renderer.text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g_renderer.text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    return true;
}

static HICON make_ftui_icon(int size) {
    if (!g_renderer.d2d_factory||!g_renderer.dwrite_factory) return nullptr;
    HDC screen_dc=GetDC(nullptr); HDC hdc=CreateCompatibleDC(screen_dc); ReleaseDC(nullptr,screen_dc);
    BITMAPV5HEADER bmi={}; bmi.bV5Size=sizeof(bmi); bmi.bV5Width=size; bmi.bV5Height=-size;
    bmi.bV5Planes=1; bmi.bV5BitCount=32; bmi.bV5Compression=BI_BITFIELDS;
    bmi.bV5RedMask=0x00FF0000; bmi.bV5GreenMask=0x0000FF00; bmi.bV5BlueMask=0x000000FF; bmi.bV5AlphaMask=0xFF000000;
    void* bits=nullptr; HBITMAP hbm=CreateDIBSection(hdc,(BITMAPINFO*)&bmi,DIB_RGB_COLORS,&bits,nullptr,0);
    if (!hbm){DeleteDC(hdc);return nullptr;}
    HGDIOBJ old=SelectObject(hdc,hbm);
    D2D1_RENDER_TARGET_PROPERTIES rtp=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
    ID2D1DCRenderTarget* dc_rt=nullptr; g_renderer.d2d_factory->CreateDCRenderTarget(&rtp,&dc_rt);
    if (!dc_rt){SelectObject(hdc,old);DeleteObject(hbm);DeleteDC(hdc);return nullptr;}
    RECT br={0,0,size,size}; dc_rt->BindDC(hdc,&br);
    ID2D1SolidColorBrush* ibr=nullptr; dc_rt->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),&ibr);
    float s=(float)size, margin=s*0.1114f, sw=fmaxf(1.f,s/56.f);
    D2D1_COLOR_F orange={1.f,0.51f,0.f,1.f};
    dc_rt->BeginDraw(); dc_rt->Clear(D2D1::ColorF(0,0,0,1));
    { ID2D1PathGeometry* tri=nullptr; g_renderer.d2d_factory->CreatePathGeometry(&tri);
      ID2D1GeometrySink* sink=nullptr; tri->Open(&sink);
      sink->BeginFigure({s*.5f,margin},D2D1_FIGURE_BEGIN_FILLED);
      D2D1_POINT_2F pts[2]={{s-margin,s-margin},{margin,s-margin}}; sink->AddLines(pts,2);
      sink->EndFigure(D2D1_FIGURE_END_CLOSED); sink->Close(); sink->Release();
      ibr->SetColor(D2D1::ColorF(1,1,1,1)); dc_rt->FillGeometry(tri,ibr); tri->Release(); }
    ibr->SetColor(orange); dc_rt->DrawLine({margin,margin},{s-margin,s-margin},ibr,sw*1.5f);
    { D2D1_RECT_F brd={margin,margin,s-margin,s-margin}; dc_rt->DrawRectangle(brd,ibr,sw); }
    dc_rt->EndDraw(); ibr->Release(); dc_rt->Release();
    int ms=((size+31)/32)*4; std::vector<BYTE> mb(ms*size,0);
    HBITMAP hbm_mask=CreateBitmap(size,size,1,1,mb.data());
    ICONINFO ii={TRUE,0,0,hbm_mask,hbm}; HICON icon=CreateIconIndirect(&ii);
    SelectObject(hdc,old); DeleteObject(hbm_mask); DeleteObject(hbm); DeleteDC(hdc);
    return icon;
}

static void release_render_target() {
    if (g_renderer.rendering_params){g_renderer.rendering_params->Release();g_renderer.rendering_params=nullptr;}
    if (g_renderer.brush){g_renderer.brush->Release();g_renderer.brush=nullptr;}
    if (g_renderer.target){g_renderer.target->Release();g_renderer.target=nullptr;}
}

static void execute_command() {
    if (strcmp(g_cmd.buf,"q")==0) PostQuitMessage(0);
    else apply_cmd_theme();
    cmd_clear();
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (hwnd!=g_platform.hwnd&&g_platform.hwnd) return DefWindowProcW(hwnd,msg,wp,lp);
    switch(msg) {
    case WM_DESTROY: g_platform.running=false; PostQuitMessage(0); return 0;
    case WM_SIZE: {
        int pw=LOWORD(lp),ph=HIWORD(lp);
        g_platform.width=(int)(pw/g_renderer.dpi_scale+.5f); g_platform.height=(int)(ph/g_renderer.dpi_scale+.5f);
        if (g_renderer.target&&pw>0&&ph>0) g_renderer.target->Resize({(UINT32)pw,(UINT32)ph});
        return 0; }
    case WM_DPICHANGED: {
        UINT ndpi=HIWORD(wp); g_renderer.dpi_scale=(float)ndpi/96.f;
        if (g_renderer.target) g_renderer.target->SetDpi((float)ndpi,(float)ndpi);
        const RECT* r=(const RECT*)lp;
        SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        return 0; }
    case WM_MOUSEMOVE:
        g_input.mouse_x=GET_X_LPARAM(lp)/g_renderer.dpi_scale;
        g_input.mouse_y=GET_Y_LPARAM(lp)/g_renderer.dpi_scale; return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        g_input.mouse_down=g_input.mouse_pressed=true;
        g_input.mouse_x=GET_X_LPARAM(lp)/g_renderer.dpi_scale;
        g_input.mouse_y=GET_Y_LPARAM(lp)/g_renderer.dpi_scale;
        g_input.shift_held=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        if (g_cmd.active) cmd_clear(); return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        g_input.mouse_down=false; g_input.mouse_released=true;
        g_input.mouse_x=GET_X_LPARAM(lp)/g_renderer.dpi_scale;
        g_input.mouse_y=GET_Y_LPARAM(lp)/g_renderer.dpi_scale; return 0;
    case WM_CHAR: {
        wchar_t wc=(wchar_t)wp;
        if (g_cmd.active) {
            if (wc==L'\b'){if(g_cmd.len>0)g_cmd.buf[--g_cmd.len]='\0';}
            else if(wc==L'\r'||wc==L'\n') execute_command();
            else if(wc==27) cmd_clear();
            else if(wc>=32&&g_cmd.len<15){g_cmd.buf[g_cmd.len++]=(char)wc;g_cmd.buf[g_cmd.len]='\0';}
            return 0;
        }
        if (wc==L'\b') g_input.key_backspace=true;
        else if(wc==L'\r'||wc==L'\n') g_input.key_enter=true;
        else if(wc==L':'&&g_ctx.focused_input_id==0&&g_shortcuts_enabled){g_cmd.active=true;g_cmd.len=0;g_cmd.buf[0]='\0';}
        else if(wc>=32){
            char buf[8]={}; int n=WideCharToMultiByte(CP_UTF8,0,&wc,1,buf,sizeof(buf)-1,nullptr,nullptr);
            if(n>0&&g_input.text_input_count+n<(int)sizeof(g_input.text_input)){memcpy(g_input.text_input+g_input.text_input_count,buf,n);g_input.text_input_count+=n;}
        }
        return 0; }
    case WM_KEYDOWN:
        g_input.shift_held=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        g_input.ctrl_held=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        if (g_cmd.active){if(wp==VK_ESCAPE)cmd_clear();return 0;}
        if(wp==VK_BACK)  g_input.key_backspace=true;
        if(wp==VK_RETURN)g_input.key_enter=true;
        if(wp==VK_TAB){if(g_input.shift_held)g_input.key_shift_tab=true;else g_input.key_tab=true;}
        if(wp==VK_LEFT) g_input.key_left=true;
        if(wp==VK_RIGHT)g_input.key_right=true;
        if(wp==VK_UP)   g_input.key_up=true;
        if(wp==VK_DOWN) g_input.key_down=true;
        if(g_input.ctrl_held){
            if(wp=='A'&&g_ctx.focused_input_id!=0){
                g_text_cursor_id=g_ctx.focused_input_id; g_text_sel_anchor=0; g_text_cursor=0x7FFFFFFF;
                g_ta_cursor_id  =g_ctx.focused_input_id; g_ta_sel_anchor  =0; g_ta_cursor  =0x7FFFFFFF;
            }
            if(wp=='C')g_input.key_ctrl_c=true;
            if(wp=='V')g_input.key_ctrl_v=true;
            if(wp=='Q'&&g_shortcuts_enabled)PostQuitMessage(0);
        }
        return 0;
    case WM_KEYUP:
        g_input.shift_held=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        g_input.ctrl_held=(GetKeyState(VK_CONTROL)&0x8000)!=0; return 0;
    case WM_MOUSEWHEEL: {
        short delta=(short)HIWORD(wp);
        g_scroll_y -= (float)delta/WHEEL_DELTA*80.f;
        if(g_scroll_y<0)g_scroll_y=0; return 0; }
    case WM_SETFOCUS:   g_input.focused=true;  return 0;
    case WM_KILLFOCUS:  g_input.focused=false;  g_ctx.focused_input_id=0; return 0;
    case WM_ERASEBKGND: return 1;
    default: return DefWindowProcW(hwnd,msg,wp,lp);
    }
}

} // namespace internal

bool create_window(const Config& cfg) {
    using namespace internal;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_renderer.com_inited = (hr==S_OK||hr==S_FALSE);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_style = default_dark_style();
    g_platform.instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc={sizeof(wc)}; wc.style=CS_HREDRAW|CS_VREDRAW; wc.lpfnWndProc=wndproc;
    wc.hInstance=g_platform.instance; wc.hCursor=LoadCursorW(nullptr,(LPCWSTR)IDC_ARROW);
    wc.lpszClassName=L"FTUI_Window";
    wc.hIcon=cfg.icon?(HICON)cfg.icon:LoadIconW(nullptr,(LPCWSTR)IDI_APPLICATION); wc.hIconSm=wc.hIcon;
    if (!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return false;

    DWORD style=WS_OVERLAPPEDWINDOW; if(!cfg.resizable)style&=~(WS_THICKFRAME|WS_MAXIMIZEBOX);
    UINT sys_dpi=GetDpiForSystem(); float pre=(float)sys_dpi/96.f;
    RECT rc={0,0,(LONG)(cfg.width*pre+.5f),(LONG)(cfg.height*pre+.5f)};
    using AdjFn=BOOL(WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT);
    auto adj=(AdjFn)GetProcAddress(GetModuleHandleW(L"user32.dll"),"AdjustWindowRectExForDpi");
    if(adj) adj(&rc,style,FALSE,0,sys_dpi); else AdjustWindowRect(&rc,style,FALSE);
    int w=rc.right-rc.left,h=rc.bottom-rc.top,x=CW_USEDEFAULT,y=CW_USEDEFAULT;
    if(cfg.center_window){int sw=GetSystemMetrics(SM_CXSCREEN),sh=GetSystemMetrics(SM_CYSCREEN);x=(sw-w)/2;y=(sh-h)/2;}

    std::wstring wtitle = utf8_to_wide(cfg.title ? cfg.title : "FTUI App");
    g_platform.hwnd=CreateWindowExW(0,L"FTUI_Window",wtitle.c_str(),style,x,y,w,h,nullptr,nullptr,g_platform.instance,nullptr);
    if(!g_platform.hwnd) return false;
    g_platform.width=cfg.width; g_platform.height=cfg.height; g_platform.running=true;
    if(!init_d2d()||!create_render_target()||!create_text_format()) return false;
    if(g_platform.width!=cfg.width||g_platform.height!=cfg.height){
        RECT rc2={0,0,(LONG)(cfg.width*g_renderer.dpi_scale+.5f),(LONG)(cfg.height*g_renderer.dpi_scale+.5f)};
        UINT adpi=(UINT)(g_renderer.dpi_scale*96+.5f);
        if(adj)adj(&rc2,style,FALSE,0,adpi);else AdjustWindowRect(&rc2,style,FALSE);
        int nw=rc2.right-rc2.left,nh=rc2.bottom-rc2.top,nx=x,ny=y;
        if(cfg.center_window){nx=(GetSystemMetrics(SM_CXSCREEN)-nw)/2;ny=(GetSystemMetrics(SM_CYSCREEN)-nh)/2;}
        SetWindowPos(g_platform.hwnd,nullptr,nx,ny,nw,nh,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    HICON ico=cfg.icon?(HICON)cfg.icon:make_ftui_icon(256);
    HICON ico_sm=cfg.icon?(HICON)cfg.icon:make_ftui_icon(32);
    SendMessageW(g_platform.hwnd,WM_SETICON,ICON_BIG,(LPARAM)ico);
    SendMessageW(g_platform.hwnd,WM_SETICON,ICON_SMALL,(LPARAM)ico_sm);
    ShowWindow(g_platform.hwnd,SW_SHOW); UpdateWindow(g_platform.hwnd);
    QueryPerformanceFrequency(&g_freq); QueryPerformanceCounter(&g_last_time);
    return true;
}

bool pump() {
    using namespace internal;
    g_input.mouse_pressed=g_input.mouse_released=false;
    g_input.key_backspace=g_input.key_enter=g_input.key_tab=g_input.key_shift_tab=false;
    g_input.key_left=g_input.key_right=g_input.key_up=g_input.key_down=false;
    g_input.key_ctrl_c=g_input.key_ctrl_v=false;
    g_input.text_input_count=0; memset(g_input.text_input,0,sizeof(g_input.text_input));
    MSG msg;
    while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
        if(msg.message==WM_QUIT){g_platform.running=false;return false;}
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    return g_platform.running;
}

void begin() {
    using namespace internal;
    g_ctx.hot_id=0;
    if((g_input.key_tab||g_input.key_shift_tab)&&!g_ctx.tab_stops_prev.empty()){
        auto& stops=g_ctx.tab_stops_prev;
        int cur=g_ctx.focused_input_id,idx=-1;
        for(int i=0;i<(int)stops.size();i++){if(stops[i]==cur){idx=i;break;}}
        g_ctx.focused_input_id=g_input.key_shift_tab?stops[(idx<=0?(int)stops.size():idx)-1]:stops[(idx+1)%(int)stops.size()];
        g_text_cursor_id=g_ta_cursor_id=0;
    }
    g_ctx.tab_stops.clear();
    float pad=g_style.window_padding;
    g_ctx.content_region={pad,pad,(float)g_platform.width-2*pad,(float)g_platform.height-2*pad};
    g_ctx.cursor_x=g_ctx.content_region.x;
    const float kSW=14;
    float vh=g_ctx.content_region.h;
    bool nsb=g_content_height>vh+1;
    float ms=nsb?g_content_height-vh:0;
    if(!nsb)g_scroll_y=0; g_scroll_y=g_scroll_y<0?0:(g_scroll_y>ms?ms:g_scroll_y);
    if(nsb)g_ctx.content_region.w-=kSW+pad;
    g_ctx.cursor_y=g_ctx.content_region.y-g_scroll_y;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    float dt=(float)(now.QuadPart-g_last_time.QuadPart)/(float)g_freq.QuadPart;
    g_last_time=now; g_fps_accum+=dt; g_fps_frames++;
    if(g_fps_accum>=0.5f){g_fps=(float)g_fps_frames/g_fps_accum;g_fps_frames=0;g_fps_accum=0;}
    if(!g_renderer.target) return;
    g_renderer.target->BeginDraw(); g_drawing=true;
    clear_bg(g_style.background);
    D2D1_RECT_F cr={0,g_ctx.content_region.y,(float)g_platform.width,g_ctx.content_region.y+vh};
    g_renderer.target->PushAxisAlignedClip(cr,D2D1_ANTIALIAS_MODE_ALIASED);
}

void end() {
    using namespace internal;
    if(!g_drawing) return;
    g_renderer.target->PopAxisAlignedClip();
    float new_ch=g_ctx.cursor_y+g_scroll_y-g_ctx.content_region.y;
    if(new_ch<0)new_ch=0;
    {
        const float kSW=14; float vh=g_ctx.content_region.h;
        bool nsb=g_content_height>vh+1;
        if(nsb){
            float ms=g_content_height-vh;
            float tx=g_ctx.content_region.x+g_ctx.content_region.w+g_style.window_padding;
            Rect track={tx,g_ctx.content_region.y,kSW,vh};
            float th=fmaxf(20,(vh/g_content_height)*vh);
            float tt=ms>0?g_scroll_y/ms:0;
            float ty2=g_ctx.content_region.y+tt*(vh-th);
            Rect thumb={tx,ty2,kSW,th};
            bool thov=rect_contains(thumb,g_input.mouse_x,g_input.mouse_y);
            bool trhov=rect_contains(track,g_input.mouse_x,g_input.mouse_y);
            if(g_input.mouse_pressed&&thov){g_sb_dragging=true;g_sb_drag_mouse_y=g_input.mouse_y;g_sb_drag_scroll0=g_scroll_y;}
            if(g_sb_dragging){
                if(g_input.mouse_down||g_input.mouse_released){float sc=ms/fmaxf(1,vh-th);g_scroll_y=g_sb_drag_scroll0+(g_input.mouse_y-g_sb_drag_mouse_y)*sc;g_scroll_y=g_scroll_y<0?0:(g_scroll_y>ms?ms:g_scroll_y);}
                if(g_input.mouse_released)g_sb_dragging=false;
            }
            if(g_input.mouse_pressed&&trhov&&!thov){g_scroll_y+=(g_input.mouse_y<ty2?-vh:vh);g_scroll_y=g_scroll_y<0?0:(g_scroll_y>ms?ms:g_scroll_y);}
            fill_round_rect(track,6,g_style.input_bg);
            fill_round_rect(thumb,6,g_sb_dragging?g_style.input_focus:thov?g_style.button_hover:g_style.border);
        }
        g_content_height=new_ch;
    }
    if(g_debug.show_fps){
        char buf[64]; snprintf(buf,sizeof(buf),"FPS: %.0f  frame:%d  DPI:%.0f%%",g_fps,g_ctx.frame_index,g_renderer.dpi_scale*100);
        float lh=text_line_height(); Rect r={4,4,300,lh}; fill_rect(r,{0,0,0,.6f}); draw_text_utf8(buf,r,g_style.text_dim);
    }
    if(g_debug.show_hovered_id||g_debug.show_active_id){
        char buf[128]; snprintf(buf,sizeof(buf),"hot=%d active=%d focused=%d",g_ctx.hot_id,g_ctx.active_id,g_ctx.focused_input_id);
        float lh=text_line_height(),oy=g_debug.show_fps?lh+6:4; Rect r={4,oy,360,lh}; fill_rect(r,{0,0,0,.6f}); draw_text_utf8(buf,r,g_style.text_dim);
    }
    if(g_cmd.active){
        char buf[32]; snprintf(buf,sizeof(buf),":%s_",g_cmd.buf);
        float lh=text_line_height(),oy=(float)g_platform.height-g_style.window_padding-lh;
        Rect r={g_style.window_padding,oy,200,lh};
        fill_rect({r.x-4,r.y-2,r.w+8,r.h+4},{0,0,0,.75f}); draw_text_utf8(buf,r,g_style.text);
    }
    HRESULT hr=g_renderer.target->EndDraw(); g_drawing=false;
    if(hr==D2DERR_RECREATE_TARGET){release_render_target();create_render_target();}
    g_ctx.tab_stops_prev=g_ctx.tab_stops; g_ctx.frame_index++;
}

void shutdown() {
    using namespace internal;
    release_render_target();
    if(g_renderer.wic_factory)   {g_renderer.wic_factory->Release();   g_renderer.wic_factory=nullptr;}
    if(g_renderer.text_format)   {g_renderer.text_format->Release();   g_renderer.text_format=nullptr;}
    if(g_renderer.dwrite_factory){g_renderer.dwrite_factory->Release();g_renderer.dwrite_factory=nullptr;}
    if(g_renderer.d2d_factory)   {g_renderer.d2d_factory->Release();   g_renderer.d2d_factory=nullptr;}
    if(g_platform.hwnd)          {DestroyWindow(g_platform.hwnd);      g_platform.hwnd=nullptr;}
    if(g_renderer.com_inited)    {CoUninitialize();g_renderer.com_inited=false;}
}

void open_child_window(const Config& cfg, std::function<void()> fn) {
    using namespace internal;
    if(!g_platform.hwnd) return;
    struct Snap {
        PlatformState p; RendererState r; InputState in; UIContext ctx; Style sty; DebugState dbg;
        float sc,ch,sbmy,sbms0; bool sbd;
        CmdState cmd; float fps,fpsa; int fpsf;
        LARGE_INTEGER lt;
        int tci,tc,tsa,taci,tac,taas; float tasc;
        bool shortcuts,drawing;
    } s;
    s.p=g_platform;s.r=g_renderer;s.in=g_input;s.ctx=g_ctx;s.sty=g_style;s.dbg=g_debug;
    s.sc=g_scroll_y;s.ch=g_content_height;s.sbd=g_sb_dragging;s.sbmy=g_sb_drag_mouse_y;s.sbms0=g_sb_drag_scroll0;
    s.cmd=g_cmd;s.fps=g_fps;s.fpsa=g_fps_accum;s.fpsf=g_fps_frames;s.lt=g_last_time;
    s.tci=g_text_cursor_id;s.tc=g_text_cursor;s.tsa=g_text_sel_anchor;
    s.taci=g_ta_cursor_id;s.tac=g_ta_cursor;s.taas=g_ta_sel_anchor;s.tasc=g_ta_scroll_y;
    s.shortcuts=g_shortcuts_enabled;s.drawing=g_drawing;

    auto* d2d = g_renderer.d2d_factory;
    auto* wic = g_renderer.wic_factory;
    auto* dw = g_renderer.dwrite_factory;
    auto  inst = g_platform.instance;
    auto  owner = g_platform.hwnd;

    g_platform={}; g_platform.instance=inst;
    g_renderer={}; g_renderer.d2d_factory=d2d; g_renderer.dwrite_factory=(IDWriteFactory*)dw; g_renderer.wic_factory=(IWICImagingFactory*)wic; g_renderer.dpi_scale=1;
    g_input={}; g_ctx={}; g_style=s.sty; g_debug=s.dbg;
    g_scroll_y=g_content_height=0; g_sb_dragging=false; g_cmd={};
    g_fps=g_fps_accum=0; g_fps_frames=0;
    g_text_cursor_id=g_text_cursor=g_text_sel_anchor=0;
    g_ta_cursor_id=g_ta_cursor=g_ta_sel_anchor=0; g_ta_scroll_y=0;
    g_shortcuts_enabled=s.shortcuts; g_drawing=false;

    DWORD ws=WS_OVERLAPPEDWINDOW; if(!cfg.resizable)ws&=~(WS_THICKFRAME|WS_MAXIMIZEBOX);
    UINT sd=GetDpiForSystem(); float ps=(float)sd/96;
    RECT rc={0,0,(LONG)(cfg.width*ps+.5f),(LONG)(cfg.height*ps+.5f)};
    using AdjFn=BOOL(WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT);
    auto adj=(AdjFn)GetProcAddress(GetModuleHandleW(L"user32.dll"),"AdjustWindowRectExForDpi");
    if(adj)adj(&rc,ws,FALSE,0,sd);else AdjustWindowRect(&rc,ws,FALSE);
    int cw=rc.right-rc.left,ch2=rc.bottom-rc.top,cx=CW_USEDEFAULT,cy=CW_USEDEFAULT;
    if(cfg.center_window){cx=(GetSystemMetrics(SM_CXSCREEN)-cw)/2;cy=(GetSystemMetrics(SM_CYSCREEN)-ch2)/2;}
    std::wstring wt=utf8_to_wide(cfg.title?cfg.title:"FTUI");
    g_platform.hwnd=CreateWindowExW(0,L"FTUI_Window",wt.c_str(),ws,cx,cy,cw,ch2,owner,nullptr,inst,nullptr);
    if(g_platform.hwnd){
        g_platform.width=cfg.width; g_platform.height=cfg.height; g_platform.running=true;
        if(create_render_target()&&create_text_format()){
            QueryPerformanceFrequency(&g_freq); QueryPerformanceCounter(&g_last_time);
            ShowWindow(g_platform.hwnd,SW_SHOW); UpdateWindow(g_platform.hwnd);
            while(pump()){begin();fn();end();}
        }
        release_render_target();
        if(g_renderer.text_format){g_renderer.text_format->Release();g_renderer.text_format=nullptr;}
        g_renderer.d2d_factory=nullptr; g_renderer.dwrite_factory=nullptr; g_renderer.wic_factory=nullptr;
        if(g_platform.hwnd){DestroyWindow(g_platform.hwnd);g_platform.hwnd=nullptr;}
    }
    g_platform=s.p;g_renderer=s.r;g_input=s.in;g_ctx=s.ctx;g_style=s.sty;g_debug=s.dbg;
    g_scroll_y=s.sc;g_content_height=s.ch;g_sb_dragging=s.sbd;g_sb_drag_mouse_y=s.sbmy;g_sb_drag_scroll0=s.sbms0;
    g_cmd=s.cmd;g_fps=s.fps;g_fps_accum=s.fpsa;g_fps_frames=s.fpsf;g_last_time=s.lt;
    g_text_cursor_id=s.tci;g_text_cursor=s.tc;g_text_sel_anchor=s.tsa;
    g_ta_cursor_id=s.taci;g_ta_cursor=s.tac;g_ta_sel_anchor=s.taas;g_ta_scroll_y=s.tasc;
    g_shortcuts_enabled=s.shortcuts;g_drawing=s.drawing;
    InvalidateRect(owner,nullptr,FALSE);
}

ImageHandle* load_image(const char* utf8_path) {
    using namespace internal;
    if(!g_renderer.target||!utf8_path||!utf8_path[0]) return nullptr;
    if(!g_renderer.wic_factory){
        if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&g_renderer.wic_factory)))) return nullptr;
    }
    std::wstring wp=utf8_to_wide(utf8_path);
    IWICBitmapDecoder* dec=nullptr;
    if(FAILED(g_renderer.wic_factory->CreateDecoderFromFilename(wp.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&dec))) return nullptr;
    IWICBitmapFrameDecode* frame=nullptr;
    HRESULT hr=dec->GetFrame(0,&frame); dec->Release(); if(FAILED(hr)) return nullptr;
    IWICFormatConverter* conv=nullptr; g_renderer.wic_factory->CreateFormatConverter(&conv);
    hr=conv->Initialize(frame,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeMedianCut);
    frame->Release(); if(FAILED(hr)){conv->Release();return nullptr;}
    ID2D1Bitmap* bmp=nullptr;
    hr=g_renderer.target->CreateBitmapFromWicBitmap(conv,nullptr,&bmp); conv->Release();
    if(FAILED(hr)) return nullptr;
    ImageHandle* h=new ImageHandle(); h->_impl=bmp; return h;
}
void free_image(ImageHandle* img) {
    if(!img) return;
    if(img->_impl){static_cast<ID2D1Bitmap*>(img->_impl)->Release();img->_impl=nullptr;}
    delete img;
}

std::string open_file_dialog(const char* title, const FileFilter* filters, int filter_count) {
    using namespace internal;
    IFileOpenDialog* dlg=nullptr;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dlg)))) return "";
    if(title){std::wstring wt=utf8_to_wide(title);dlg->SetTitle(wt.c_str());}
    if(filters&&filter_count>0){
        std::vector<COMDLG_FILTERSPEC> specs(filter_count);
        std::vector<std::wstring> wnames(filter_count),wspecs(filter_count);
        for(int i=0;i<filter_count;i++){
            wnames[i]=utf8_to_wide(filters[i].name?filters[i].name:"");
            wspecs[i]=utf8_to_wide(filters[i].spec?filters[i].spec:"*.*");
            specs[i]={wnames[i].c_str(),wspecs[i].c_str()};
        }
        dlg->SetFileTypes((UINT)filter_count,specs.data()); dlg->SetFileTypeIndex(1);
    }
    std::string result;
    if(SUCCEEDED(dlg->Show(g_platform.hwnd))){
        IShellItem* item=nullptr;
        if(SUCCEEDED(dlg->GetResult(&item))){
            PWSTR wpath=nullptr;
            if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&wpath))){
                int n=WideCharToMultiByte(CP_UTF8,0,wpath,-1,nullptr,0,nullptr,nullptr);
                if(n>1){result.resize(n-1);WideCharToMultiByte(CP_UTF8,0,wpath,-1,&result[0],n,nullptr,nullptr);}
                CoTaskMemFree(wpath);
            }
            item->Release();
        }
    }
    dlg->Release(); return result;
}


// ============================================================
// Linux Implementation (X11 + Cairo)
// ============================================================
#elif defined(__linux__)

namespace internal {

struct LinuxImage { cairo_surface_t* surface = nullptr; };

struct PlatformState {
    Display* display = nullptr; Window window = 0;
    Atom wm_delete = 0; bool running = false; int width = 0, height = 0;
};
struct RendererState {
    cairo_surface_t* xlib_surf = nullptr;
    cairo_surface_t* back_surf = nullptr;
    Pixmap           back_px   = 0;
    cairo_t*         cr        = nullptr;
    float            dpi_scale = 1.0f;
    char             font_face[64] = "sans-serif";
};

static PlatformState  g_platform;
static RendererState  g_renderer;
static struct timespec g_last_time;
static XIM  g_xim = nullptr;
static XIC  g_xic = nullptr;
static std::string g_clipboard_buf;

static void dbg(const char* fmt, ...) {
    char buf[512]; va_list a; va_start(a,fmt); vsnprintf(buf,sizeof(buf),fmt,a); va_end(a);
    fputs(buf, stderr);
}

static void set_color(Color c) { cairo_set_source_rgba(g_renderer.cr,c.r,c.g,c.b,c.a); }

static void rrect_path(cairo_t* cr, float x, float y, float w, float h, float r) {
    if (r <= 0.0f || w <= 0.0f || h <= 0.0f) { cairo_rectangle(cr,x,y,w,h); return; }
    float rr = r < w*0.5f ? (r < h*0.5f ? r : h*0.5f) : w*0.5f;
    const double PI = M_PI;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x+w-rr, y+rr,     rr, -PI/2,  0);
    cairo_arc(cr, x+w-rr, y+h-rr,   rr,  0,      PI/2);
    cairo_arc(cr, x+rr,   y+h-rr,   rr,  PI/2,   PI);
    cairo_arc(cr, x+rr,   y+rr,     rr,  PI,      3*PI/2);
    cairo_close_path(cr);
}

static void fill_round_rect(Rect r, float rad, Color c) {
    set_color(c); rrect_path(g_renderer.cr, r.x,r.y,r.w,r.h,rad); cairo_fill(g_renderer.cr);
}
static void stroke_round_rect(Rect r, float rad, float thick, Color c) {
    set_color(c); cairo_set_line_width(g_renderer.cr, thick);
    rrect_path(g_renderer.cr, r.x,r.y,r.w,r.h,rad); cairo_stroke(g_renderer.cr);
}
static void fill_rect(Rect r, Color c) {
    set_color(c); cairo_rectangle(g_renderer.cr,r.x,r.y,r.w,r.h); cairo_fill(g_renderer.cr);
}
static void draw_line(float x0, float y0, float x1, float y1, float thick, Color c) {
    set_color(c); cairo_set_line_width(g_renderer.cr,thick);
    cairo_move_to(g_renderer.cr,x0,y0); cairo_line_to(g_renderer.cr,x1,y1); cairo_stroke(g_renderer.cr);
}
static void push_clip(Rect r) {
    cairo_save(g_renderer.cr); cairo_rectangle(g_renderer.cr,r.x,r.y,r.w,r.h); cairo_clip(g_renderer.cr);
}
static void pop_clip() { cairo_restore(g_renderer.cr); }

static void apply_font() {
#ifdef FTUI_LINUX_FONT
    cairo_select_font_face(g_renderer.cr, FTUI_LINUX_FONT, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
#else
    cairo_select_font_face(g_renderer.cr, g_renderer.font_face, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
#endif
    cairo_set_font_size(g_renderer.cr, g_style.font_size);
}

static void draw_text_utf8(const char* utf8, Rect r, Color c) {
    if (!utf8||!utf8[0]) return;
    cairo_t* cr = g_renderer.cr;
    apply_font();
    cairo_font_extents_t fe; cairo_font_extents(cr,&fe);
    double by = r.y + (r.h - fe.height)*0.5 + fe.ascent;
    set_color(c);
    cairo_save(cr); cairo_rectangle(cr,r.x,r.y,r.w,r.h); cairo_clip(cr);
    cairo_move_to(cr, r.x, by); cairo_show_text(cr, utf8); cairo_restore(cr);
}

static void draw_text_utf8_centered(const char* utf8, Rect r, Color c) {
    if (!utf8||!utf8[0]) return;
    cairo_t* cr = g_renderer.cr;
    apply_font();
    cairo_text_extents_t te; cairo_text_extents(cr,utf8,&te);
    cairo_font_extents_t fe; cairo_font_extents(cr,&fe);
    double cx = r.x + (r.w - te.x_advance)*0.5;
    double by = r.y + (r.h - fe.height)*0.5 + fe.ascent;
    set_color(c);
    cairo_save(cr); cairo_rectangle(cr,r.x,r.y,r.w,r.h); cairo_clip(cr);
    cairo_move_to(cr,cx,by); cairo_show_text(cr,utf8); cairo_restore(cr);
}

static float measure_text_width(const char* utf8) {
    if (!utf8||!utf8[0]) return 0;
    apply_font();
    cairo_text_extents_t te; cairo_text_extents(g_renderer.cr,utf8,&te);
    return (float)te.x_advance;
}

static int byte_from_x(const char* utf8, float rel_x) {
    if (!utf8||!utf8[0]||rel_x<=0) return 0;
    int len=(int)strlen(utf8), pos=0;
    float prev=0;
    while (pos<len) {
        int np=utf8_advance(utf8,pos);
        float w=measure_text_at(utf8,np);
        float mid=(prev+w)*0.5f;
        if (rel_x<mid) return pos;
        prev=w; pos=np;
    }
    return len;
}

static void draw_image_scaled(cairo_surface_t* surf, Rect dst) {
    if (!surf) return;
    double sw=cairo_image_surface_get_width(surf);
    double sh=cairo_image_surface_get_height(surf);
    if (sw<=0||sh<=0) return;
    cairo_t* cr=g_renderer.cr;
    cairo_save(cr);
    cairo_translate(cr,dst.x,dst.y);
    cairo_scale(cr,dst.w/sw,dst.h/sh);
    cairo_set_source_surface(cr,surf,0,0);
    cairo_pattern_set_filter(cairo_get_source(cr),CAIRO_FILTER_BILINEAR);
    cairo_rectangle(cr,0,0,sw,sh); cairo_fill(cr);
    cairo_restore(cr);
}

static void draw_image_handle(ImageHandle* img, Rect r) {
    if (!img||!img->_impl) return;
    auto* li=static_cast<LinuxImage*>(img->_impl);
    if (li&&li->surface) draw_image_scaled(li->surface,r);
}

static float detect_dpi(Display* dpy) {
    char* xrm=XResourceManagerString(dpy);
    if (xrm) {
        XrmDatabase db=XrmGetStringDatabase(xrm);
        if (db) {
            XrmValue val; char* type=nullptr;
            if (XrmGetResource(db,"Xft.dpi","Xft.Dpi",&type,&val)&&val.addr) {
                float dpi=(float)atof(val.addr); XrmDestroyDatabase(db);
                if (dpi>0) return dpi;
            }
            XrmDestroyDatabase(db);
        }
    }
    int s=DefaultScreen(dpy);
    int pw=DisplayWidth(dpy,s),mm=DisplayWidthMM(dpy,s);
    if (mm>0){float d=(float)pw/((float)mm/25.4f);if(d>0&&d<1000)return d;}
    return 96.0f;
}

static bool create_back_buffer(int w, int h) {
    if (!g_platform.display||!g_platform.window) return false;
    Display* dpy=g_platform.display; int s=DefaultScreen(dpy);
    if (g_renderer.cr)       { cairo_destroy(g_renderer.cr);          g_renderer.cr=nullptr; }
    if (g_renderer.back_surf){ cairo_surface_destroy(g_renderer.back_surf); g_renderer.back_surf=nullptr; }
    if (g_renderer.back_px)  { XFreePixmap(dpy,g_renderer.back_px);  g_renderer.back_px=0; }
    g_renderer.back_px=XCreatePixmap(dpy,g_platform.window,w,h,DefaultDepth(dpy,s));
    g_renderer.back_surf=cairo_xlib_surface_create(dpy,g_renderer.back_px,DefaultVisual(dpy,s),w,h);
    g_renderer.cr=cairo_create(g_renderer.back_surf);
    return cairo_status(g_renderer.cr)==CAIRO_STATUS_SUCCESS;
}

static bool init_cairo() {
    Display* dpy=g_platform.display; int s=DefaultScreen(dpy);
    g_renderer.xlib_surf=cairo_xlib_surface_create(dpy,g_platform.window,DefaultVisual(dpy,s),g_platform.width,g_platform.height);
    return create_back_buffer(g_platform.width,g_platform.height);
}

static void teardown_cairo() {
    if (g_renderer.cr)       { cairo_destroy(g_renderer.cr);          g_renderer.cr=nullptr; }
    if (g_renderer.back_surf){ cairo_surface_destroy(g_renderer.back_surf); g_renderer.back_surf=nullptr; }
    if (g_renderer.back_px&&g_platform.display){ XFreePixmap(g_platform.display,g_renderer.back_px); g_renderer.back_px=0; }
    if (g_renderer.xlib_surf){ cairo_surface_destroy(g_renderer.xlib_surf); g_renderer.xlib_surf=nullptr; }
}

static void swap_buffers() {
    if (!g_renderer.xlib_surf||!g_renderer.back_surf) return;
    cairo_t* fc=cairo_create(g_renderer.xlib_surf);
    cairo_set_source_surface(fc,g_renderer.back_surf,0,0);
    cairo_paint(fc); cairo_destroy(fc);
    XFlush(g_platform.display);
}

static void clipboard_set(const char* utf8) {
    if (!utf8) return;
    g_clipboard_buf=utf8;
    Atom clip=XInternAtom(g_platform.display,"CLIPBOARD",False);
    XSetSelectionOwner(g_platform.display,clip,g_platform.window,CurrentTime);
    XSetSelectionOwner(g_platform.display,XA_PRIMARY,g_platform.window,CurrentTime);
}

static void serve_selection(XEvent& ev) {
    XSelectionRequestEvent* req=&ev.xselectionrequest;
    XEvent resp={}; resp.xselection.type=SelectionNotify;
    resp.xselection.display=req->display; resp.xselection.requestor=req->requestor;
    resp.xselection.selection=req->selection; resp.xselection.target=req->target;
    resp.xselection.time=req->time; resp.xselection.property=None;
    Atom utf8a=XInternAtom(g_platform.display,"UTF8_STRING",False);
    Atom tgts=XInternAtom(g_platform.display,"TARGETS",False);
    if (req->target==tgts) {
        Atom t[]={tgts,utf8a,XA_STRING};
        XChangeProperty(req->display,req->requestor,req->property,XA_ATOM,32,PropModeReplace,(unsigned char*)t,3);
        resp.xselection.property=req->property;
    } else if (req->target==utf8a||req->target==XA_STRING) {
        XChangeProperty(req->display,req->requestor,req->property,req->target,8,PropModeReplace,(unsigned char*)g_clipboard_buf.c_str(),(int)g_clipboard_buf.size());
        resp.xselection.property=req->property;
    }
    XSendEvent(req->display,req->requestor,False,0,&resp);
}

static std::string clipboard_get() {
    Display* dpy=g_platform.display; Window win=g_platform.window;
    Atom clip=XInternAtom(dpy,"CLIPBOARD",False);
    if (XGetSelectionOwner(dpy,clip)==win) return g_clipboard_buf;
    Atom utf8a=XInternAtom(dpy,"UTF8_STRING",False);
    Atom prop=XInternAtom(dpy,"FTUI_CLIP",False);
    XConvertSelection(dpy,clip,utf8a,prop,win,CurrentTime); XFlush(dpy);
    for (int i=0;i<50;i++) {
        if (XPending(dpy)) {
            XEvent ev; XNextEvent(dpy,&ev);
            if (ev.type==SelectionNotify) {
                if (ev.xselection.property==None) return "";
                Atom at; int fmt; unsigned long n,ba; unsigned char* data=nullptr;
                XGetWindowProperty(dpy,win,prop,0,0x7fffffff,True,AnyPropertyType,&at,&fmt,&n,&ba,&data);
                if (data) { std::string r((char*)data,n); XFree(data); return r; }
                return "";
            }
            if (ev.type==SelectionRequest) serve_selection(ev);
        }
        struct timespec ts={0,2000000}; nanosleep(&ts,nullptr);
    }
    return "";
}

static void execute_command() {
    if (strcmp(g_cmd.buf,"q")==0) g_platform.running=false;
    else apply_cmd_theme();
    cmd_clear();
}

static void handle_xevent(XEvent& ev) {
    if (ev.type==SelectionRequest) { serve_selection(ev); return; }
    if (ev.xany.window!=g_platform.window) return;
    switch (ev.type) {
    case ConfigureNotify: {
        int nw=ev.xconfigure.width,nh=ev.xconfigure.height;
        if (nw!=g_platform.width||nh!=g_platform.height) {
            g_platform.width=nw; g_platform.height=nh;
            if (g_renderer.xlib_surf) cairo_xlib_surface_set_size(g_renderer.xlib_surf,nw,nh);
            create_back_buffer(nw,nh);
        }
        break; }
    case ButtonPress:
        if (ev.xbutton.button==Button1) {
            g_input.mouse_pressed=g_input.mouse_down=true;
            g_input.mouse_x=(float)ev.xbutton.x; g_input.mouse_y=(float)ev.xbutton.y;
            if (g_cmd.active) cmd_clear();
        } else if (ev.xbutton.button==Button4) {
            g_scroll_y-=80; if(g_scroll_y<0)g_scroll_y=0;
        } else if (ev.xbutton.button==Button5) {
            g_scroll_y+=80;
        }
        break;
    case ButtonRelease:
        if (ev.xbutton.button==Button1) {
            g_input.mouse_released=true; g_input.mouse_down=false;
            g_input.mouse_x=(float)ev.xbutton.x; g_input.mouse_y=(float)ev.xbutton.y;
        }
        break;
    case MotionNotify:
        g_input.mouse_x=(float)ev.xmotion.x; g_input.mouse_y=(float)ev.xmotion.y; break;
    case KeyRelease:
        g_input.shift_held=(ev.xkey.state&ShiftMask)!=0;
        g_input.ctrl_held =(ev.xkey.state&ControlMask)!=0; break;
    case KeyPress: {
        g_input.shift_held=(ev.xkey.state&ShiftMask)!=0;
        g_input.ctrl_held =(ev.xkey.state&ControlMask)!=0;
        char buf[32]={}; KeySym ks=0;
        int n=0;
        if (g_xic) {
            Status st; n=Xutf8LookupString(g_xic,&ev.xkey,buf,sizeof(buf)-1,&ks,&st);
            if (st==XLookupNone||st==XLookupChars) { if(st==XLookupNone) n=0; ks=XLookupKeysym(&ev.xkey,0); }
        } else {
            ks=XLookupKeysym(&ev.xkey,0);
            n=XLookupString(&ev.xkey,buf,sizeof(buf)-1,nullptr,nullptr);
        }
        // navigation keys
        if (ks==XK_BackSpace) g_input.key_backspace=true;
        if (ks==XK_Return||ks==XK_KP_Enter) g_input.key_enter=true;
        if (ks==XK_Tab) { if(g_input.shift_held)g_input.key_shift_tab=true;else g_input.key_tab=true; }
        if (ks==XK_Left)  g_input.key_left=true;
        if (ks==XK_Right) g_input.key_right=true;
        if (ks==XK_Up)    g_input.key_up=true;
        if (ks==XK_Down)  g_input.key_down=true;
        if (ks==XK_Escape && g_cmd.active) { cmd_clear(); break; }
        if (g_input.ctrl_held) {
            if (ks==XK_q||ks==XK_Q) { if(g_shortcuts_enabled)g_platform.running=false; break; }
            if (ks==XK_c||ks==XK_C) g_input.key_ctrl_c=true;
            if (ks==XK_v||ks==XK_V) g_input.key_ctrl_v=true;
            if ((ks==XK_a||ks==XK_A)&&g_ctx.focused_input_id!=0) {
                g_text_cursor_id=g_ctx.focused_input_id; g_text_sel_anchor=0; g_text_cursor=0x7FFFFFFF;
                g_ta_cursor_id  =g_ctx.focused_input_id; g_ta_sel_anchor  =0; g_ta_cursor  =0x7FFFFFFF;
            }
            break;
        }
        if (g_cmd.active) {
            if (ks==XK_BackSpace){if(g_cmd.len>0)g_cmd.buf[--g_cmd.len]='\0';}
            else if(ks==XK_Return||ks==XK_KP_Enter) execute_command();
            else if(n>0&&buf[0]>=32&&g_cmd.len<15){g_cmd.buf[g_cmd.len++]=buf[0];g_cmd.buf[g_cmd.len]='\0';}
            break;
        }
        if (n>0&&buf[0]==':'&&g_ctx.focused_input_id==0&&g_shortcuts_enabled) {
            g_cmd.active=true; g_cmd.len=0; g_cmd.buf[0]='\0'; break;
        }
        if (n>0&&(unsigned char)buf[0]>=32) {
            if (g_input.text_input_count+n<(int)sizeof(g_input.text_input)) {
                memcpy(g_input.text_input+g_input.text_input_count,buf,n);
                g_input.text_input_count+=n;
            }
        }
        break; }
    case FocusIn:  g_input.focused=true; break;
    case FocusOut: g_input.focused=false; g_ctx.focused_input_id=0; break;
    case ClientMessage:
        if ((Atom)ev.xclient.data.l[0]==g_platform.wm_delete) g_platform.running=false; break;
    }
}

} // namespace internal

bool create_window(const Config& cfg) {
    using namespace internal;
    Display* dpy=XOpenDisplay(nullptr); if (!dpy) return false;
    g_platform.display=dpy;
    g_renderer.dpi_scale=detect_dpi(dpy)/96.0f;
    int s=DefaultScreen(dpy); Window root=RootWindow(dpy,s);
    int x=0,y=0;
    if (cfg.center_window){x=(DisplayWidth(dpy,s)-cfg.width)/2;y=(DisplayHeight(dpy,s)-cfg.height)/2;}
    g_platform.window=XCreateSimpleWindow(dpy,root,x,y,cfg.width,cfg.height,0,BlackPixel(dpy,s),BlackPixel(dpy,s));
    g_platform.wm_delete=XInternAtom(dpy,"WM_DELETE_WINDOW",False);
    XSetWMProtocols(dpy,g_platform.window,&g_platform.wm_delete,1);
    const char* title=cfg.title?cfg.title:"FTUI App";
    XStoreName(dpy,g_platform.window,title);
    Atom net_wm_name=XInternAtom(dpy,"_NET_WM_NAME",False);
    Atom utf8str=XInternAtom(dpy,"UTF8_STRING",False);
    XChangeProperty(dpy,g_platform.window,net_wm_name,utf8str,8,PropModeReplace,(unsigned char*)title,(int)strlen(title));
    XClassHint ch={(char*)"ftui",(char*)"ftui"}; XSetClassHint(dpy,g_platform.window,&ch);
    if (!cfg.resizable){XSizeHints sh={};sh.flags=PMinSize|PMaxSize;sh.min_width=sh.max_width=cfg.width;sh.min_height=sh.max_height=cfg.height;XSetWMNormalHints(dpy,g_platform.window,&sh);}
    XSelectInput(dpy,g_platform.window,ExposureMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask|KeyPressMask|KeyReleaseMask|StructureNotifyMask|FocusChangeMask);
    g_xim=XOpenIM(dpy,nullptr,nullptr,nullptr);
    if (g_xim) g_xic=XCreateIC(g_xim,XNInputStyle,(XIMPreeditNothing|XIMStatusNothing),XNClientWindow,g_platform.window,XNFocusWindow,g_platform.window,(void*)nullptr);
    XMapWindow(dpy,g_platform.window); XFlush(dpy);
    g_platform.width=cfg.width; g_platform.height=cfg.height; g_platform.running=true;
    g_style=default_dark_style();
    if (!init_cairo()) return false;
    clock_gettime(CLOCK_MONOTONIC,&g_last_time);
    return true;
}

bool pump() {
    using namespace internal;
    g_input.mouse_pressed=g_input.mouse_released=false;
    g_input.key_backspace=g_input.key_enter=g_input.key_tab=g_input.key_shift_tab=false;
    g_input.key_left=g_input.key_right=g_input.key_up=g_input.key_down=false;
    g_input.key_ctrl_c=g_input.key_ctrl_v=false;
    g_input.text_input_count=0; memset(g_input.text_input,0,sizeof(g_input.text_input));
    while (XPending(g_platform.display)) {
        XEvent ev; XNextEvent(g_platform.display,&ev); handle_xevent(ev);
        if (!g_platform.running) return false;
    }
    return g_platform.running;
}

void begin() {
    using namespace internal;
    g_ctx.hot_id=0;
    if ((g_input.key_tab||g_input.key_shift_tab)&&!g_ctx.tab_stops_prev.empty()) {
        auto& stops=g_ctx.tab_stops_prev;
        int cur=g_ctx.focused_input_id,idx=-1;
        for(int i=0;i<(int)stops.size();i++){if(stops[i]==cur){idx=i;break;}}
        g_ctx.focused_input_id=g_input.key_shift_tab?stops[(idx<=0?(int)stops.size():idx)-1]:stops[(idx+1)%(int)stops.size()];
        g_text_cursor_id=g_ta_cursor_id=0;
    }
    g_ctx.tab_stops.clear();
    float pad=g_style.window_padding;
    g_ctx.content_region={pad,pad,(float)g_platform.width-2*pad,(float)g_platform.height-2*pad};
    g_ctx.cursor_x=g_ctx.content_region.x;
    const float kSW=14; float vh=g_ctx.content_region.h;
    bool nsb=g_content_height>vh+1;
    float ms=nsb?g_content_height-vh:0;
    if(!nsb)g_scroll_y=0; g_scroll_y=g_scroll_y<0?0:(g_scroll_y>ms?ms:g_scroll_y);
    if(nsb)g_ctx.content_region.w-=kSW+pad;
    g_ctx.cursor_y=g_ctx.content_region.y-g_scroll_y;
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    float dt=(now.tv_sec-g_last_time.tv_sec)+(now.tv_nsec-g_last_time.tv_nsec)*1e-9f;
    g_last_time=now; g_fps_accum+=dt; g_fps_frames++;
    if(g_fps_accum>=0.5f){g_fps=(float)g_fps_frames/g_fps_accum;g_fps_frames=0;g_fps_accum=0;}
    if(!g_renderer.cr) return;
    apply_font();
    // clear
    set_color(g_style.background); cairo_paint(g_renderer.cr);
    // content clip
    cairo_save(g_renderer.cr);
    cairo_rectangle(g_renderer.cr,0,g_ctx.content_region.y,(float)g_platform.width,vh);
    cairo_clip(g_renderer.cr);
    g_drawing=true;
}

void end() {
    using namespace internal;
    if (!g_drawing) return;
    cairo_restore(g_renderer.cr); // pop content clip
    float new_ch=g_ctx.cursor_y+g_scroll_y-g_ctx.content_region.y;
    if(new_ch<0)new_ch=0;
    {
        const float kSW=14; float vh=g_ctx.content_region.h;
        bool nsb=g_content_height>vh+1;
        if(nsb){
            float ms=g_content_height-vh;
            float tx=g_ctx.content_region.x+g_ctx.content_region.w+g_style.window_padding;
            Rect track={tx,g_ctx.content_region.y,kSW,vh};
            float th=fmaxf(20,(vh/g_content_height)*vh);
            float tt=ms>0?g_scroll_y/ms:0;
            float ty2=g_ctx.content_region.y+tt*(vh-th);
            Rect thumb={tx,ty2,kSW,th};
            bool thov=rect_contains(thumb,g_input.mouse_x,g_input.mouse_y);
            bool trhov=rect_contains(track,g_input.mouse_x,g_input.mouse_y);
            if(g_input.mouse_pressed&&thov){g_sb_dragging=true;g_sb_drag_mouse_y=g_input.mouse_y;g_sb_drag_scroll0=g_scroll_y;}
            if(g_sb_dragging){
                if(g_input.mouse_down||g_input.mouse_released){float sc=ms/fmaxf(1,vh-th);g_scroll_y=g_sb_drag_scroll0+(g_input.mouse_y-g_sb_drag_mouse_y)*sc;g_scroll_y=g_scroll_y<0?0:(g_scroll_y>ms?ms:g_scroll_y);}
                if(g_input.mouse_released)g_sb_dragging=false;
            }
            if(g_input.mouse_pressed&&trhov&&!thov){g_scroll_y+=(g_input.mouse_y<ty2?-vh:vh);g_scroll_y=g_scroll_y<0?0:(g_scroll_y>ms?ms:g_scroll_y);}
            fill_round_rect(track,6,g_style.input_bg);
            fill_round_rect(thumb,6,g_sb_dragging?g_style.input_focus:thov?g_style.button_hover:g_style.border);
        }
        g_content_height=new_ch;
    }
    if(g_debug.show_fps){
        char buf[64]; snprintf(buf,sizeof(buf),"FPS: %.0f  frame:%d  DPI:%.0f%%",g_fps,g_ctx.frame_index,g_renderer.dpi_scale*100);
        float lh=text_line_height(); Rect r={4,4,300,lh}; fill_rect(r,{0,0,0,.6f}); draw_text_utf8(buf,r,g_style.text_dim);
    }
    if(g_debug.show_hovered_id||g_debug.show_active_id){
        char buf[128]; snprintf(buf,sizeof(buf),"hot=%d active=%d focused=%d",g_ctx.hot_id,g_ctx.active_id,g_ctx.focused_input_id);
        float lh=text_line_height(),oy=g_debug.show_fps?lh+6:4; Rect r={4,oy,360,lh}; fill_rect(r,{0,0,0,.6f}); draw_text_utf8(buf,r,g_style.text_dim);
    }
    if(g_cmd.active){
        char buf[32]; snprintf(buf,sizeof(buf),":%s_",g_cmd.buf);
        float lh=text_line_height(),oy=(float)g_platform.height-g_style.window_padding-lh;
        Rect r={g_style.window_padding,oy,200,lh};
        fill_rect({r.x-4,r.y-2,r.w+8,r.h+4},{0,0,0,.75f}); draw_text_utf8(buf,r,g_style.text);
    }
    g_drawing=false;
    swap_buffers();
    g_ctx.tab_stops_prev=g_ctx.tab_stops; g_ctx.frame_index++;
}

void shutdown() {
    using namespace internal;
    teardown_cairo();
    if(g_xic){XDestroyIC(g_xic);g_xic=nullptr;}
    if(g_xim){XCloseIM(g_xim);g_xim=nullptr;}
    if(g_platform.window){XDestroyWindow(g_platform.display,g_platform.window);g_platform.window=0;}
    if(g_platform.display){XCloseDisplay(g_platform.display);g_platform.display=nullptr;}
}

void open_child_window(const Config& cfg, std::function<void()> fn) {
    using namespace internal;
    if(!g_platform.display) return;
    struct Snap {
        PlatformState p; RendererState r; InputState in; UIContext ctx; Style sty; DebugState dbg;
        float sc,ch,sbmy,sbms0; bool sbd;
        CmdState cmd; float fps,fpsa; int fpsf;
        struct timespec lt;
        int tci,tc,tsa,taci,tac,taas; float tasc;
        bool shortcuts,drawing;
        XIM xim; XIC xic; std::string cb;
    } s;
    s.p=g_platform;s.r=g_renderer;s.in=g_input;s.ctx=g_ctx;s.sty=g_style;s.dbg=g_debug;
    s.sc=g_scroll_y;s.ch=g_content_height;s.sbd=g_sb_dragging;s.sbmy=g_sb_drag_mouse_y;s.sbms0=g_sb_drag_scroll0;
    s.cmd=g_cmd;s.fps=g_fps;s.fpsa=g_fps_accum;s.fpsf=g_fps_frames;s.lt=g_last_time;
    s.tci=g_text_cursor_id;s.tc=g_text_cursor;s.tsa=g_text_sel_anchor;
    s.taci=g_ta_cursor_id;s.tac=g_ta_cursor;s.taas=g_ta_sel_anchor;s.tasc=g_ta_scroll_y;
    s.shortcuts=g_shortcuts_enabled;s.drawing=g_drawing;s.xim=g_xim;s.xic=g_xic;s.cb=g_clipboard_buf;
    Display* dpy=g_platform.display; Window parent=g_platform.window;
    g_platform={}; g_platform.display=dpy;
    g_renderer={}; g_input={}; g_ctx={}; g_style=s.sty; g_debug=s.dbg;
    g_scroll_y=g_content_height=0; g_sb_dragging=false; g_cmd={};
    g_fps=g_fps_accum=0; g_fps_frames=0;
    g_text_cursor_id=g_text_cursor=g_text_sel_anchor=0;
    g_ta_cursor_id=g_ta_cursor=g_ta_sel_anchor=0; g_ta_scroll_y=0;
    g_shortcuts_enabled=s.shortcuts; g_drawing=false; g_clipboard_buf=s.cb;
    g_xim=nullptr; g_xic=nullptr;
    int sc=DefaultScreen(dpy); int cx=0,cy=0;
    if(cfg.center_window){cx=(DisplayWidth(dpy,sc)-cfg.width)/2;cy=(DisplayHeight(dpy,sc)-cfg.height)/2;}
    g_platform.window=XCreateSimpleWindow(dpy,RootWindow(dpy,sc),cx,cy,cfg.width,cfg.height,0,BlackPixel(dpy,sc),BlackPixel(dpy,sc));
    g_platform.wm_delete=XInternAtom(dpy,"WM_DELETE_WINDOW",False);
    XSetWMProtocols(dpy,g_platform.window,&g_platform.wm_delete,1);
    XSetTransientForHint(dpy,g_platform.window,parent);
    const char* t=cfg.title?cfg.title:"FTUI"; XStoreName(dpy,g_platform.window,t);
    Atom nwn=XInternAtom(dpy,"_NET_WM_NAME",False),us=XInternAtom(dpy,"UTF8_STRING",False);
    XChangeProperty(dpy,g_platform.window,nwn,us,8,PropModeReplace,(unsigned char*)t,(int)strlen(t));
    if(!cfg.resizable){XSizeHints sh={};sh.flags=PMinSize|PMaxSize;sh.min_width=sh.max_width=cfg.width;sh.min_height=sh.max_height=cfg.height;XSetWMNormalHints(dpy,g_platform.window,&sh);}
    XSelectInput(dpy,g_platform.window,ExposureMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask|KeyPressMask|KeyReleaseMask|StructureNotifyMask|FocusChangeMask);
    g_xim=XOpenIM(dpy,nullptr,nullptr,nullptr);
    if(g_xim) g_xic=XCreateIC(g_xim,XNInputStyle,(XIMPreeditNothing|XIMStatusNothing),XNClientWindow,g_platform.window,XNFocusWindow,g_platform.window,(void*)nullptr);
    g_platform.width=cfg.width;g_platform.height=cfg.height;g_platform.running=true;
    XMapWindow(dpy,g_platform.window); XFlush(dpy);
    g_renderer.dpi_scale=s.r.dpi_scale;
    if(init_cairo()){clock_gettime(CLOCK_MONOTONIC,&g_last_time);while(pump()){begin();fn();end();}}
    teardown_cairo();
    if(g_xic){XDestroyIC(g_xic);g_xic=nullptr;}
    if(g_xim){XCloseIM(g_xim);g_xim=nullptr;}
    if(g_platform.window){XDestroyWindow(dpy,g_platform.window);g_platform.window=0;} XFlush(dpy);
    g_platform=s.p;g_renderer=s.r;g_input=s.in;g_ctx=s.ctx;g_style=s.sty;g_debug=s.dbg;
    g_scroll_y=s.sc;g_content_height=s.ch;g_sb_dragging=s.sbd;g_sb_drag_mouse_y=s.sbmy;g_sb_drag_scroll0=s.sbms0;
    g_cmd=s.cmd;g_fps=s.fps;g_fps_accum=s.fpsa;g_fps_frames=s.fpsf;g_last_time=s.lt;
    g_text_cursor_id=s.tci;g_text_cursor=s.tc;g_text_sel_anchor=s.tsa;
    g_ta_cursor_id=s.taci;g_ta_cursor=s.tac;g_ta_sel_anchor=s.taas;g_ta_scroll_y=s.tasc;
    g_shortcuts_enabled=s.shortcuts;g_drawing=s.drawing;g_xim=s.xim;g_xic=s.xic;g_clipboard_buf=s.cb;
    // trigger repaint of parent
    XExposeEvent xe={}; xe.type=Expose; xe.window=g_platform.window; xe.count=0;
    XSendEvent(dpy,g_platform.window,False,ExposureMask,(XEvent*)&xe); XFlush(dpy);
}

ImageHandle* load_image(const char* utf8_path) {
    if (!utf8_path||!utf8_path[0]) return nullptr;
    cairo_surface_t* surf=cairo_image_surface_create_from_png(utf8_path);
    if (!surf||cairo_surface_status(surf)!=CAIRO_STATUS_SUCCESS) {
        if(surf)cairo_surface_destroy(surf);
        internal::dbg("ftui: load_image failed for \"%s\"\n",utf8_path);
        return nullptr;
    }
    auto* li=new internal::LinuxImage{surf};
    auto* h=new ImageHandle(); h->_impl=li; return h;
}
void free_image(ImageHandle* img) {
    if (!img) return;
    if (img->_impl) { auto* li=static_cast<internal::LinuxImage*>(img->_impl); if(li->surface)cairo_surface_destroy(li->surface); delete li; img->_impl=nullptr; }
    delete img;
}

std::string open_file_dialog(const char* title, const FileFilter* filters, int filter_count) {
    std::string cmd="zenity --file-selection";
    if (title&&title[0]) { cmd+=" --title='"; for(const char*p=title;*p;p++){if(*p=='\'')cmd+="'\\''";else cmd+=*p;} cmd+="'"; }
    for (int i=0;i<filter_count;i++) {
        cmd+=" --file-filter='";
        if(filters[i].name){for(const char*p=filters[i].name;*p;p++){if(*p=='\'')cmd+="'\\''";else cmd+=*p;}}
        cmd+=" | ";
        if(filters[i].spec){std::string sp=filters[i].spec;for(char&c:sp)if(c==';')c=' ';cmd+=sp;}
        cmd+="'";
    }
    cmd+=" 2>/dev/null";
    FILE* f=popen(cmd.c_str(),"r"); if(!f) return "";
    std::string result; char buf[4096];
    while(fgets(buf,sizeof(buf),f)) result+=buf;
    pclose(f);
    while(!result.empty()&&(result.back()=='\n'||result.back()=='\r')) result.pop_back();
    return result;
}

#else
    #error "ftui.hpp: unsupported platform (Windows and Linux only)"
#endif // platform


// ============================================================
// Shared Widget Implementations
// ============================================================

using namespace internal;

void text(const char* label) {
    if (!g_drawing) return;
    char vis[256]; const char* hs;
    split_label(label, vis, sizeof(vis), &hs);
    float h = g_style.item_height;
    Rect r = next_rect(h);
    if (g_debug.show_layout_rects) stroke_round_rect(r, 0, 1, {1,0,0,0.4f});
    draw_text_utf8(vis, r, g_style.text);
}

void separator() {
    if (!g_drawing) return;
    float h = g_style.item_spacing * 2 + 1;
    Rect r = next_rect(h);
    float my = r.y + r.h * 0.5f;
    draw_line(r.x, my, r.x + r.w, my, 1.0f, g_style.border);
}

void spacing(float px) {
    if (!g_drawing) return;
    next_rect(px);
}

bool button(const char* label) {
    if (!g_drawing) return false;
    char vis[256]; const char* hs;
    split_label(label, vis, sizeof(vis), &hs);
    int id = hash_str(hs);

    Rect r = next_rect(g_style.item_height);
    bool hov = rect_contains(r, g_input.mouse_x, g_input.mouse_y);
    bool clicked = false;

    if (hov && g_input.mouse_pressed) { g_ctx.active_id = id; }
    if (g_ctx.active_id == id && g_input.mouse_released) {
        if (hov) clicked = true;
        g_ctx.active_id = 0;
    }
    if (hov) g_ctx.hot_id = id;

    Color bg = (g_ctx.active_id == id) ? g_style.button_active
             : (hov)                   ? g_style.button_hover
                                       : g_style.button;
    fill_round_rect(r, g_style.rounding, bg);
    stroke_round_rect(r, g_style.rounding, g_style.border_width, g_style.border);
    draw_text_utf8_centered(vis, r, g_style.text);

    if (g_debug.show_layout_rects) stroke_round_rect(r, 0, 1, {0,1,0,0.4f});
    return clicked;
}

bool input(const char* label, char* buffer, int buffer_size,
           InputFlags flags, bool* enter_pressed) {
    if (!g_drawing) return false;
    char vis[128]; const char* hs;
    split_label(label, vis, sizeof(vis), &hs);
    int id = hash_str(hs);

    // Register tab stop
    g_ctx.tab_stops.push_back(id);

    Rect r = next_rect(g_style.item_height);
    bool hov = rect_contains(r, g_input.mouse_x, g_input.mouse_y);

    // Click to focus
    if (hov && g_input.mouse_pressed) {
        g_ctx.focused_input_id = id;
        g_text_cursor_id = id;
        int len = (int)strlen(buffer);
        // Place cursor at click position
        float inner_x = r.x + 8.0f;
        g_text_cursor = byte_from_x(buffer, g_input.mouse_x - inner_x);
        g_text_sel_anchor = g_text_cursor;
    }

    bool focused = (g_ctx.focused_input_id == id);
    bool changed = false;

    if (focused) {
        if (g_text_cursor_id != id) {
            g_text_cursor_id = id;
            g_text_cursor = (int)strlen(buffer);
            g_text_sel_anchor = g_text_cursor;
        }
        int len = (int)strlen(buffer);
        if (g_text_cursor > len) g_text_cursor = len;
        if (g_text_sel_anchor > len) g_text_sel_anchor = len;

        bool ro = (flags & InputFlags::ReadOnly);

        // Ctrl+A: select all
        if (g_input.ctrl_held && g_input.text_input_count > 0 && g_input.text_input[0] == 1) {
            g_text_sel_anchor = 0; g_text_cursor = len;
        }

        // Arrow keys
        if (g_input.key_left) {
            if (g_input.shift_held) {
                g_text_cursor = utf8_retreat(buffer, g_text_cursor);
            } else if (g_text_cursor != g_text_sel_anchor) {
                g_text_cursor = g_text_sel_anchor < g_text_cursor ? g_text_sel_anchor : g_text_cursor;
                g_text_sel_anchor = g_text_cursor;
            } else {
                g_text_cursor = utf8_retreat(buffer, g_text_cursor);
                g_text_sel_anchor = g_text_cursor;
            }
        }
        if (g_input.key_right) {
            if (g_input.shift_held) {
                g_text_cursor = utf8_advance(buffer, g_text_cursor);
            } else if (g_text_cursor != g_text_sel_anchor) {
                g_text_cursor = g_text_sel_anchor > g_text_cursor ? g_text_sel_anchor : g_text_cursor;
                g_text_sel_anchor = g_text_cursor;
            } else {
                g_text_cursor = utf8_advance(buffer, g_text_cursor);
                g_text_sel_anchor = g_text_cursor;
            }
        }

        // Ctrl+C
        if (g_input.key_ctrl_c && g_text_sel_anchor != g_text_cursor) {
            int lo = g_text_sel_anchor < g_text_cursor ? g_text_sel_anchor : g_text_cursor;
            int hi = g_text_sel_anchor > g_text_cursor ? g_text_sel_anchor : g_text_cursor;
            std::string sel(buffer + lo, buffer + hi);
            clipboard_set(sel.c_str());
        }

        // Ctrl+V
        if (g_input.key_ctrl_v && !ro) {
            std::string cb = clipboard_get();
            if (!cb.empty()) {
                int lo = g_text_sel_anchor < g_text_cursor ? g_text_sel_anchor : g_text_cursor;
                int hi = g_text_sel_anchor > g_text_cursor ? g_text_sel_anchor : g_text_cursor;
                if (lo != hi) { memmove(buffer+lo, buffer+hi, len-hi+1); len -= (hi-lo); g_text_cursor = lo; }
                int ins = (int)cb.size();
                if (len + ins < buffer_size - 1) {
                    memmove(buffer+g_text_cursor+ins, buffer+g_text_cursor, len-g_text_cursor+1);
                    memcpy(buffer+g_text_cursor, cb.c_str(), ins);
                    g_text_cursor += ins;
                }
                g_text_sel_anchor = g_text_cursor;
                changed = true;
            }
        }

        // Backspace
        if (g_input.key_backspace && !ro) {
            if (g_text_cursor != g_text_sel_anchor) {
                int lo = g_text_sel_anchor < g_text_cursor ? g_text_sel_anchor : g_text_cursor;
                int hi = g_text_sel_anchor > g_text_cursor ? g_text_sel_anchor : g_text_cursor;
                memmove(buffer+lo, buffer+hi, len-hi+1);
                g_text_cursor = g_text_sel_anchor = lo;
                changed = true;
            } else if (g_text_cursor > 0) {
                int prev = utf8_retreat(buffer, g_text_cursor);
                memmove(buffer+prev, buffer+g_text_cursor, len-g_text_cursor+1);
                g_text_cursor = g_text_sel_anchor = prev;
                changed = true;
            }
        }

        // Printable input
        if (!ro && g_input.text_input_count > 0) {
            // Delete selection first
            if (g_text_cursor != g_text_sel_anchor) {
                int lo = g_text_sel_anchor < g_text_cursor ? g_text_sel_anchor : g_text_cursor;
                int hi = g_text_sel_anchor > g_text_cursor ? g_text_sel_anchor : g_text_cursor;
                memmove(buffer+lo, buffer+hi, len-hi+1);
                len -= (hi-lo); g_text_cursor = g_text_sel_anchor = lo;
            }
            for (int i = 0; i < g_input.text_input_count && g_input.text_input[i]; ) {
                unsigned char c = (unsigned char)g_input.text_input[i];
                int cs = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
                if (len + cs < buffer_size - 1) {
                    memmove(buffer+g_text_cursor+cs, buffer+g_text_cursor, len-g_text_cursor+1);
                    memcpy(buffer+g_text_cursor, g_input.text_input+i, cs);
                    g_text_cursor += cs; len += cs;
                }
                i += cs;
            }
            g_text_sel_anchor = g_text_cursor;
            changed = true;
        }

        // Enter
        if (g_input.key_enter) {
            if (enter_pressed) *enter_pressed = true;
        }

        // Tab / Shift+Tab
        if (g_input.key_tab || g_input.key_shift_tab) {
            auto& ts = g_ctx.tab_stops_prev;
            if (!ts.empty()) {
                int pos = 0;
                for (int i = 0; i < (int)ts.size(); i++) if (ts[i] == id) { pos = i; break; }
                if (g_input.key_shift_tab) pos = (pos - 1 + (int)ts.size()) % (int)ts.size();
                else                       pos = (pos + 1) % (int)ts.size();
                g_ctx.focused_input_id = ts[pos];
                g_text_cursor_id = 0; // reset so new widget picks up cursor position
            }
        }
    }

    // Draw
    Color border_col = focused ? g_style.input_focus : g_style.border;
    fill_round_rect(r, g_style.rounding, g_style.input_bg);
    stroke_round_rect(r, g_style.rounding, focused ? 2.0f : g_style.border_width, border_col);

    float pad = 8.0f;
    Rect inner = {r.x + pad, r.y, r.w - pad * 2 - 80.0f, r.h};

    // Build display string
    int len = (int)strlen(buffer);
    std::string disp;
    if (flags & InputFlags::Password) disp = std::string(utf8_char_count(buffer, len), '*');
    else disp = buffer;

    // Scroll so cursor is visible
    static float s_scroll[256] = {}; // per-hash scroll offset (limited slots)
    int slot = (id ^ (id >> 8)) & 0xFF;
    float& scroll_x = s_scroll[slot];
    if (focused) {
        float cx = measure_text_at(disp.c_str(), g_text_cursor);
        float vis_w = inner.w;
        if (cx - scroll_x > vis_w - 4) scroll_x = cx - vis_w + 4;
        if (cx - scroll_x < 0)         scroll_x = cx;
        if (scroll_x < 0) scroll_x = 0;
    }

    push_clip(inner);

    // Selection highlight
    if (focused && g_text_sel_anchor != g_text_cursor) {
        int lo = g_text_sel_anchor < g_text_cursor ? g_text_sel_anchor : g_text_cursor;
        int hi = g_text_sel_anchor > g_text_cursor ? g_text_sel_anchor : g_text_cursor;
        float sx = inner.x - scroll_x + measure_text_at(disp.c_str(), lo);
        float ex = inner.x - scroll_x + measure_text_at(disp.c_str(), hi);
        Rect sel_r = {sx, r.y + 4, ex - sx, r.h - 8};
        Color sel_col = g_style.input_focus; sel_col.a = 0.35f;
        fill_rect(sel_r, sel_col);
    }

    Rect text_r = {inner.x - scroll_x, inner.y, inner.w + scroll_x, inner.h};
    draw_text_utf8(disp.c_str(), text_r, g_style.text);

    // Cursor blink
    if (focused && (g_ctx.frame_index / 30) % 2 == 0) {
        float cx = inner.x - scroll_x + measure_text_at(disp.c_str(), g_text_cursor);
        draw_line(cx, r.y + 5, cx, r.y + r.h - 5, 1.5f, g_style.text);
    }

    pop_clip();

    // Label
    Rect lbl_r = {r.x + r.w - 78.0f, r.y, 76.0f, r.h};
    draw_text_utf8(vis, lbl_r, g_style.text_dim);

    if (g_debug.show_layout_rects) stroke_round_rect(r, 0, 1, {0,0,1,0.4f});
    return changed;
}

bool text_area(const char* label, char* buffer, int buffer_size, int rows) {
    if (!g_drawing) return false;
    char vis[128]; const char* hs;
    split_label(label, vis, sizeof(vis), &hs);
    int id = hash_str(hs);

    g_ctx.tab_stops.push_back(id);

    float lh = text_line_height();
    float widget_h = rows * lh + 8.0f;
    Rect r = next_rect(widget_h);
    bool hov = rect_contains(r, g_input.mouse_x, g_input.mouse_y);

    if (hov && g_input.mouse_pressed) {
        g_ctx.focused_input_id = id;
        g_ta_cursor_id = id;
    }

    bool focused = (g_ctx.focused_input_id == id);
    bool changed = false;

    if (focused) {
        if (g_ta_cursor_id != id) {
            g_ta_cursor_id = id;
            g_ta_cursor = (int)strlen(buffer);
            g_ta_sel_anchor = g_ta_cursor;
            g_ta_scroll_y = 0;
        }
        int len = (int)strlen(buffer);
        if (g_ta_cursor > len) g_ta_cursor = len;
        if (g_ta_sel_anchor > len) g_ta_sel_anchor = len;

        // Ctrl+A
        if (g_input.ctrl_held && g_input.text_input_count > 0 && g_input.text_input[0] == 1) {
            g_ta_sel_anchor = 0; g_ta_cursor = len;
        }

        // Ctrl+C
        if (g_input.key_ctrl_c && g_ta_sel_anchor != g_ta_cursor) {
            int lo = g_ta_sel_anchor < g_ta_cursor ? g_ta_sel_anchor : g_ta_cursor;
            int hi = g_ta_sel_anchor > g_ta_cursor ? g_ta_sel_anchor : g_ta_cursor;
            std::string sel(buffer+lo, buffer+hi);
            clipboard_set(sel.c_str());
        }

        // Ctrl+V
        if (g_input.key_ctrl_v) {
            std::string cb = clipboard_get();
            if (!cb.empty()) {
                if (g_ta_cursor != g_ta_sel_anchor) {
                    int lo = g_ta_sel_anchor < g_ta_cursor ? g_ta_sel_anchor : g_ta_cursor;
                    int hi = g_ta_sel_anchor > g_ta_cursor ? g_ta_sel_anchor : g_ta_cursor;
                    memmove(buffer+lo, buffer+hi, len-hi+1); len -= (hi-lo); g_ta_cursor = lo;
                }
                int ins = (int)cb.size();
                if (len + ins < buffer_size - 1) {
                    memmove(buffer+g_ta_cursor+ins, buffer+g_ta_cursor, len-g_ta_cursor+1);
                    memcpy(buffer+g_ta_cursor, cb.c_str(), ins);
                    g_ta_cursor += ins;
                }
                g_ta_sel_anchor = g_ta_cursor;
                changed = true;
            }
        }

        // Backspace
        if (g_input.key_backspace) {
            if (g_ta_cursor != g_ta_sel_anchor) {
                int lo = g_ta_sel_anchor < g_ta_cursor ? g_ta_sel_anchor : g_ta_cursor;
                int hi = g_ta_sel_anchor > g_ta_cursor ? g_ta_sel_anchor : g_ta_cursor;
                memmove(buffer+lo, buffer+hi, len-hi+1);
                g_ta_cursor = g_ta_sel_anchor = lo;
                changed = true;
            } else if (g_ta_cursor > 0) {
                int prev = utf8_retreat(buffer, g_ta_cursor);
                memmove(buffer+prev, buffer+g_ta_cursor, len-g_ta_cursor+1);
                g_ta_cursor = g_ta_sel_anchor = prev;
                changed = true;
            }
        }

        // Enter: insert newline
        if (g_input.key_enter) {
            if (len + 1 < buffer_size - 1) {
                memmove(buffer+g_ta_cursor+1, buffer+g_ta_cursor, len-g_ta_cursor+1);
                buffer[g_ta_cursor] = '\n';
                g_ta_cursor++;
                g_ta_sel_anchor = g_ta_cursor;
                changed = true;
            }
        }

        // Left/Right
        if (g_input.key_left) {
            if (g_input.shift_held) {
                g_ta_cursor = utf8_retreat(buffer, g_ta_cursor);
            } else if (g_ta_cursor != g_ta_sel_anchor) {
                g_ta_cursor = g_ta_sel_anchor < g_ta_cursor ? g_ta_sel_anchor : g_ta_cursor;
                g_ta_sel_anchor = g_ta_cursor;
            } else {
                g_ta_cursor = utf8_retreat(buffer, g_ta_cursor);
                g_ta_sel_anchor = g_ta_cursor;
            }
        }
        if (g_input.key_right) {
            if (g_input.shift_held) {
                g_ta_cursor = utf8_advance(buffer, g_ta_cursor);
            } else if (g_ta_cursor != g_ta_sel_anchor) {
                g_ta_cursor = g_ta_sel_anchor > g_ta_cursor ? g_ta_sel_anchor : g_ta_cursor;
                g_ta_sel_anchor = g_ta_cursor;
            } else {
                g_ta_cursor = utf8_advance(buffer, g_ta_cursor);
                g_ta_sel_anchor = g_ta_cursor;
            }
        }

        // Up/Down: move to same column on adjacent line
        if (g_input.key_up || g_input.key_down) {
            // Find which line cursor is on and its line-start
            int line_start = 0, line_num = 0;
            int cur_line_start = 0;
            for (int i = 0; i < g_ta_cursor; i++) {
                if (buffer[i] == '\n') { line_num++; line_start = i + 1; }
            }
            cur_line_start = line_start;
            float col_x = measure_text_at(buffer + cur_line_start, g_ta_cursor - cur_line_start);

            if (g_input.key_up && line_num > 0) {
                // Find start of previous line
                int prev_end = cur_line_start - 1; // the '\n'
                int prev_start = 0;
                for (int i = 0; i < prev_end; i++) if (buffer[i] == '\n') prev_start = i + 1;
                int prev_len = prev_end - prev_start;
                int new_pos = prev_start + byte_from_x(buffer + prev_start, col_x);
                if (new_pos > prev_start + prev_len) new_pos = prev_start + prev_len;
                g_ta_cursor = new_pos;
                if (!g_input.shift_held) g_ta_sel_anchor = g_ta_cursor;
            } else if (g_input.key_down) {
                // Find start of next line
                int next_start = g_ta_cursor;
                while (next_start < len && buffer[next_start] != '\n') next_start++;
                if (next_start < len) {
                    next_start++; // skip '\n'
                    int next_end = next_start;
                    while (next_end < len && buffer[next_end] != '\n') next_end++;
                    int new_pos = next_start + byte_from_x(buffer + next_start, col_x);
                    if (new_pos > next_end) new_pos = next_end;
                    g_ta_cursor = new_pos;
                    if (!g_input.shift_held) g_ta_sel_anchor = g_ta_cursor;
                }
            }
        }

        // Click: set cursor
        if (hov && g_input.mouse_pressed) {
            float inner_x = r.x + 6.0f;
            float inner_y = r.y + 4.0f - g_ta_scroll_y;
            int line_idx = (int)((g_input.mouse_y - inner_y) / lh);
            if (line_idx < 0) line_idx = 0;
            // Find the Nth line
            int cur_line = 0, line_start = 0;
            int p = 0;
            while (p <= len) {
                if (cur_line == line_idx) {
                    int line_end = p;
                    while (line_end < len && buffer[line_end] != '\n') line_end++;
                    int clicked_off = byte_from_x(buffer + line_start, g_input.mouse_x - inner_x);
                    if (clicked_off > line_end - line_start) clicked_off = line_end - line_start;
                    g_ta_cursor = line_start + clicked_off;
                    g_ta_sel_anchor = g_ta_cursor;
                    break;
                }
                if (p == len) break;
                if (buffer[p] == '\n') { cur_line++; line_start = p + 1; }
                p++;
            }
            if (line_idx > cur_line) { g_ta_cursor = len; g_ta_sel_anchor = len; }
        }

        // Printable input (skip control chars except those handled above)
        if (g_input.text_input_count > 0) {
            if (g_ta_cursor != g_ta_sel_anchor) {
                int lo = g_ta_sel_anchor < g_ta_cursor ? g_ta_sel_anchor : g_ta_cursor;
                int hi = g_ta_sel_anchor > g_ta_cursor ? g_ta_sel_anchor : g_ta_cursor;
                memmove(buffer+lo, buffer+hi, len-hi+1);
                len -= (hi-lo); g_ta_cursor = g_ta_sel_anchor = lo;
            }
            for (int i = 0; i < g_input.text_input_count && g_input.text_input[i]; ) {
                unsigned char c = (unsigned char)g_input.text_input[i];
                int cs = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
                if (len + cs < buffer_size - 1) {
                    memmove(buffer+g_ta_cursor+cs, buffer+g_ta_cursor, len-g_ta_cursor+1);
                    memcpy(buffer+g_ta_cursor, g_input.text_input+i, cs);
                    g_ta_cursor += cs; len += cs;
                }
                i += cs;
            }
            g_ta_sel_anchor = g_ta_cursor;
            changed = true;
        }

        // Tab: cycle focus
        if (g_input.key_tab || g_input.key_shift_tab) {
            auto& ts = g_ctx.tab_stops_prev;
            if (!ts.empty()) {
                int pos = 0;
                for (int i = 0; i < (int)ts.size(); i++) if (ts[i] == id) { pos = i; break; }
                if (g_input.key_shift_tab) pos = (pos - 1 + (int)ts.size()) % (int)ts.size();
                else                       pos = (pos + 1) % (int)ts.size();
                g_ctx.focused_input_id = ts[pos];
                g_ta_cursor_id = 0;
            }
        }

        // Scroll so cursor is visible
        int cur_line = 0;
        for (int i = 0; i < g_ta_cursor; i++) if (buffer[i] == '\n') cur_line++;
        float cursor_y_in_widget = cur_line * lh;
        float visible_h = widget_h - 8.0f;
        if (cursor_y_in_widget - g_ta_scroll_y < 0)          g_ta_scroll_y = cursor_y_in_widget;
        if (cursor_y_in_widget + lh - g_ta_scroll_y > visible_h) g_ta_scroll_y = cursor_y_in_widget + lh - visible_h;
        if (g_ta_scroll_y < 0) g_ta_scroll_y = 0;
    }

    // Mouse-wheel scroll when hovered
    // (scroll_y adjustments are done per-frame in pump(); re-use g_scroll_y delta isn't applicable here,
    //  so platforms must set g_ta_scroll_y directly or we rely on the pump-level handling)

    // --- Drawing ---
    Color border_col = focused ? g_style.input_focus : g_style.border;
    fill_round_rect(r, g_style.rounding, g_style.input_bg);
    stroke_round_rect(r, g_style.rounding, focused ? 2.0f : g_style.border_width, border_col);

    Rect clip_r = {r.x + 2, r.y + 2, r.w - 4, r.h - 4};
    push_clip(clip_r);

    float pad_x = 6.0f, pad_y = 4.0f;
    int len = (int)strlen(buffer);

    // Count total lines
    int total_lines = 1;
    for (int i = 0; i < len; i++) if (buffer[i] == '\n') total_lines++;

    // Compute selection range
    int sel_lo = g_ta_sel_anchor < g_ta_cursor ? g_ta_sel_anchor : g_ta_cursor;
    int sel_hi = g_ta_sel_anchor > g_ta_cursor ? g_ta_sel_anchor : g_ta_cursor;

    // Draw each line
    int line_start_byte = 0;
    for (int li = 0; li < total_lines; li++) {
        int line_end_byte = line_start_byte;
        while (line_end_byte < len && buffer[line_end_byte] != '\n') line_end_byte++;

        float line_y = r.y + pad_y - g_ta_scroll_y + li * lh;
        if (line_y + lh < r.y || line_y > r.y + r.h) {
            line_start_byte = line_end_byte + 1;
            continue;
        }

        // Selection highlight for this line
        if (focused && sel_lo != sel_hi) {
            int line_sel_lo = sel_lo < line_start_byte ? line_start_byte : sel_lo;
            int line_sel_hi = sel_hi > line_end_byte   ? line_end_byte   : sel_hi;
            if (line_sel_lo < line_sel_hi) {
                float sx = r.x + pad_x + measure_text_at(buffer + line_start_byte, line_sel_lo - line_start_byte);
                float ex = r.x + pad_x + measure_text_at(buffer + line_start_byte, line_sel_hi - line_start_byte);
                Rect sel_r = {sx, line_y, ex - sx, lh};
                Color sc = g_style.input_focus; sc.a = 0.35f;
                fill_rect(sel_r, sc);
            }
        }

        // Draw line text
        if (line_end_byte > line_start_byte) {
            std::string line_str(buffer + line_start_byte, buffer + line_end_byte);
            Rect lr = {r.x + pad_x, line_y, r.w - pad_x * 2, lh};
            draw_text_utf8(line_str.c_str(), lr, g_style.text);
        }

        // Draw cursor on this line
        if (focused && g_ta_cursor >= line_start_byte && g_ta_cursor <= line_end_byte) {
            if ((g_ctx.frame_index / 30) % 2 == 0) {
                float cx = r.x + pad_x + measure_text_at(buffer + line_start_byte, g_ta_cursor - line_start_byte);
                draw_line(cx, line_y + 2, cx, line_y + lh - 2, 1.5f, g_style.text);
            }
        }

        line_start_byte = line_end_byte + 1;
    }

    pop_clip();

    // Scrollbar
    float total_h = total_lines * lh;
    if (total_h > r.h - 8) {
        float sb_w = 6.0f, sb_x = r.x + r.w - sb_w - 2;
        float visible_h = r.h - 8;
        float thumb_h = (visible_h / total_h) * visible_h;
        if (thumb_h < 12) thumb_h = 12;
        float thumb_y = r.y + 4 + (g_ta_scroll_y / (total_h - visible_h)) * (visible_h - thumb_h);
        Rect track_r = {sb_x, r.y + 4, sb_w, visible_h};
        Rect thumb_r = {sb_x, thumb_y, sb_w, thumb_h};
        Color track_c = g_style.border; track_c.a = 0.3f;
        fill_round_rect(track_r, sb_w * 0.5f, track_c);
        fill_round_rect(thumb_r, sb_w * 0.5f, g_style.text_dim);
    }

    // Label above/to the right
    if (vis[0]) {
        float lbl_w = 80.0f;
        Rect lbl_r = {r.x + r.w + 4, r.y, lbl_w, g_style.item_height};
        draw_text_utf8(vis, lbl_r, g_style.text_dim);
    }

    if (g_debug.show_layout_rects) stroke_round_rect(r, 0, 1, {0,0,1,0.4f});
    return changed;
}

bool checkbox(const char* label, bool* value) {
    if (!g_drawing) return false;
    char vis[128]; const char* hs;
    split_label(label, vis, sizeof(vis), &hs);
    int id = hash_str(hs);

    Rect r = next_rect(g_style.item_height);
    bool hov = rect_contains(r, g_input.mouse_x, g_input.mouse_y);
    bool clicked = false;

    if (hov && g_input.mouse_pressed) g_ctx.active_id = id;
    if (g_ctx.active_id == id && g_input.mouse_released) {
        if (hov) { *value = !*value; clicked = true; }
        g_ctx.active_id = 0;
    }

    float box_sz = g_style.item_height - 8;
    Rect box = {r.x, r.y + (r.h - box_sz) * 0.5f, box_sz, box_sz};
    Color bg = hov ? g_style.button_hover : g_style.input_bg;
    fill_round_rect(box, g_style.rounding * 0.5f, bg);
    stroke_round_rect(box, g_style.rounding * 0.5f, g_style.border_width, *value ? g_style.input_focus : g_style.border);

    if (*value) {
        // Checkmark: two lines forming a tick
        float m = 3.0f;
        draw_line(box.x+m, box.y+box_sz*0.5f, box.x+box_sz*0.4f, box.y+box_sz-m, 2, g_style.input_focus);
        draw_line(box.x+box_sz*0.4f, box.y+box_sz-m, box.x+box_sz-m, box.y+m, 2, g_style.input_focus);
    }

    Rect lbl_r = {box.x + box_sz + 8, r.y, r.w - box_sz - 8, r.h};
    draw_text_utf8(vis, lbl_r, g_style.text);

    if (g_debug.show_layout_rects) stroke_round_rect(r, 0, 1, {1,0,1,0.4f});
    return clicked;
}

bool slider_float(const char* label, float* value, float min_v, float max_v) {
    if (!g_drawing) return false;
    char vis[128]; const char* hs;
    split_label(label, vis, sizeof(vis), &hs);
    int id = hash_str(hs);

    Rect r = next_rect(g_style.item_height);
    float lbl_w = 90.0f;
    Rect track_r = {r.x, r.y + r.h * 0.5f - 3, r.w - lbl_w, 6};
    bool hov = rect_contains(r, g_input.mouse_x, g_input.mouse_y);
    bool changed = false;

    if (hov && g_input.mouse_pressed) g_ctx.active_id = id;
    if (g_ctx.active_id == id) {
        if (g_input.mouse_released) g_ctx.active_id = 0;
        else {
            float t = (g_input.mouse_x - track_r.x) / track_r.w;
            if (t < 0) t = 0; if (t > 1) t = 1;
            *value = min_v + t * (max_v - min_v);
            changed = true;
        }
    }

    fill_round_rect(track_r, 3, g_style.input_bg);
    stroke_round_rect(track_r, 3, g_style.border_width, g_style.border);

    float t = (*value - min_v) / (max_v - min_v);
    if (t < 0) t = 0; if (t > 1) t = 1;
    Rect fill_r = {track_r.x, track_r.y, track_r.w * t, track_r.h};
    if (fill_r.w > 0) fill_round_rect(fill_r, 3, g_style.input_focus);

    float knob_sz = 14.0f;
    Rect knob = {track_r.x + track_r.w * t - knob_sz * 0.5f, r.y + r.h * 0.5f - knob_sz * 0.5f, knob_sz, knob_sz};
    Color knob_col = (g_ctx.active_id == id) ? g_style.button_active : (hov ? g_style.button_hover : g_style.button);
    fill_round_rect(knob, knob_sz * 0.5f, knob_col);
    stroke_round_rect(knob, knob_sz * 0.5f, g_style.border_width, g_style.input_focus);

    char val_str[64];
    snprintf(val_str, sizeof(val_str), "%s  %.2f", vis, *value);
    Rect lbl_r2 = {r.x + r.w - lbl_w + 4, r.y, lbl_w - 4, r.h};
    draw_text_utf8(val_str, lbl_r2, g_style.text_dim);

    if (g_debug.show_layout_rects) stroke_round_rect(r, 0, 1, {1,1,0,0.4f});
    return changed;
}

void image(ImageHandle* img, float width, float height) {
    if (!g_drawing) return;
    Rect r = next_rect(height);
    if (width < r.w) r.w = width;
    draw_image_handle(img, r);
    if (g_debug.show_layout_rects) stroke_round_rect(r, 0, 1, {0,1,1,0.4f});
}

bool tabs(const char* const* labels, int count, int* selected) {
    if (!g_drawing || count <= 0) return false;
    float tab_h = g_style.item_height;
    Rect r = next_rect(tab_h);
    float tab_w = r.w / count;
    bool changed = false;

    for (int i = 0; i < count; i++) {
        char vis[128]; const char* hs;
        split_label(labels[i], vis, sizeof(vis), &hs);
        int id = hash_str(hs) ^ (i * 31337);

        Rect tr = {r.x + i * tab_w, r.y, tab_w, tab_h};
        bool hov = rect_contains(tr, g_input.mouse_x, g_input.mouse_y);

        if (hov && g_input.mouse_pressed) g_ctx.active_id = id;
        if (g_ctx.active_id == id && g_input.mouse_released) {
            if (hov && *selected != i) { *selected = i; changed = true; }
            g_ctx.active_id = 0;
        }

        bool sel = (*selected == i);
        Color bg = sel ? g_style.panel : (hov ? g_style.button_hover : g_style.button);
        fill_round_rect(tr, g_style.rounding, bg);

        if (sel) {
            // Accent line at bottom
            Rect accent = {tr.x + 4, tr.y + tr.h - 3, tr.w - 8, 3};
            fill_round_rect(accent, 1.5f, g_style.input_focus);
        }
        stroke_round_rect(tr, g_style.rounding, g_style.border_width, g_style.border);
        draw_text_utf8_centered(vis, tr, sel ? g_style.text : g_style.text_dim);
    }

    if (g_debug.show_layout_rects) stroke_round_rect(r, 0, 1, {0.5f,0,1,0.4f});
    return changed;
}

void row(int cols, std::function<void()> fn) {
    if (!g_drawing || cols <= 0) return;
    auto& rc = g_ctx.row_ctx;
    float gap = g_style.item_spacing;
    rc.active   = true;
    rc.cols     = cols;
    rc.gap      = gap;
    rc.cell_w   = (g_ctx.content_region.w - gap * (cols - 1)) / cols;
    rc.start_x  = g_ctx.cursor_x;
    rc.start_y  = g_ctx.cursor_y;
    rc.col_index = 0;
    rc.row_height = 0;
    fn();
    rc.active = false;
    g_ctx.cursor_y += rc.row_height + g_style.item_spacing;
}

} // namespace ftui

#endif // FTUI_IMPLEMENTATION


