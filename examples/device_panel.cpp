#define FTHT_IMPLEMENTATION
#include "../ftht.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

static void copy_text(char* buffer, int cap, const char* text) {
    if (!buffer || cap <= 0) return;
    std::snprintf(buffer, (size_t)cap, "%s", text ? text : "");
}

static void append_line(char* buffer, int cap, const char* line) {
    if (!buffer || cap <= 0 || !line) return;
    int len = (int)std::strlen(buffer);
    if (len > 0 && len < cap - 1) {
        buffer[len++] = '\n';
        buffer[len] = '\0';
    }
    if (len >= cap - 1) return;
    int room = cap - len - 1;
    int add = (int)std::strlen(line);
    if (add > room) add = room;
    if (add > 0) {
        std::memcpy(buffer + len, line, (size_t)add);
        buffer[len + add] = '\0';
    }
}

static void append_event(char* log, int cap, const char* tag, const char* message) {
    char line[256];
    std::time_t now = std::time(nullptr);
#if defined(_WIN32)
    std::tm tm_buf{};
    localtime_s(&tm_buf, &now);
    std::tm* tm = &tm_buf;
#else
    std::tm* tm = std::localtime(&now);
#endif
    std::snprintf(line, sizeof(line), "[%02d:%02d:%02d] %-8s %s",
                  tm ? tm->tm_hour : 0, tm ? tm->tm_min : 0, tm ? tm->tm_sec : 0,
                  tag ? tag : "event", message ? message : "");
    append_line(log, cap, line);
}

static void metric_card(const char* title, const char* value, const char* unit, const char* note) {
    char html[768];
    std::snprintf(html, sizeof(html),
        "<section class=\"metric\"><div>%s</div><b>%s<span>%s</span></b><small>%s</small></section>",
        title, value, unit, note);
    ftht::html(html);
}

int main() {
    ftht::Config cfg;
    cfg.title = "Grow Rack Controller";
    cfg.port = 8080;
    cfg.client_poll_ms = 1500;
#ifdef FTHT_DEMO_CLIENT_DEBUG
    cfg.client_debug_output = true;
#endif
    cfg.extra_head_html =
        "<style>"
        ".metrics{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px}"
        ".metric{border:2px solid var(--border);padding:10px;background:var(--paper2);box-shadow:3px 3px 0 var(--border)}"
        ".metric div{font-size:12px;font-weight:900;text-transform:uppercase;color:var(--muted)}"
        ".metric b{display:block;font-size:28px;line-height:1.05;margin-top:6px}"
        ".metric b span{font-size:13px;margin-left:4px}"
        ".metric small{display:block;color:var(--muted);margin-top:7px}"
        ".ok{color:#2f855a}.warntext{color:#a14d21}.bad{color:#8f1d1d}"
        "@media(max-width:820px){.metrics{grid-template-columns:repeat(2,minmax(0,1fr))}}"
        "</style>";

    if (!ftht::create_server(cfg)) return 1;

    char device_name[48] = "rack-a";
    char location[64] = "utility room";
    char notes[768] =
        "Target: keep seedlings warm, humid, and watered.\n"
        "This demo simulates sensor drift on each browser refresh.";
    char log[4096] =
        "[boot]    controller started\n"
        "[boot]    telemetry poll enabled";

    bool lights = true;
    bool pump = false;
    bool fan = true;
    bool alarm_armed = true;
    float target_temp = 24.0f;
    float min_moisture = 38.0f;
    float light_hours = 14.0f;
    int mode = 1;
    int tab = 0;
    int tick = 0;

    static const char* tabs[] = { "Status", "Controls", "Events" };
    static const char* modes[] = { "Manual", "Auto", "Night" };

    while (ftht::pump()) {
        ++tick;

        float wave = std::sin(tick * 0.37f);
        float temp = 22.6f + wave * 1.7f + (lights ? 0.8f : -0.4f) + (fan ? -0.3f : 0.2f);
        float humidity = 58.0f + std::cos(tick * 0.21f) * 7.0f + (pump ? 4.0f : 0.0f);
        float moisture = 42.0f + std::sin(tick * 0.17f + 1.0f) * 9.0f + (pump ? 8.0f : -2.0f);
        float power = 4.5f + (lights ? 18.0f : 0.0f) + (pump ? 8.5f : 0.0f) + (fan ? 2.8f : 0.0f);

        if (mode == 1) {
            if (moisture < min_moisture && !pump) {
                pump = true;
                append_event(log, sizeof(log), "auto", "soil below threshold; pump enabled");
            } else if (moisture > min_moisture + 8.0f && pump) {
                pump = false;
                append_event(log, sizeof(log), "auto", "soil recovered; pump disabled");
            }
            fan = temp > target_temp + 0.6f;
        } else if (mode == 2) {
            lights = false;
            fan = false;
        }

        ftht::begin();

        ftht::text_wrapped(
            "A practical FTHT panel: live telemetry, persistent controls, operator notes, "
            "and event history over the same immediate-mode C++ loop.");
        ftht::separator();
        ftht::tabs(tabs, 3, &tab);
        ftht::separator();

        if (tab == 0) {
            char temp_buf[32], hum_buf[32], moist_buf[32], power_buf[32], summary[256];
            std::snprintf(temp_buf, sizeof(temp_buf), "%.1f", temp);
            std::snprintf(hum_buf, sizeof(hum_buf), "%.0f", humidity);
            std::snprintf(moist_buf, sizeof(moist_buf), "%.0f", moisture);
            std::snprintf(power_buf, sizeof(power_buf), "%.1f", power);

            ftht::html("<div class=\"metrics\">");
            metric_card("Air temp", temp_buf, "C", temp > target_temp + 2.0f ? "above target" : "stable");
            metric_card("Humidity", hum_buf, "%", humidity < 48.0f ? "dry air" : "in range");
            metric_card("Soil", moist_buf, "%", moisture < min_moisture ? "needs water" : "ok");
            metric_card("Power", power_buf, "W", "estimated load");
            ftht::html("</div>");

            ftht::separator();
            std::snprintf(summary, sizeof(summary),
                          "Mode: %s | lights: %s | pump: %s | fan: %s | alarm: %s | sample: %d",
                          modes[mode], lights ? "on" : "off", pump ? "on" : "off",
                          fan ? "on" : "off", alarm_armed ? "armed" : "off", tick);
            ftht::text(summary);

            if (ftht::button("Refresh now", ftht::ColorRole::Accent)) {
                append_event(log, sizeof(log), "manual", "operator requested a telemetry refresh");
            }
        } else if (tab == 1) {
            ftht::row({1.0f, 1.0f}, [&]() {
                ftht::input("Device name", device_name, sizeof(device_name));
                ftht::input("Location", location, sizeof(location));
            });

            ftht::row({1.0f, 1.0f, 1.0f}, [&]() {
                ftht::dropdown("Mode", modes, 3, &mode, 3);
                ftht::checkbox("Lights relay", &lights);
                ftht::checkbox("Pump relay", &pump);
            });

            ftht::row({1.0f, 1.0f, 1.0f}, [&]() {
                ftht::checkbox("Fan relay", &fan);
                ftht::checkbox("Alarm armed", &alarm_armed);
                ftht::slider_float("Light hours", &light_hours, 0.0f, 24.0f);
            });

            ftht::row({1.0f, 1.0f}, [&]() {
                ftht::slider_float("Target temp C", &target_temp, 16.0f, 32.0f);
                ftht::slider_float("Min soil %", &min_moisture, 10.0f, 80.0f);
            });

            ftht::text_area_ex("Operator notes", notes, sizeof(notes), 6, ftht::TextAreaFlags::WordWrap);

            ftht::row(3, [&]() {
                if (ftht::button("Apply settings", ftht::ColorRole::Success)) {
                    append_event(log, sizeof(log), "config", "settings applied");
                }
                if (ftht::button("Prime pump", ftht::ColorRole::Accent)) {
                    pump = true;
                    append_event(log, sizeof(log), "manual", "pump primed");
                }
                if (ftht::button("Emergency stop", ftht::ColorRole::Warning)) {
                    lights = false;
                    pump = false;
                    fan = false;
                    alarm_armed = false;
                    append_event(log, sizeof(log), "safety", "all outputs disabled");
                }
            });
        } else {
            ftht::log_view("Event log", log, 16,
                           ftht::LogViewFlags::WordWrap | ftht::LogViewFlags::AutoScrollBottom);
            ftht::row(2, [&]() {
                if (ftht::button("Add inspection note")) {
                    append_event(log, sizeof(log), "note", "visual inspection completed");
                }
                if (ftht::button("Clear log", ftht::ColorRole::Warning)) {
                    copy_text(log, sizeof(log), "[log]     cleared by operator");
                }
            });
        }

        ftht::end();
    }

    ftht::shutdown();
    return 0;
}
