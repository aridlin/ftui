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

static void append_event(char* buffer, int cap, const char* source, const char* level, const char* message) {
    char line[256];
    snprintf(line, sizeof(line), "[%s] %-7s %s", source, level, message);
    append_line(buffer, cap, line);
}

int main() {
    ftui::Config cfg;
    cfg.title = "FTUI Log Viewer";
    cfg.width = 1180;
    cfg.height = 860;

    if (!ftui::create_window(cfg)) return 1;

    char notes[2048] =
        "Operator scratchpad:\n"
        "- Watch reconnect attempts.\n"
        "- Capture device IDs before handoff.\n"
        "- Note any repeated latency spikes.";
    char output[8192] =
        "[engine] INFO    Renderer ready.\n"
        "[voice] INFO    Voice list refreshed.\n"
        "[network] WARN    Retry scheduled in 5 seconds.\n"
        "[system] INFO    Session opened.";

    bool auto_follow = true;
    bool wrap_output = true;
    int source_sel = 0;
    int level_sel = 0;

    static const char* sources[] = { "engine", "voice", "network", "system" };
    static const char* levels[] = { "INFO", "WARN", "ERROR" };
    static const char* incidents[] = {
        "Voice engine warmed up after a cold start.",
        "Two reconnect attempts happened during a DNS failover.",
        "Transcript upload queue drained successfully.",
        "Audio device changed and was rebound in place.",
        "Cached profile metadata was refreshed from disk.",
        "Low-priority telemetry stayed below the throttle cap.",
        "Operator note export completed without blocking the UI.",
        "Theme update propagated to the native titlebar."
    };

    while (ftui::pump()) {
        ftui::begin();

        ftui::text("Operations log viewer");
        ftui::text_wrapped("This example focuses on the read-only output path: a scrollable log view for live text, plus a writable notes pane for operator context.");
        ftui::separator();

        ftui::row({1.0f, 1.0f, 0.8f, 0.8f}, [&]() {
            ftui::dropdown("Source", sources, 4, &source_sel, 4);
            ftui::dropdown("Severity", levels, 3, &level_sel, 3);
            ftui::checkbox("Auto follow", &auto_follow);
            ftui::checkbox("Wrap lines", &wrap_output);
        });

        ftui::LogViewFlags log_flags = ftui::LogViewFlags::Default;
        if (wrap_output) log_flags = log_flags | ftui::LogViewFlags::WordWrap;
        if (auto_follow) log_flags = log_flags | ftui::LogViewFlags::AutoScrollBottom;

        ftui::row({2.0f, 1.2f}, [&]() {
            ftui::log_view("Combined output", output, 16, log_flags);
            ftui::text_area_ex("Operator notes", notes, sizeof(notes), 16, ftui::TextAreaFlags::WordWrap);
        });

        ftui::separator();
        ftui::row(3, [&]() {
            if (ftui::button("Append sample")) {
                append_event(output, sizeof(output), sources[source_sel], levels[level_sel], "Synthetic event appended from the example toolbar.");
            }
            if (ftui::button("Append latency warning")) {
                append_event(output, sizeof(output), "network", "WARN", "Latency threshold exceeded on the west relay.");
            }
            if (ftui::button("Clear output")) {
                output[0] = '\0';
                append_event(output, sizeof(output), "system", "INFO", "Output buffer cleared.");
            }
        });

        ftui::separator();
        ftui::scroll_area("Incident queue", 200.0f, [&]() {
            for (int i = 0; i < (int)(sizeof(incidents) / sizeof(incidents[0])); ++i) {
                char line[256];
                snprintf(line, sizeof(line), "%02d  %s", i + 1, incidents[i]);
                ftui::text(line);
            }
        });

        ftui::end();
    }

    ftui::shutdown();
    return 0;
}
