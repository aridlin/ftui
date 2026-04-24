#define FTUI_IMPLEMENTATION
#include "ftui.hpp"

int main() {
    ftui::Config cfg;
    cfg.title  = L"FTUI Demo";
    cfg.width  = 960;
    cfg.height = 860;

    if (!ftui::create_window(cfg)) return 1;

    // Load bundled image at startup (relative to exe working directory)
    ftui::ImageHandle* img = ftui::load_image("bpostmwhite.png");
    char img_path[512] = "bpostmwhite.png";

    char username[64]  = "";
    char password[64]  = "";
    char notes[256]    = "";
    int  click_count   = 0;
    bool opt_enabled   = false;
    float slider_val   = 0.5f;

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
    ftui::set_style(theme_fns[theme_idx]());

    // File dialog filters (shared across frames)
    static const ftui::FileFilter img_filters[] = {
        { L"Images",  L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff" },
        { L"All Files", L"*.*" },
    };

    while (ftui::pump()) {
        ftui::begin();

        ftui::text("FTUI demo");
        ftui::text("Tiny immediate-mode GUI for Windows");
        ftui::separator();

        bool login = false;
        ftui::input("Username", username, sizeof(username), ftui::InputFlags::None, &login);
        ftui::input("Password", password, sizeof(password), ftui::InputFlags::Password, &login);
        ftui::input("Notes",    notes,    sizeof(notes));

        ftui::text("Tab / Shift+Tab cycle fields  |  Enter submits  |  Ctrl+Q quit");
        ftui::separator();

        if (ftui::button("Sign in") || login) click_count++;
        if (ftui::button("Reset counter"))    click_count = 0;

        ftui::separator();

        ftui::checkbox("Enable option", &opt_enabled);
        ftui::slider_float("Blend##slider", &slider_val, 0.0f, 1.0f);

        ftui::separator();

        // Image + file browser
        ftui::image(img, 200, 200);

        char file_btn[512];
        snprintf(file_btn, sizeof(file_btn), "Image: %s", img_path[0] ? img_path : "(none)");
        if (ftui::button(file_btn)) {
            std::string picked = ftui::open_file_dialog(
                L"Open Image", img_filters, 2);
            if (!picked.empty()) {
                ftui::free_image(img);
                img = ftui::load_image(picked.c_str());
                // Store just the filename for display
                const char* sep = picked.c_str() + picked.size();
                while (sep > picked.c_str() && *(sep-1) != '/' && *(sep-1) != '\\') --sep;
                snprintf(img_path, sizeof(img_path), "%s", sep);
            }
        }

        ftui::separator();

        // Theme cycling
        char theme_btn[64];
        snprintf(theme_btn, sizeof(theme_btn), "Theme: %s", theme_names[theme_idx]);
        if (ftui::button(theme_btn)) {
            theme_idx = (theme_idx + 1) % 5;
            ftui::set_style(theme_fns[theme_idx]());
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
