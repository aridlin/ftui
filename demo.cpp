#define FTUI_IMPLEMENTATION
#include "ftui.hpp"

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

static void copy_text(char* buffer, int cap, const char* text) {
    if (!buffer || cap <= 0) return;
    if (!text) {
        buffer[0] = '\0';
        return;
    }
    snprintf(buffer, (size_t)cap, "%s", text);
}

int main() {
    ftui::Config cfg;
    cfg.title = "FTUI Demo";
    cfg.width = 1080;
    cfg.height = 920;

    if (!ftui::create_window(cfg)) return 1;

    char username[64] = "operator";
    char password[64] = "";
    char port[16] = "8080";
    char access_code[32] = "DEMO01";
    char editor[2048] =
        "This is an editable multiline text box.\n"
        "Turn on word wrap to keep long notes readable without extra work.\n"
        "It uses the same immediate-mode flow as the smaller widgets.";
    char logs[4096] =
        "[09:14] Boot sequence complete.\n"
        "[09:14] Waiting for input.\n"
        "[09:15] Renderer warmed up.\n"
        "[09:16] Ready.";

    int click_count = 0;
    bool safe_mode = false;
    bool advanced_enabled = true;
    float blend = 0.45f;

    static const char* tabs[] = { "Workspace", "Logs", "Settings" };
    int tab_sel = 0;

    static const char* env_items[] = { "Local", "Staging", "Production" };
    static const char* profile_items[] = { "Admin", "Observer", "Maintainer", "Support" };
    static const char* shell_items[] = { "Powershell", "Bash", "Cmd" };
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

    int env_sel = 1;
    int profile_sel = 0;
    int shell_sel = 0;
    int theme_idx = 0;
    for (int i = 0; i < 5; ++i) {
        if (theme_fns[i] == FTUI_DEFAULT_STYLE) {
            theme_idx = i;
            break;
        }
    }
    ftui::set_style(theme_fns[theme_idx]());

    bool general_open = true;
    bool layout_open = true;
    bool modal_was_open = false;

    static const char* history_lines[] = {
        "Background sync completed with 12 updates.",
        "Parser cache rebuilt after settings change.",
        "New device attached on COM7.",
        "Live session exported to disk.",
        "Diagnostics snapshot created for support.",
        "Low-priority polling interval reduced.",
        "Message queue drained without errors.",
        "Operator role promoted to maintainer.",
        "Theme changed at runtime without restart.",
        "Scroll container preserved wheel ownership.",
        "Tooltip text only appears on hover or focus.",
        "Modal blocked background interaction correctly."
    };

    while (ftui::pump()) {
        ftui::begin();

        ftui::text("FTUI demo");
        ftui::text_wrapped(
            "Tiny immediate-mode GUI for utility apps. This demo focuses on the higher-value widgets "
            "and layout helpers while keeping the basic flow the same: begin, draw widgets, end.");
        ftui::separator();

        ftui::tabs(tabs, 3, &tab_sel);
        ftui::separator();

        if (tab_sel == 0) {
            ftui::text_wrapped("Core form widgets stay compact, but you can now layer in pickers, layout hints, tooltips, and disabled scopes without changing the basic pattern.");
            ftui::separator();

            ftui::row({2.0f, 1.0f}, [&]() {
                ftui::input("Username", username, sizeof(username));
                ftui::tooltip("Single-line input with the original simple call shape.");

                ftui::input("Port", port, sizeof(port), ftui::InputFlags::CharsDecimal);
                ftui::tooltip("This field uses decimal-only filtering.");

                ftui::input("Password", password, sizeof(password), ftui::InputFlags::Password);
                ftui::tooltip("Password masking still uses the same input widget.");

                ftui::input("Access code", access_code, sizeof(access_code),
                            ftui::InputFlags::CharsUppercase | ftui::InputFlags::CharsNoBlank);
                ftui::tooltip("Uppercase plus no-blank filtering is additive.");

                if (ftui::button("Sign in")) {
                    click_count++;
                    append_line(logs, sizeof(logs), "[Workspace] Sign in requested.");
                }
                ftui::tooltip("Buttons support hover, press, disabled, and keyboard activation.");

                if (ftui::button("Focus username")) {
                    ftui::request_focus("Username");
                }

                ftui::listbox("Profile", profile_items, 4, &profile_sel, 4);
                ftui::tooltip("Listboxes are explicit always-visible pickers.");

                ftui::radio_group("Shell", shell_items, 3, &shell_sel, 1);
                ftui::tooltip("Radio groups use arrow-key navigation when focused.");
            });

            ftui::separator();
            ftui::checkbox("Enable safe mode", &safe_mode);
            ftui::tooltip("Checkboxes now use a filled square instead of a checkmark.");
            ftui::slider_float("Blend", &blend, 0.0f, 1.0f);
            ftui::tooltip("Sliders also participate in the shared focus and disabled state path.");

            ftui::begin_disabled();
            if (!advanced_enabled) {
                ftui::button("Disabled action");
                ftui::tooltip("Scoped disabled state dims visuals and suppresses interaction.");
            }
            ftui::end_disabled();

            ftui::row(2, [&]() {
                if (ftui::button("Toggle advanced")) advanced_enabled = !advanced_enabled;
                if (ftui::button("Open child window")) {
                    ftui::Config child;
                    child.title = "FTUI Child";
                    child.width = 520;
                    child.height = 260;
                    child.center_window = true;
                    ftui::open_child_window(child, [&]() {
                        ftui::text("Child windows inherit the same API.");
                        ftui::text_wrapped("This is still immediate mode. There is no extra controller layer for nested windows.");
                        ftui::separator();
                        static bool child_flag = true;
                        static float child_mix = 0.35f;
                        ftui::checkbox("Keep child updates live", &child_flag);
                        ftui::slider_float("Mix", &child_mix, 0.0f, 1.0f);
                    });
                }
            });
        } else if (tab_sel == 1) {
            ftui::row({3.0f, 2.0f}, [&]() {
                ftui::text_area_ex("Editor##main", editor, sizeof(editor), 10, ftui::TextAreaFlags::WordWrap);
                ftui::tooltip("Editable multiline input with optional word wrap.");

                ftui::log_view("Output##log", logs, 10, ftui::LogViewFlags::WordWrap | ftui::LogViewFlags::AutoScrollBottom);
                ftui::tooltip("Read-only log view supports selection, copy, wrapping, and scrolling.");
            });

            ftui::separator();
            if (ftui::button("Append log line")) {
                append_line(logs, sizeof(logs), "[Logs] Manual line appended.");
            }
            ftui::tooltip("Useful for transcripts, debug output, and runtime status.");

            ftui::separator();
            ftui::scroll_area("History", 220.0f, [&]() {
                for (int i = 0; i < (int)(sizeof(history_lines) / sizeof(history_lines[0])); ++i) {
                    char line[256];
                    snprintf(line, sizeof(line), "%02d  %s", i + 1, history_lines[i]);
                    ftui::text(line);
                }
            });
            ftui::tooltip("Nested scroll areas consume wheel input before the outer window.");
        } else {
            ftui::text_wrapped("Collapsible sections, one-shot layout hints, modals, and theme switching are meant to stay opt-in. If you do not call them, the basic API does not get more complicated.");
            ftui::separator();

            if (ftui::collapsing_header("General##settings", &general_open)) {
                ftui::checkbox("Enable advanced controls", &advanced_enabled);
                ftui::text("Current environment");
                ftui::text(env_items[env_sel]);
                ftui::set_next_width(220.0f);
                ftui::button("Fixed width example");
                ftui::set_next_fill();
                ftui::button("Fill width example");
            }

            if (ftui::collapsing_header("Layout helpers##settings", &layout_open)) {
                ftui::text("Percent width");
                ftui::set_next_percent(0.55f);
                ftui::button("55% width");
                ftui::text("Clamped width");
                ftui::set_next_percent(0.75f);
                ftui::set_next_limits(180.0f, 280.0f);
                ftui::set_next_align(ftui::Align::End);
                ftui::button("Aligned and clamped");
                ftui::row({2.0f, 1.0f}, [&]() {
                    ftui::button("Wide cell");
                    ftui::button("Narrow cell");
                });
            }

            ftui::separator();
            if (ftui::button("Reset session")) {
                ftui::open_modal("Reset confirmation");
            }
        }

        ftui::separator();
        ftui::row(3, [&]() {
            if (ftui::button("Toggle rects")) ftui::debug().show_layout_rects ^= true;
            if (ftui::button("Toggle IDs")) {
                ftui::debug().show_hovered_id ^= true;
                ftui::debug().show_active_id ^= true;
            }
            if (ftui::button("Toggle FPS")) ftui::debug().show_fps ^= true;
        });

        ftui::separator();
        char summary[256];
        snprintf(summary, sizeof(summary),
                 "Clicks: %d   safe mode: %s   blend: %.2f   shell: %s",
                 click_count, safe_mode ? "on" : "off", blend, shell_items[shell_sel]);
        ftui::text(summary);

        if (ftui::dropdown("Theme", theme_names, 5, &theme_idx, 5)) {
            ftui::set_style(theme_fns[theme_idx]());
            append_line(logs, sizeof(logs), "[Settings] Theme changed.");
        }
        ftui::tooltip("Dropdowns are opt-in single-call pickers.");

        bool modal_open = ftui::modal("Reset confirmation", [&]() {
            ftui::text_wrapped("This lightweight modal blocks background widgets until you close it. It is still immediate mode: just call ftui::modal() and draw inside it.");
            ftui::separator();
            ftui::row({1.0f, 1.0f}, [&]() {
                if (ftui::button("Cancel")) ftui::close_modal();
                if (ftui::button("Reset now")) {
                    username[0] = '\0';
                    password[0] = '\0';
                    copy_text(port, sizeof(port), "8080");
                    copy_text(access_code, sizeof(access_code), "DEMO01");
                    copy_text(editor, sizeof(editor), "Session reset.\nYou can keep typing here.");
                    copy_text(logs, sizeof(logs), "[Reset] Session cleared.");
                    click_count = 0;
                    safe_mode = false;
                    blend = 0.45f;
                    ftui::close_modal();
                }
            });
        });
        if (modal_open && !modal_was_open) {
            append_line(logs, sizeof(logs), "[Settings] Reset modal opened.");
        }
        modal_was_open = modal_open;

        ftui::end();
    }

    ftui::shutdown();
    return 0;
}
