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
    cfg.title = "FTHT Form Login Demo";
    cfg.port = 8081;
    cfg.login_mode = ftht::LoginMode::Form;
    cfg.login_username = "admin";
    cfg.login_password = "secret";
    cfg.login_db_path = "ftht_form_login_demo.litesql";
    cfg.login_session_seconds = 10 * 60;
    cfg.login_allow_registration = true;

    if (!ftht::create_server(cfg)) return 1;

    bool relay = false;
    float threshold = 42.0f;
    char operator_note[256] = "Try admin / secret, or request registration and approve it in this terminal.";
    char log[2048] =
        "[boot] form login enabled\n"
        "[auth] registration requests require terminal approval\n"
        "[auth] idle sessions expire after 10 minutes";

    while (ftht::pump()) {
        ftht::begin();

        ftht::text_wrapped(
            "This panel is protected by the built-in form login. The first run writes a LiteSQL "
            "text file containing username, salt, and salted password hash.");
        ftht::separator();

        ftht::row({1.0f, 1.0f}, [&]() {
            if (ftht::checkbox("Relay armed", &relay)) {
                append_event(log, sizeof(log), relay ? "relay armed" : "relay disarmed");
            }
            if (ftht::slider_float("Alert threshold", &threshold, 0.0f, 100.0f)) {
                append_event(log, sizeof(log), "threshold changed");
            }
        });

        ftht::text_area_ex("Operator note", operator_note, sizeof(operator_note), 4,
                           ftht::TextAreaFlags::WordWrap);

        ftht::row(2, [&]() {
            if (ftht::button("Record check", ftht::ColorRole::Success)) {
                append_event(log, sizeof(log), "manual check recorded");
            }
            ftht::html("<button type=\"button\" onclick=\"location.href='/ftht/logout'\">Logout</button>");
        });

        ftht::log_view("Audit log", log, 10,
                       ftht::LogViewFlags::WordWrap | ftht::LogViewFlags::AutoScrollBottom);

        ftht::end();
    }

    ftht::shutdown();
    return 0;
}
