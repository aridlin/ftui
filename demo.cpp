#define FTUI_IMPLEMENTATION
#include "ftui.hpp"

#ifdef _WIN32
#include <windows.h>
#include <dxgi.h>

static void hw_get_cpu(char* out, int out_sz) {
    wchar_t id[256] = {};
    DWORD sz = sizeof(id);
    GetEnvironmentVariableW(L"PROCESSOR_IDENTIFIER", id, sz / sizeof(wchar_t));
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    char id_utf8[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, id, -1, id_utf8, sizeof(id_utf8), nullptr, nullptr);
    snprintf(out, out_sz, "%s  |  %lu logical cores", id_utf8, si.dwNumberOfProcessors);
}

static void hw_get_gpu(char* out, int out_sz) {
    typedef HRESULT (WINAPI *PFN_CreateDXGIFactory)(REFIID, void**);
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    if (!dxgi) { snprintf(out, out_sz, "(dxgi unavailable)"); return; }
    auto fn = (PFN_CreateDXGIFactory)GetProcAddress(dxgi, "CreateDXGIFactory");
    if (!fn) { FreeLibrary(dxgi); snprintf(out, out_sz, "(CreateDXGIFactory missing)"); return; }
    IDXGIFactory* factory = nullptr;
    if (FAILED(fn(__uuidof(IDXGIFactory), (void**)&factory))) {
        FreeLibrary(dxgi); snprintf(out, out_sz, "(factory failed)"); return;
    }
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(factory->EnumAdapters(0, &adapter))) {
        DXGI_ADAPTER_DESC desc = {};
        adapter->GetDesc(&desc);
        char name[128] = {};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
        float vram_gb = desc.DedicatedVideoMemory / (1024.0f * 1024.0f * 1024.0f);
        snprintf(out, out_sz, "%s  |  %.1f GB VRAM", name, vram_gb);
        adapter->Release();
    } else {
        snprintf(out, out_sz, "(no adapter found)");
    }
    factory->Release();
    FreeLibrary(dxgi);
}

static void hw_get_ram(char* out, int out_sz) {
    MEMORYSTATUSEX ms = {};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    float total_gb = ms.ullTotalPhys / (1024.0f * 1024.0f * 1024.0f);
    float avail_gb = ms.ullAvailPhys / (1024.0f * 1024.0f * 1024.0f);
    snprintf(out, out_sz, "%.1f GB total  |  %.1f GB available", total_gb, avail_gb);
}
#endif // _WIN32

int main() {
    ftui::Config cfg;
    cfg.title  = "FTUI Demo";
    cfg.width  = 960;
    cfg.height = 900;

    if (!ftui::create_window(cfg)) return 1;

    ftui::ImageHandle* img = ftui::load_image("bpostmwhite.png");
    char img_path[512] = "bpostmwhite.png";

    char username[64]  = "";
    char password[64]  = "";
    char notes[512]    = "";
    int  click_count   = 0;
    bool opt_enabled   = false;
    float slider_val   = 0.5f;

    // tabs demo
    static const char* tab_labels[] = { "Login", "Notes", "Settings" };
    int tab_sel = 0;

    // Theme cycling
    static const char* theme_names[] = {
        "Default Dark", "Catppuccin Mocha", "Nord", "Gruvbox Dark", "One Dark"
    };
    static ftui::Style (*theme_fns[])() = {
        ftui::default_dark_style,
        ftui::catppuccin_mocha_style,
        ftui::nord_style,
        ftui::gruvbox_dark_style,
        ftui::one_dark_style,
    };
    int theme_idx = 0;
    for (int i = 0; i < 5; ++i) {
        if (theme_fns[i] == FTUI_DEFAULT_STYLE) {
            theme_idx = i;
            break;
        }
    }
    ftui::set_style(theme_fns[theme_idx]());

    static const ftui::FileFilter img_filters[] = {
        { "Images",    "*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff" },
        { "All Files", "*.*" },
    };

    while (ftui::pump()) {
        ftui::begin();

        ftui::text("FTUI demo");
        ftui::text("Tiny immediate-mode GUI — Windows + Linux");
        ftui::separator();

        ftui::tabs(tab_labels, 3, &tab_sel);
        ftui::separator();

        if (tab_sel == 0) {
            bool login = false;
            ftui::input("Username", username, sizeof(username), ftui::InputFlags::Default, &login);
            ftui::input("Password", password, sizeof(password), ftui::InputFlags::Password, &login);
            ftui::text("Tab / Shift+Tab cycle fields  |  Enter submits  |  Ctrl+Q quit");
            ftui::separator();
            if (ftui::button("Sign in") || login) click_count++;
            if (ftui::button("Reset counter"))    click_count = 0;
            ftui::separator();
            ftui::checkbox("Enable option", &opt_enabled);
            ftui::slider_float("Blend##slider", &slider_val, 0.0f, 1.0f);
        } else if (tab_sel == 1) {
            ftui::text_area("Notes##ta", notes, sizeof(notes), 6);
            ftui::text("Enter inserts newline  |  Tab cycles focus  |  Ctrl+C/V copy/paste");
        } else {
            // Image + file browser
            ftui::image(img, 200, 200);
            char file_btn[512];
            snprintf(file_btn, sizeof(file_btn), "Image: %s", img_path[0] ? img_path : "(none)");
            if (ftui::button(file_btn)) {
                std::string picked = ftui::open_file_dialog("Open Image", img_filters, 2);
                if (!picked.empty()) {
                    ftui::free_image(img);
                    img = ftui::load_image(picked.c_str());
                    const char* sep2 = picked.c_str() + picked.size();
                    while (sep2 > picked.c_str() && *(sep2-1) != '/' && *(sep2-1) != '\\') --sep2;
                    snprintf(img_path, sizeof(img_path), "%s", sep2);
                }
            }
            ftui::separator();

            char theme_btn[64];
            snprintf(theme_btn, sizeof(theme_btn), "Theme: %s", theme_names[theme_idx]);
            if (ftui::button(theme_btn)) {
                theme_idx = (theme_idx + 1) % 5;
                ftui::set_style(theme_fns[theme_idx]());
            }

#ifdef _WIN32
            ftui::separator();
            if (ftui::button("Hardware Check")) {
                static char s_cpu[256], s_gpu[256], s_ram[256];
                hw_get_cpu(s_cpu, sizeof(s_cpu));
                hw_get_gpu(s_gpu, sizeof(s_gpu));
                hw_get_ram(s_ram, sizeof(s_ram));
                static bool show_cpu = false, show_gpu = false, show_ram = false;
                show_cpu = show_gpu = show_ram = false;
                ftui::Config hw{};
                hw.title  = "Hardware Info";
                hw.width  = 700;
                hw.height = 300;
                hw.center_window = true;
                ftui::open_child_window(hw, [&]() {
                    ftui::text("Hardware Information");
                    ftui::separator();
                    if (ftui::button("CPU")) show_cpu = !show_cpu;
                    if (show_cpu) ftui::text(s_cpu);
                    if (ftui::button("GPU")) show_gpu = !show_gpu;
                    if (show_gpu) ftui::text(s_gpu);
                    if (ftui::button("RAM")) show_ram = !show_ram;
                    if (show_ram) ftui::text(s_ram);
                });
            }
#endif
        }

        ftui::separator();

        ftui::row(3, [&]() {
            if (ftui::button("Toggle rects")) ftui::debug().show_layout_rects ^= true;
            if (ftui::button("Toggle IDs"))   { ftui::debug().show_hovered_id ^= true; ftui::debug().show_active_id ^= true; }
            if (ftui::button("Toggle FPS"))   ftui::debug().show_fps ^= true;
        });

        ftui::separator();

        char tmp[256];
        snprintf(tmp, sizeof(tmp), "Clicks: %d   option: %s   blend: %.2f",
            click_count, opt_enabled ? "on" : "off", slider_val);
        ftui::text(tmp);
        snprintf(tmp, sizeof(tmp), "Username length: %d", (int)strlen(username));
        ftui::text(tmp);

        ftui::end();
    }

    ftui::free_image(img);
    ftui::shutdown();
    return 0;
}
