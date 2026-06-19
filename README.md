# FTUI

FTUI is a tiny immediate-mode GUI for C++.

It is a single header, keeps the baseline loop small, and aims for utility-app workflows where you want native windows and straightforward widget code without bringing in a retained UI framework.

- Windows backend: Win32 + Direct2D + DirectWrite
- Linux backend: X11 + Cairo
- Distribution model: single header, no CMake required

## Philosophy

- Keep the default path tiny: `create_window() -> pump() -> begin() -> widgets -> end() -> shutdown()`
- Add power through new functions and additive flags, not new required setup
- Avoid builders, registries, or retained widget trees
- If you do not use a feature, it should not make the rest of the API feel heavier

## Quick start

In exactly one `.cpp` file:

```cpp
#define FTUI_IMPLEMENTATION
#include "ftui.hpp"

int main() {
    ftui::Config cfg;
    cfg.title = "My App";
    cfg.width = 960;
    cfg.height = 640;
    cfg.fps_limit = 60; // default; use 0 for uncapped

    if (!ftui::create_window(cfg)) return 1;

    char name[64] = "";

    while (ftui::pump()) {
        ftui::begin();

        ftui::text("Hello");
        ftui::input("Name", name, sizeof(name));

        if (ftui::button("Go")) {
            // handle click
        }

        ftui::end();
    }

    ftui::shutdown();
    return 0;
}
```

Every other translation unit should include `ftui.hpp` without `FTUI_IMPLEMENTATION`.

## Frame pacing

FTUI caps rendering at `Config::fps_limit`, which defaults to `60`.

```cpp
ftui::Config cfg;
cfg.fps_limit = 144; // or 0 for uncapped rendering
```

You can also change it at runtime:

```cpp
ftui::set_fps_limit(30);
```

FTUI sleeps while idle, then redraws on native input/window events, active interaction, active debug/effect overlays, or an explicit request:

```cpp
ftui::request_redraw();
```

The Linux backend renders into an X11 pixmap back buffer and swaps it to the window.

Effects backdrops can use the default blur-style panel on Windows, or a theme-colored Bayer dither pattern on both Windows and Linux:

```cpp
ftui::Config cfg;
cfg.backdrop_effect = ftui::BackdropEffect::BayerDither;
cfg.dither_size = 5;
```

Window-level transparency is separate:

```cpp
cfg.window_transparency = ftui::WindowTransparency::Plain;
cfg.window_opacity = 0.88f;
```

Windows supports `Opaque`, `Plain`, `BayerDither`, and `Blur`. Linux supports `Opaque`, `Plain`, and `BayerDither`; plain opacity uses the compositor `_NET_WM_WINDOW_OPACITY` property.

## Build

Windows with `clang++`:

```bash
clang++ main.cpp -o app.exe -std=c++17 -ld2d1 -ldwrite -lgdi32 -lole32 -luuid -luser32 -lwindowscodecs
```

Windows with MSVC:

- `ftui.hpp` already includes the required `#pragma comment(lib, ...)` lines.

Linux:

```bash
g++ main.cpp -o app -DFTUI_IMPLEMENTATION $(pkg-config --cflags --libs cairo x11) -std=c++17
```

Linux note:
- `open_file_dialog()` uses `zenity` when available.
- The Linux backend intentionally keeps a simpler rendering path than Windows in the current release.

Example builds from the repo root:

```bash
clang++ demo.cpp -o demo.exe -std=c++17 -ld2d1 -ldwrite -lgdi32 -lole32 -luuid -luser32 -lwindowscodecs
clang++ examples/benchmark.cpp -o benchmark.exe -std=c++17 -ld2d1 -ldwrite -lgdi32 -lole32 -luuid -luser32 -lwindowscodecs
```

## Repository layout

- `ftui.hpp`: the single-header library
- `demo.cpp`: compatibility entrypoint that builds the showcase example
- `examples/showcase.cpp`: broad widget and layout tour
- `examples/control_panel.cpp`: settings-heavy utility app example
- `examples/log_viewer.cpp`: read-only output and operator-notes example
- `examples/benchmark.cpp`: synthetic workload / FPS stress example
- `ftui.svg`: built-in icon source used by the project

## Compatibility

FTUI 1.x treats the common surface as additive-first.

- Core config types like `Config.title` stay stable through 1.x.
- Existing calls like `input()`, `text_area()`, `tabs()`, `row(int, ...)`, and `open_child_window()` remain valid.
- New capability is added through new helpers, new widgets, and additive flags rather than changing the shape of existing calls.

## Themes

New windows start with the style configured near the top of `ftui.hpp`:

```cpp
#ifndef FTUI_DEFAULT_STYLE
#define FTUI_DEFAULT_STYLE ftui::one_dark_style
#endif
```

If the application developer does not define `FTUI_DEFAULT_STYLE`, FTUI also checks `FTUI_THEME` at startup. Vim-style theme commands such as `:to`, `:tn`, and `:th` update `FTUI_THEME` for the current process, so child windows and later windows can reuse the selected theme. A developer-defined `FTUI_DEFAULT_STYLE` always takes precedence.

Vim-style commands:

- `:td` default dark
- `:tc` Catppuccin Mocha
- `:tn` Nord
- `:tg` Gruvbox dark
- `:to` One Dark
- `:th` Ghostty green
- `:q` quit

Built-in presets:

```cpp
ftui::set_style(ftui::default_dark_style());
ftui::set_style(ftui::catppuccin_mocha_style());
ftui::set_style(ftui::nord_style());
ftui::set_style(ftui::gruvbox_dark_style());
ftui::set_style(ftui::one_dark_style());
ftui::set_style(ftui::ghostty_green_style());
```

The built-in presets are the library’s named color packs. Each one fully initializes the semantic color roles used by FTUI, including `accent`, `warning`, and `success`.

Hex colors are supported directly:

```cpp
ftui::Color red = ftui::color_from_hex("#ff4d4f");
ftui::Color translucent = ftui::color_from_hex("#3b82f680");
```

Manual overrides stay immediate-mode and scoped:

```cpp
ftui::set_next_color(ftui::ColorRole::Text, ftui::color_from_hex("#ffffff"));
ftui::set_next_color(ftui::ColorRole::Button, ftui::color_from_hex("#c93c3c"));
ftui::button("One-shot red");

ftui::push_color(ftui::ColorRole::Button, ftui::color_from_hex("#2f855a"));
ftui::button("Scoped green");
ftui::button("Still green");
ftui::pop_color();
```

Semantic roles also work directly at the callsite:

```cpp
ftui::button("Primary", ftui::ColorRole::Accent);
ftui::button("Delete", ftui::ColorRole::Warning);
ftui::button("Healthy", ftui::ColorRole::Success);
ftui::button("Custom", ftui::color_from_hex("#7c3aed"));
```

## Window chrome and icons

On Windows, the titlebar color follows the active FTUI theme automatically.

Use the built-in icon variants:

```cpp
ftui::set_window_icon_builtin(ftui::BuiltinIcon::Symbol);
ftui::set_window_icon_builtin(ftui::BuiltinIcon::SymbolWithText);
```

Or pass a native icon handle directly on Windows:

```cpp
ftui::set_window_icon(native_hicon);
```

## Core widgets

### Text

```cpp
ftui::text("Section title");
ftui::text_wrapped("Longer help text that should wrap inside the current content width.");
ftui::separator();
ftui::spacing(12.0f);
```

### Buttons

```cpp
if (ftui::button("Run")) {
    // fired on mouse-up inside the button
}
```

Optional tint overloads keep one-off emphasis straightforward without changing the surrounding theme:

```cpp
ftui::button("Promote", ftui::ColorRole::Accent);
ftui::button("Danger", ftui::ColorRole::Warning);
ftui::button("Success", ftui::color_from_hex("#22c55e"));
```

Visible labels can be reused by suffixing an internal ID:

```cpp
ftui::button("Open##file");
ftui::button("Open##folder");
```

### Side menus

Drawer-style navigation:

```cpp
static const char* pages[] = {"Dashboard", "Network", "Services", "Logs", "Settings"};
int page = 0;

ftui::side_menu_drawer("Navigation", pages, 5, &page);

if (page == 0) ftui::text("Dashboard");
else if (page == 1) ftui::text("Network");
```

Embedded sidebar navigation:

```cpp
static const char* pages[] = {"Dashboard", "Network", "Services", "Logs", "Settings"};
int page = 0;

ftui::side_layout(220.0f, [&]() {
    ftui::side_menu("Navigation", pages, 5, &page);
    ftui::content([&]() {
        if (page == 0) ftui::text("Dashboard");
        else if (page == 1) ftui::text("Network");
    });
});
```

Use `side_menu_drawer(...)` when you want an app-style slide-out sidebar. Use `side_layout(...)` or `split({220.0f, 1.0f}, ...)` directly when you want a persistent sidebar. Values `>= 16` are fixed pixel columns; smaller values are flexible weights.

### Toasts

```cpp
ftui::toast("Saved");
ftui::toast_success("Configuration saved");
ftui::toast_warning("High CPU usage");
ftui::toast_error("Connection failed");
```

For custom timing:

```cpp
ftui::Toast t;
t.message = "Relay restarted";
t.duration_ms = 5000;
t.dismissible = true;
t.type = ftui::ToastType::Success;
ftui::toast(t);
```

### Progress bars

```cpp
float progress = 0.73f;
ftui::progress_bar(progress);
ftui::progress_bar(progress, "Loading assets");
```

Masked progress bars fill the white/opaque area of an SVG, PNG, or image from left to right:

```cpp
ftui::ProgressStyle style;
style.mask_path = "battery.png";
style.wave_front = true;
style.glint = true;
ftui::progress_bar(progress, style);
```

For single-binary examples, embed SVG text directly:

```cpp
ftui::ProgressStyle battery;
battery.mask_svg = "<svg width='160' height='64' viewBox='0 0 160 64'>"
                   "<rect x='4' y='12' width='132' height='40' fill='white'/>"
                   "<rect x='140' y='24' width='16' height='16' fill='white'/>"
                   "</svg>";
ftui::progress_bar(progress, battery);
```

Built-in masks are available by name:

```cpp
ftui::progress_bar(progress, "battery");

ftui::ProgressStyle pill;
pill.mask_shape = "pill"; // battery, tank, pill, circle, logo
ftui::progress_bar(progress, pill);
```

### Single-line input

```cpp
char username[128] = "";
char password[128] = "";
bool submitted = false;

ftui::input("Username", username, sizeof(username));
ftui::input("Password", password, sizeof(password), ftui::InputFlags::Password, &submitted);
```

Additive filtering flags:

```cpp
char port[16] = "8080";
char code[32] = "";

ftui::input("Port", port, sizeof(port), ftui::InputFlags::CharsDecimal);
ftui::input("Code", code, sizeof(code),
            ftui::InputFlags::CharsUppercase | ftui::InputFlags::CharsNoBlank);
```

Read-only input:

```cpp
ftui::input("Token", code, sizeof(code), ftui::InputFlags::ReadOnly);
```

### Multiline text

Simple multiline editor:

```cpp
char notes[2048] = "";
ftui::text_area("Notes", notes, sizeof(notes), 8);
```

Extended multiline editor:

```cpp
ftui::text_area_ex("Notes", notes, sizeof(notes), 8,
                   ftui::TextAreaFlags::WordWrap);
```

Read-only / wrapped variants:

```cpp
ftui::text_area_ex("Preview", notes, sizeof(notes), 6,
                   ftui::TextAreaFlags::ReadOnly | ftui::TextAreaFlags::WordWrap);
```

### Log / output view

```cpp
const char* log_text =
    "[09:14] Boot complete.\n"
    "[09:15] Waiting for input.\n";

ftui::log_view("Output", log_text, 10,
               ftui::LogViewFlags::WordWrap | ftui::LogViewFlags::AutoScrollBottom);
```

`log_view()` is intended for logs, transcripts, debug output, and read-only history panes.

### Checkbox and slider

```cpp
bool enabled = false;
float blend = 0.5f;

ftui::checkbox("Enable option", &enabled);
ftui::slider_float("Blend", &blend, 0.0f, 1.0f);
```

### Tabs

```cpp
static const char* tabs[] = { "Login", "Notes", "Settings" };
int selected_tab = 0;

ftui::tabs(tabs, 3, &selected_tab);
```

### Pickers

Dropdown:

```cpp
static const char* envs[] = { "Local", "Staging", "Production" };
int env = 0;

ftui::dropdown("Environment", envs, 3, &env);
```

Dropdown popups render as top-layer overlays, flip upward when needed to stay in bounds, and use the lightweight frosted popup treatment on Windows when effects are enabled.

Listbox:

```cpp
static const char* roles[] = { "Admin", "Observer", "Maintainer" };
int role = 0;

ftui::listbox("Role", roles, 3, &role, 4);
```

Radio group:

```cpp
static const char* shells[] = { "Powershell", "Bash", "Cmd" };
int shell = 0;

ftui::radio_group("Shell", shells, 3, &shell, 1);
```

### Collapsible sections

```cpp
bool advanced_open = true;

if (ftui::collapsing_header("Advanced", &advanced_open)) {
    ftui::checkbox("Enable tracing", &enabled);
}
```

With the Windows effects path enabled, collapsible sections reveal and hide their body gradually and the widgets below them ease into their new positions instead of snapping.

## Layout helpers

Equal row:

```cpp
ftui::row(3, [&]() {
    ftui::button("Left");
    ftui::button("Center");
    ftui::button("Right");
});
```

Weighted row:

```cpp
ftui::row({2.0f, 1.0f}, [&]() {
    ftui::button("Wide");
    ftui::button("Narrow");
});
```

One-shot width helpers:

```cpp
ftui::set_next_width(220.0f);
ftui::button("Fixed width");

ftui::set_next_percent(0.60f);
ftui::button("60 percent");

ftui::set_next_fill();
ftui::button("Fill width");

ftui::set_next_percent(0.75f);
ftui::set_next_limits(180.0f, 280.0f);
ftui::set_next_align(ftui::Align::End);
ftui::button("Aligned and clamped");
```

### Scroll areas

```cpp
ftui::scroll_area("History", 220.0f, [&]() {
    for (int i = 0; i < 20; ++i) {
        char line[64];
        snprintf(line, sizeof(line), "Event %02d", i + 1);
        ftui::text(line);
    }
});
```

Scroll areas keep their own scroll state and consume wheel input when hovered.

## State helpers

Disabled scope:

```cpp
ftui::begin_disabled();
ftui::button("Disabled button");
ftui::end_disabled();
```

Tooltips:

```cpp
ftui::button("Hover me");
ftui::tooltip("Shown after a short steady hover.");
```

Tooltips fade in and out, wait for a steady cursor hover, and back off more aggressively if the user repeatedly moves away right after they appear.

Request focus:

```cpp
if (ftui::button("Focus username")) {
    ftui::request_focus("Username");
}
```

Text measurement:

```cpp
float w = ftui::calc_text_width("Status");
float h = ftui::calc_text_height(long_text, 280.0f);
```

## Modal and child windows

Modal:

```cpp
if (ftui::button("Reset")) {
    ftui::open_modal("Confirm reset");
}

ftui::modal("Confirm reset", [&]() {
    ftui::text_wrapped("This blocks background interaction until closed.");
    if (ftui::button("Cancel")) ftui::close_modal();
});
```

Child window:

```cpp
ftui::Config child;
child.title = "Details";
child.width = 640;
child.height = 360;

ftui::open_child_window(child, [&]() {
    ftui::text("Child window content");
});
```

## Images and file dialogs

```cpp
ftui::ImageHandle* img = ftui::load_image("photo.png");
ftui::image(img, 200.0f, 150.0f);
ftui::free_image(img);
```

```cpp
static const ftui::FileFilter filters[] = {
    { "Images", "*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff" },
    { "All Files", "*.*" },
};

std::string path = ftui::open_file_dialog("Open Image", filters, 2);
```

## Windows effects

Windows can enable a lightweight effects layer with:

- smooth window and widget scrolling
- hover and press easing
- tab underline motion
- tab content slide motion
- collapsible section reveal and layout easing
- frosted dropdown popups on Windows

Runtime opt-out:

```cpp
ftui::Config cfg;
cfg.enable_effects = false;
```

Compile-time strip:

```cpp
#define FTUI_DISABLE_EFFECTS
#define FTUI_IMPLEMENTATION
#include "ftui.hpp"
```

Linux keeps the same API and behavior model, but currently uses a flatter rendering path without the Windows-only effects layer.[^linux]

## Keyboard behavior

- `Tab` / `Shift+Tab` cycles interactive widgets
- `Enter` / `Space` activate focused buttons and picker widgets
- Arrow keys navigate tabs, radios, listboxes, and dropdown selections
- `Enter` inserts newline in multiline text
- `Ctrl+C` / `Ctrl+V` work in text widgets where applicable
- `Ctrl+Q` quits by default
- `:` opens command mode when no text widget is focused

Disable the built-in quit shortcut:

```cpp
ftui::set_quit_on_ctrl_q(false);
```

## Examples

[`demo.cpp`](/C:/Users/aridlin/code/claude/ftui/demo.cpp) remains the easiest root-level build target and now simply includes the showcase example so older commands still work.

- [`examples/showcase.cpp`](/C:/Users/aridlin/code/claude/ftui/examples/showcase.cpp): broad FTUI tour covering forms, text areas, log output, scroll areas, themes, modals, and child windows
- [`examples/control_panel.cpp`](/C:/Users/aridlin/code/claude/ftui/examples/control_panel.cpp): a settings-heavy control panel with collapsible sections, confirmation flow, and activity output
- [`examples/log_viewer.cpp`](/C:/Users/aridlin/code/claude/ftui/examples/log_viewer.cpp): a focused log/transcript app using `log_view()`, multiline notes, filters, and a nested history scroller
- [`examples/benchmark.cpp`](/C:/Users/aridlin/code/claude/ftui/examples/benchmark.cpp): a synthetic benchmark app for row-count, workload, and frame-pacing checks with the FPS overlay enabled

[^linux]: Linux support is first-class at the API level and actively maintained, but the Windows backend currently receives the richer visual effects path first so the Linux renderer can stay lean and dependency-light.
