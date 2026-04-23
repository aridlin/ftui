# FTUI

Tiny immediate-mode GUI for Windows. Single header, no CMake, no dependencies beyond the Win32 SDK.

```cpp
#define FTUI_IMPLEMENTATION
#include "ftui.hpp"

int main() {
    if (!ftui::create_window()) return 1;

    char name[64] = "";

    while (ftui::pump()) {
        ftui::begin();

        ftui::text("Hello");
        ftui::input("Name", name, sizeof(name));
        if (ftui::button("Go")) { /* ... */ }

        ftui::end();
    }

    ftui::shutdown();
}
```

---

## Compile

```bash
clang++ main.cpp -o app.exe -ld2d1 -ldwrite -lgdi32 -lole32 -luuid -luser32 -std=c++17
```

With MSVC the `#pragma comment(lib, ...)` lines inside the header handle linking automatically.

---

## Distribution

Copy `ftui.hpp` into your project. In **exactly one** `.cpp` file:

```cpp
#define FTUI_IMPLEMENTATION
#include "ftui.hpp"
```

All other files just `#include "ftui.hpp"` without the define.

---

## Lifecycle

```cpp
ftui::Config cfg;
cfg.title         = L"My App";
cfg.width         = 960;
cfg.height        = 640;
cfg.resizable     = true;
cfg.center_window = true;
cfg.icon          = nullptr; // HICON, or nullptr for the built-in FTUI logo

ftui::create_window(cfg);   // create Win32 window + Direct2D + DirectWrite + COM
ftui::pump();               // process messages, returns false when closed
ftui::begin();              // start frame — resets layout cursor, clears background
ftui::end();                // finish frame — present to screen
ftui::shutdown();           // release all resources
```

---

## Widgets

All widgets are placed in call order, top to bottom, full content width. No coordinates.

### Text

```cpp
ftui::text("Hello, world!");
```

### Separator and spacing

```cpp
ftui::separator();          // thin horizontal rule
ftui::spacing(16.0f);       // blank vertical gap (default 8px)
```

### Button

```cpp
if (ftui::button("Click me")) {
    // fired on mouse-up inside the button
}
```

Supports `##` suffix for disambiguation when two buttons share the same visible label:

```cpp
ftui::button("Open##file_open");
ftui::button("Open##folder_open");
```

### Text input

```cpp
char buf[128] = "";
if (ftui::input("Label", buf, sizeof(buf))) {
    // contents changed this frame
}

// With flags
ftui::input("Password", pw, sizeof(pw), ftui::InputFlags::Password);
ftui::input("Read only", buf, sizeof(buf), ftui::InputFlags::ReadOnly);

// Enter to submit
bool submitted = false;
ftui::input("Search", query, sizeof(query), ftui::InputFlags::None, &submitted);
if (ftui::button("Go") || submitted) { /* run search */ }
```

**Tab / Shift+Tab** cycle focus between all visible inputs without touching the mouse. Clicking focuses a field. Backspace removes the last character.

### Checkbox

```cpp
bool enabled = false;
if (ftui::checkbox("Enable feature", &enabled)) {
    // toggled this frame
}
```

### Slider

```cpp
float volume = 0.5f;
if (ftui::slider_float("Volume", &volume, 0.0f, 1.0f)) {
    // value changed this frame
}
```

### Image

```cpp
ftui::ImageHandle* img = ftui::load_image("photo.png"); // PNG, JPEG, BMP, GIF, TIFF

ftui::image(img, 200.0f, 150.0f); // width, height in logical pixels

ftui::free_image(img);
```

Passing `nullptr` renders a placeholder box. Images are centered horizontally.

### File dialog

```cpp
static const ftui::FileFilter filters[] = {
    { L"Images", L"*.png;*.jpg;*.bmp" },
    { L"All Files", L"*.*" },
};

std::string path = ftui::open_file_dialog(L"Open Image", filters, 2);
if (!path.empty()) {
    // path is a UTF-8 absolute file path
}
```

The dialog is modal and blocks the frame loop while open (Windows handles the inner message loop).

---

## Themes

Five built-in themes, all returning a `ftui::Style`:

```cpp
ftui::set_style(ftui::default_dark_style());     // FTUI default
ftui::set_style(ftui::catppuccin_mocha_style()); // Catppuccin Mocha
ftui::set_style(ftui::nord_style());             // Nord
ftui::set_style(ftui::gruvbox_dark_style());     // Gruvbox Dark
ftui::set_style(ftui::one_dark_style());         // One Dark (Atom)
```

Call `set_style()` at any time — takes effect from the next `begin()`.

### Custom theme

```cpp
ftui::Style s = ftui::default_dark_style(); // start from a preset
s.input_focus = {0.8f, 0.3f, 0.2f, 1.0f}; // override accent color
s.rounding    = 4.0f;
ftui::set_style(s);
```

`ftui::Style` fields:

| Field | Type | Purpose |
|---|---|---|
| `background` | `Color` | Window background |
| `panel` | `Color` | Widget fill (buttons, etc.) |
| `text` | `Color` | Primary text |
| `text_dim` | `Color` | Labels, secondary text |
| `border` | `Color` | Widget borders |
| `button` / `button_hover` / `button_active` | `Color` | Button states |
| `input_bg` | `Color` | Input field background |
| `input_focus` | `Color` | Focused input border / accent |
| `window_padding` | `float` | Outer padding (px) |
| `item_spacing` | `float` | Gap between widgets (px) |
| `item_height` | `float` | Height of interactive controls (px) |
| `rounding` | `float` | Corner radius (px) |
| `border_width` | `float` | Stroke width (px) |
| `font_size` | `float` | Font size in DIPs |

---

## Debug overlays

```cpp
ftui::debug().show_layout_rects = true; // outline every widget rect
ftui::debug().show_hovered_id   = true; // show hot/active/focused IDs
ftui::debug().show_active_id    = true;
ftui::debug().show_fps          = true; // FPS + DPI% in corner
ftui::debug().log_widget_calls  = true; // log to OutputDebugString (DebugView)
```

---

## Console window

By default FTUI suppresses the console window via a linker pragma. To keep it (e.g. during development):

```cpp
#define FTUI_KEEP_CONSOLE
#define FTUI_IMPLEMENTATION
#include "ftui.hpp"
```

---

## Keyboard shortcuts

| Key | Action |
|---|---|
| **Tab** | Focus next input field |
| **Shift+Tab** | Focus previous input field |
| **Enter** | Signals `enter_pressed` in the focused input |
| **Backspace** | Deletes last character in focused input |
| **Ctrl+Q** | Quit (enabled by default, disable with `ftui::set_quit_on_ctrl_q(false)`) |

```cpp
// Disable Ctrl+Q (e.g. for a shipped app where accidental quit would be bad)
ftui::set_quit_on_ctrl_q(false);
```

---

## Non-goals for v1

Selection, clipboard, scrollable containers, nested layouts, drag-and-drop, markdown, animations, docking, themes editor. FTUI stays small on purpose.
