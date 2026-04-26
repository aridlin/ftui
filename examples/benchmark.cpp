#define FTUI_IMPLEMENTATION
#include "../ftui.hpp"

#include <cstdio>

namespace {
constexpr int kMaxRows = 4000;
bool g_seeded = false;
bool g_enabled[kMaxRows];
float g_mix[kMaxRows];
char g_names[kMaxRows][24];

void seed_state() {
    if (g_seeded) return;
    for (int i = 0; i < kMaxRows; ++i) {
        g_enabled[i] = (i % 3) == 0;
        g_mix[i] = (float)(i % 100) / 100.0f;
        snprintf(g_names[i], sizeof(g_names[i]), "Item %04d", i + 1);
    }
    g_seeded = true;
}

void reset_state() {
    g_seeded = false;
    seed_state();
}
} // namespace

int main() {
    seed_state();

    ftui::Config cfg;
    cfg.title = "FTUI Benchmark";
    cfg.width = 1280;
    cfg.height = 920;

    if (!ftui::create_window(cfg)) return 1;

    ftui::debug().show_fps = true;

    static const char* row_counts[] = { "250", "500", "1000", "2000", "4000" };
    static const int row_values[] = { 250, 500, 1000, 2000, 4000 };
    static const char* workloads[] = { "Labels only", "Buttons + toggles", "Inputs + sliders" };

    int row_sel = 2;
    int workload_sel = 1;
    bool dense_mode = true;
    bool wrap_notes = true;
    char notes[1024] =
        "Use this app to stress your own FTUI build.\n"
        "Start with smaller row counts, then push the workload upward.";
    char filter[64] = "Item";

    while (ftui::pump()) {
        ftui::begin();

        ftui::text("Benchmark");
        ftui::text_wrapped("A synthetic workload for frame pacing checks. The goal is to make performance changes visible without changing the core app loop or pulling in a separate profiler harness.");
        ftui::separator();

        ftui::row({1.0f, 1.0f, 0.8f}, [&]() {
            ftui::dropdown("Rows", row_counts, 5, &row_sel, 5);
            ftui::dropdown("Workload", workloads, 3, &workload_sel, 3);
            ftui::checkbox("Dense spacing", &dense_mode);
        });

        ftui::row({1.1f, 1.4f}, [&]() {
            ftui::input("Filter prefix", filter, sizeof(filter));
            ftui::text_area_ex("Run notes", notes, sizeof(notes), 4,
                               wrap_notes ? ftui::TextAreaFlags::WordWrap : ftui::TextAreaFlags::Default);
        });

        ftui::checkbox("Wrap notes", &wrap_notes);
        ftui::separator();

        ftui::row(3, [&]() {
            if (ftui::button("Reset state")) reset_state();
            if (ftui::button("Toggle FPS overlay")) ftui::debug().show_fps ^= true;
            ftui::button("Warm cache");
        });

        char summary[256];
        snprintf(summary, sizeof(summary),
                 "Rows: %d   workload: %s   effects: %s",
                 row_values[row_sel], workloads[workload_sel], cfg.enable_effects ? "on" : "off");
        ftui::text(summary);
        ftui::separator();

        const int row_count = row_values[row_sel];
        ftui::scroll_area("Benchmark canvas", 560.0f, [&]() {
            for (int i = 0; i < row_count; ++i) {
                if (workload_sel == 0) {
                    char line[128];
                    snprintf(line, sizeof(line), "%s %04d  |  enabled=%s  mix=%.2f",
                             filter, i + 1, g_enabled[i] ? "yes" : "no", g_mix[i]);
                    ftui::text(line);
                } else if (workload_sel == 1) {
                    ftui::row({2.0f, 0.9f, 1.1f}, [&]() {
                        char label[64];
                        char button_label[64];
                        char check_label[64];
                        snprintf(label, sizeof(label), "%s row %04d", filter, i + 1);
                        snprintf(button_label, sizeof(button_label), "Ping##bench_btn_%04d", i);
                        snprintf(check_label, sizeof(check_label), "Enabled##bench_chk_%04d", i);
                        ftui::text(label);
                        ftui::checkbox(check_label, &g_enabled[i]);
                        ftui::button(button_label);
                    });
                } else {
                    ftui::row({1.4f, 1.0f, 1.0f}, [&]() {
                        char input_label[64];
                        char slider_label[64];
                        char check_label[64];
                        snprintf(input_label, sizeof(input_label), "Label##bench_input_%04d", i);
                        snprintf(slider_label, sizeof(slider_label), "Mix##bench_mix_%04d", i);
                        snprintf(check_label, sizeof(check_label), "Live##bench_live_%04d", i);
                        ftui::input(input_label, g_names[i], sizeof(g_names[i]));
                        ftui::slider_float(slider_label, &g_mix[i], 0.0f, 1.0f);
                        ftui::checkbox(check_label, &g_enabled[i]);
                    });
                }

                if (!dense_mode && i + 1 < row_count) {
                    ftui::separator();
                }
            }
        });

        ftui::end();
    }

    ftui::shutdown();
    return 0;
}
