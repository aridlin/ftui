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

Built-in presets:

```cpp
ftui::set_style(ftui::default_dark_style());
ftui::set_style(ftui::catppuccin_mocha_style());
ftui::set_style(ftui::nord_style());
ftui::set_style(ftui::gruvbox_dark_style());
ftui::set_style(ftui::one_dark_style());
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

Visible labels can be reused by suffixing an internal ID:

```cpp
ftui::button("Open##file");
ftui::button("Open##folder");
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

## Demo

[`demo.cpp`](/C:/Users/aridlin/code/claude/ftui/demo.cpp) shows:

- wrapped text and filtered input
- editable multiline text
- read-only log output
- dropdown, listbox, and radio pickers
- scroll areas
- collapsible sections
- modal flow
- disabled scopes and tooltips
- weighted rows and one-shot layout helpers

[^linux]: Linux support is first-class at the API level and actively maintained, but the Windows backend currently receives the richer visual effects path first so the Linux renderer can stay lean and dependency-light.
