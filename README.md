# FTUI

FTUI is a tiny immediate-mode GUI for C++.

It ships as a single header, uses the native platform stack directly, and is built for "drop one file into a project and start drawing UI" workflows.

- Windows backend: Win32 + Direct2D + DirectWrite
- Linux backend: X11 + Cairo
- Distribution model: single header, no CMake required, no external runtime dependencies beyond platform SDKs/libs

## Why FTUI

- Single-header library: copy `ftui.hpp` into a project and include it
- Immediate-mode API: just call widgets each frame in layout order
- No coordinate soup: widgets flow top-to-bottom automatically
- Built-in themes: default dark, Catppuccin Mocha, Nord, Gruvbox Dark, One Dark
- Default runtime theme: controlled by `FTUI_DEFAULT_STYLE` near the top of `ftui.hpp`
- Windows polish path: smooth scrolling, tab slide transitions, hover/press motion, optional effects toggle
- Linux support included: same API, simpler visual path

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

Every other translation unit should include the header without `FTUI_IMPLEMENTATION`.

## Build

Windows with `clang++`:

```bash
clang++ main.cpp -o app.exe -std=c++17 -ld2d1 -ldwrite -lgdi32 -lole32 -luuid -luser32 -lwindowscodecs
```

Windows with MSVC:

- The header already contains the `#pragma comment(lib, ...)` lines for the required Windows libs.

Linux:

```bash
g++ main.cpp -o app -DFTUI_IMPLEMENTATION $(pkg-config --cflags --libs cairo x11) -std=c++17
```

If you use `open_file_dialog()` on Linux, install `zenity`.

## Core lifecycle

```cpp
ftui::Config cfg;
cfg.title          = "FTUI App";
cfg.width          = 960;
cfg.height         = 640;
cfg.resizable      = true;
cfg.center_window  = true;
cfg.icon           = nullptr; // HICON on Windows, ignored on Linux
cfg.enable_effects = true;    // Windows-only effects layer

if (!ftui::create_window(cfg)) return 1;

while (ftui::pump()) {
    ftui::begin();

    // widgets

    ftui::end();
}

ftui::shutdown();
```

## Widget examples

### Text, separator, spacing

```cpp
ftui::text("Section title");
ftui::separator();
ftui::spacing(12.0f);
```

### Buttons

```cpp
if (ftui::button("Sign in")) {
    // fired on mouse-up inside the button
}
```

Visible labels can be reused by adding an ID suffix:

```cpp
ftui::button("Open##file");
ftui::button("Open##folder");
```

### Single-line text boxes

```cpp
char username[128] = "";
char password[128] = "";
bool submitted = false;

ftui::input("Username", username, sizeof(username));
ftui::input("Password", password, sizeof(password), ftui::InputFlags::Password, &submitted);

if (submitted || ftui::button("Login")) {
    // submit
}
```

Read-only input:

```cpp
ftui::input("Token", username, sizeof(username), ftui::InputFlags::ReadOnly);
```

### Multi-line text box

```cpp
char notes[1024] = "";
ftui::text_area("Notes##main", notes, sizeof(notes), 8);
```

`text_area()` supports selection, copy/paste, and internal scrolling.

### Tabs

```cpp
static const char* tabs[] = { "Login", "Notes", "Settings" };
int selected_tab = 0;

ftui::tabs(tabs, 3, &selected_tab);

if (selected_tab == 0) {
    ftui::text("Login page");
} else if (selected_tab == 1) {
    ftui::text_area("Notes##notes", notes, sizeof(notes), 6);
} else {
    ftui::text("Settings page");
}
```

### Checkbox

```cpp
bool enabled = false;
ftui::checkbox("Enable option", &enabled);
```

### Slider

```cpp
float blend = 0.5f;
ftui::slider_float("Blend", &blend, 0.0f, 1.0f);
```

### Row layout

```cpp
ftui::row(3, [&]() {
    ftui::button("Left");
    ftui::button("Center");
    ftui::button("Right");
});
```

Rows split the current width into equal cells for the duration of the lambda.

### Images

```cpp
ftui::ImageHandle* img = ftui::load_image("photo.png");

ftui::image(img, 200.0f, 150.0f);

ftui::free_image(img);
```

On Windows, image decoding goes through WIC. On Linux, PNG loading uses Cairo image surfaces.

### File dialog

```cpp
static const ftui::FileFilter filters[] = {
    { "Images", "*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff" },
    { "All Files", "*.*" },
};

std::string path = ftui::open_file_dialog("Open Image", filters, 2);
if (!path.empty()) {
    // UTF-8 absolute path
}
```

### Child windows

```cpp
ftui::Config child;
child.title = "Details";
child.width = 640;
child.height = 360;

ftui::open_child_window(child, [&]() {
    ftui::text("Child window content");
    ftui::button("Close me manually");
});
```

## Themes

Built-in presets:

```cpp
ftui::set_style(ftui::default_dark_style());
ftui::set_style(ftui::catppuccin_mocha_style());
ftui::set_style(ftui::nord_style());
ftui::set_style(ftui::gruvbox_dark_style());
ftui::set_style(ftui::one_dark_style());
```

New windows start with whatever `FTUI_DEFAULT_STYLE` is set to near the top of `ftui.hpp`. By default it points to `ftui::nord_style`.

Custom theme example:

```cpp
ftui::Style s = ftui::default_dark_style();
s.input_focus = {0.80f, 0.30f, 0.20f, 1.0f};
s.rounding = 4.0f;
ftui::set_style(s);
```

## Windows effects

On Windows, FTUI can enable a lightweight effects layer with:

- smooth window scrolling
- animated hover and press states
- tab underline motion
- tab content slide transitions

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

Linux keeps the same API but does not use the Windows effects path.

## Keyboard behavior

- `Tab` / `Shift+Tab`: move focus between text inputs
- `Enter`: submit single-line input through `enter_pressed`; insert newline in `text_area()`
- `Backspace`: delete in focused text input
- `Ctrl+C` / `Ctrl+V`: copy and paste selected text
- `Ctrl+Q`: quit by default
- `:`: open command mode when no input is focused
- `Escape`: cancel command mode

Disable the built-in quit / command shortcuts:

```cpp
ftui::set_quit_on_ctrl_q(false);
```

Command mode supports:

- `:q`
- `:td`
- `:tc`
- `:tn`
- `:tg`
- `:to`

## Notes

- Layout is sequential and immediate-mode by design.
- Scrolling is automatic when content exceeds the visible region.
- The API is intentionally small: no docking, retained widget tree, or layout editor.
- Linux support exists and is maintained, but the Windows path currently gets the richer visual treatment first.

## Demo

`demo.cpp` shows:

- tabs
- text boxes and password input
- multi-line notes
- checkbox and slider
- theme switching
- file dialog usage
- images
- child windows
- debug overlays
