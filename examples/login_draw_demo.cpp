#define FTHT_IMPLEMENTATION
#include "../ftht.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

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

static void append_event(char* log, int cap, const char* message) {
    char line[160];
    std::time_t now = std::time(nullptr);
#if defined(_WIN32)
    std::tm tm_buf{};
    localtime_s(&tm_buf, &now);
    std::tm* tm = &tm_buf;
#else
    std::tm* tm = std::localtime(&now);
#endif
    std::snprintf(line, sizeof(line), "[%02d:%02d:%02d] %s",
                  tm ? tm->tm_hour : 0, tm ? tm->tm_min : 0, tm ? tm->tm_sec : 0,
                  message ? message : "");
    append_line(log, cap, line);
}

int main() {
    ftht::Config cfg;
    cfg.title = "FTHT Draw Login Demo";
    cfg.port = 8082;
    cfg.login_mode = ftht::LoginMode::DrawPenis;
    cfg.login_session_seconds = 10 * 60;

    if (!ftht::create_server(cfg)) return 1;

    bool sprinkler = false;
    bool lights = true;
    float silliness = 69.0f;
    char log[2048] =
        "[boot] drawing challenge enabled\n"
        "[auth] draw two nearby ellipses and one long ellipse or rectangle";

    while (ftht::pump()) {
        ftht::begin();

        ftht::text_wrapped(
            "This panel is protected by the drawing challenge. It unlocks when the sketch contains "
            "two nearby ellipses and one long ellipse or rectangle.");
        ftht::separator();

        ftht::row(3, [&]() {
            if (ftht::checkbox("Sprinkler", &sprinkler)) {
                append_event(log, sizeof(log), sprinkler ? "sprinkler enabled" : "sprinkler disabled");
            }
            if (ftht::checkbox("Lights", &lights)) {
                append_event(log, sizeof(log), lights ? "lights enabled" : "lights disabled");
            }
            if (ftht::slider_float("Silliness", &silliness, 0.0f, 100.0f)) {
                append_event(log, sizeof(log), "silliness adjusted");
            }
        });

        ftht::row(2, [&]() {
            if (ftht::button("Log victory", ftht::ColorRole::Accent)) {
                append_event(log, sizeof(log), "challenge accepted by the operator");
            }
            ftht::html("<button type=\"button\" onclick=\"location.href='/ftht/logout'\">Logout</button>");
        });

        ftht::log_view("Event log", log, 10,
                       ftht::LogViewFlags::WordWrap | ftht::LogViewFlags::AutoScrollBottom);

        ftht::end();
    }

    ftht::shutdown();
    return 0;
}
