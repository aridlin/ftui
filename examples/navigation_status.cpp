#define FTUI_IMPLEMENTATION
#include "../ftui.hpp"

int main() {
    ftui::Config cfg;
    cfg.title = "FTUI Navigation + Status";
    cfg.width = 1040;
    cfg.height = 720;
    cfg.backdrop_effect = ftui::BackdropEffect::BayerDither;
    cfg.dither_size = 5;

    if (!ftui::create_window(cfg)) return 1;
    ftui::set_style(ftui::ghostty_green_style());

    static const char* pages[] = {
        "Dashboard",
        "Network",
        "Services",
        "Logs",
        "Settings"
    };

    int page = 0;
    bool relay = true;
    float progress = 0.73f;

    while (ftui::pump()) {
        ftui::begin();

        ftui::side_layout(220.0f, [&]() {
            ftui::side_menu("Navigation", pages, 5, &page);

            ftui::content([&]() {
                if (page == 0) {
                    ftui::text("Dashboard");
                    ftui::text_wrapped("Side menus are useful when a utility app outgrows a tab strip.");
                    ftui::progress_bar(progress, "Loading assets");

                    ftui::ProgressStyle battery;
                    battery.label = "Battery mask";
                    battery.mask_path = "examples/battery.svg";
                    battery.height = 54.0f;
                    battery.wave_front = true;
                    battery.glint = true;
                    ftui::progress_bar(progress, battery);

                    ftui::row(3, [&]() {
                        if (ftui::button("Saved")) ftui::toast_success("Configuration saved");
                        if (ftui::button("Warn")) ftui::toast_warning("High CPU usage");
                        if (ftui::button("Error")) ftui::toast_error("Connection failed");
                    });
                } else if (page == 1) {
                    ftui::text("Network");
                    ftui::checkbox("Relay enabled", &relay);
                    if (ftui::button("Restart relay")) {
                        ftui::Toast t;
                        t.message = "Relay restarted";
                        t.type = ftui::ToastType::Success;
                        t.duration_ms = 5000;
                        t.dismissible = true;
                        ftui::toast(t);
                    }
                } else if (page == 2) {
                    ftui::text("Services");
                    ftui::progress_bar(0.42f, "Service rollout");
                } else if (page == 3) {
                    ftui::text("Logs");
                    if (ftui::button("Refresh logs")) ftui::toast_info("Logs refreshed");
                    ftui::text_wrapped("Toast calls belong naturally in event handlers, with no user-created manager or container.");
                } else {
                    ftui::text("Settings");
                    if (ftui::button("Clear toasts")) ftui::clear_toasts();
                    ftui::slider_float("Demo progress", &progress, 0.0f, 1.0f);
                }
            });
        });

        ftui::end();
    }

    ftui::shutdown();
    return 0;
}
