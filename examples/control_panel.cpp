#define FTUI_IMPLEMENTATION
#include "../ftui.hpp"

#include <cstdio>
#include <cstring>

static void append_line(char* buffer, int cap, const char* line) {
    if (!buffer || cap <= 0 || !line) return;
    int len = (int)strlen(buffer);
    if (len > 0 && len < cap - 1) {
        buffer[len++] = '\n';
        buffer[len] = '\0';
    }
    if (len >= cap - 1) return;
    int room = cap - len - 1;
    int add = (int)strlen(line);
    if (add > room) add = room;
    if (add > 0) {
        memcpy(buffer + len, line, (size_t)add);
        buffer[len + add] = '\0';
    }
}

int main() {
    ftui::Config cfg;
    cfg.title = "FTUI Control Panel";
    cfg.width = 1040;
    cfg.height = 860;

    if (!ftui::create_window(cfg)) return 1;

    char username[64] = "operator";
    char api_token[64] = "";
    char endpoint[128] = "https://deploy.internal.example";
    char notes[2048] =
        "Deploy staging after the nightly build.\n"
        "Double-check the migration window before applying changes.";
    char activity[4096] =
        "[10:12] Connected to controller.\n"
        "[10:13] Pulled latest deployment plan.\n"
        "[10:14] Waiting for operator input.";

    bool remember_session = true;
    bool dry_run = true;
    bool alerts_enabled = true;
    bool connection_open = true;
    bool automation_open = true;
    bool confirm_open_last = false;

    static const char* envs[] = { "Development", "Staging", "Production" };
    static const char* roles[] = { "Observer", "Operator", "Maintainer" };
    int env_sel = 1;
    int role_sel = 1;

    while (ftui::pump()) {
        ftui::begin();

        ftui::text("Deployment control panel");
        ftui::text_wrapped("A settings-heavy utility app can stay direct: a few collapsible sections, a confirmation modal, and read-only activity output without introducing controllers or retained screens.");
        ftui::separator();

        ftui::row({1.15f, 1.0f}, [&]() {
            ftui::dropdown("Environment", envs, 3, &env_sel, 3);
            ftui::radio_group("Operator role", roles, 3, &role_sel, 1);
        });

        if (ftui::collapsing_header("Connection", &connection_open)) {
            ftui::row({1.5f, 1.0f}, [&]() {
                ftui::input("Username", username, sizeof(username));
                ftui::input("API token", api_token, sizeof(api_token), ftui::InputFlags::Password);
            });
            ftui::input("Endpoint", endpoint, sizeof(endpoint));
            ftui::checkbox("Remember session", &remember_session);
        }

        if (ftui::collapsing_header("Automation", &automation_open)) {
            ftui::checkbox("Dry run", &dry_run);
            ftui::checkbox("Alert on completion", &alerts_enabled);
            ftui::text_area_ex("Change summary", notes, sizeof(notes), 8, ftui::TextAreaFlags::WordWrap);
        }

        ftui::separator();
        ftui::row(3, [&]() {
            if (ftui::button("Validate")) {
                append_line(activity, sizeof(activity), "[Run] Validation completed successfully.");
            }
            if (ftui::button("Apply")) {
                ftui::open_modal("Confirm deployment");
            }
            if (ftui::button("Clear summary")) {
                notes[0] = '\0';
                append_line(activity, sizeof(activity), "[Run] Summary cleared.");
            }
        });

        ftui::separator();
        ftui::log_view("Activity", activity, 10,
                       ftui::LogViewFlags::WordWrap | ftui::LogViewFlags::AutoScrollBottom);

        bool confirm_open = ftui::modal("Confirm deployment", [&]() {
            ftui::text_wrapped("Applying this plan will use the current environment, credentials, and notes exactly as shown in the form.");
            ftui::separator();
            ftui::row({1.0f, 1.0f}, [&]() {
                if (ftui::button("Cancel")) ftui::close_modal();
                if (ftui::button("Deploy now")) {
                    char line[256];
                    snprintf(line, sizeof(line), "[Deploy] Sent %s run to %s as %s.",
                             dry_run ? "dry" : "live", envs[env_sel], username[0] ? username : "operator");
                    append_line(activity, sizeof(activity), line);
                    ftui::close_modal();
                }
            });
        });

        if (confirm_open && !confirm_open_last) {
            append_line(activity, sizeof(activity), "[Run] Deployment confirmation opened.");
        }
        confirm_open_last = confirm_open;

        ftui::end();
    }

    ftui::shutdown();
    return 0;
}
