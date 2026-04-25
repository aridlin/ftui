# FTUI

Tiny immediate-mode GUI for Windows. Single header, no CMake, no dependencies beyond the Win32 SDK.[^linux]

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
clang++ main.cpp -o app.exe -std=c++17 -ld2d1 -ldwrite -lgdi32 -lole32 -luuid -luser32 -lwindowscodecs
```

With MSVC the `#pragma comment(lib, ...)` lines inside the header handle linking automatically.

---

## Distribution

Copy `ftui.hpp` into your project. In exactly one `.cpp` file:

```cpp
#define FTUI_IMPLEMENTATION
#include "ftui.hpp"
```

All other files just `#include "ftui.hpp"` without the define.

---

## Lifecycle

```cpp
ftui::Config cfg;
cfg.title         = "My App";
cfg.width         = 960;
cfg.height        = 640;
cfg.resizable     = true;
cfg.center_window = true;
cfg.icon          = nullptr; // HICON, or nullptr for the built-in FTUI logo

ftui::create_window(cfg);   // create platform window + renderer resources
ftui::pump();               // process messages, returns false when closed
ftui::begin();              // start frame - resets layout cursor, clears background
ftui::end();                // finish frame - present to screen
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
ftui::separator();    // thin horizontal rule
ftui::spacing(16.0f); // blank vertical gap (default 8px)
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

### Text boxes

```cpp
char user[128] = "";
if (ftui::input("Username", user, sizeof(user))) {
    // contents changed this frame
}

char pw[128] = "";
ftui::input("Password", pw, sizeof(pw), ftui::InputFlags::Password);
ftui::input("Read only", user, sizeof(user), ftui::InputFlags::ReadOnly);

bool submitted = false;
char query[128] = "";
ftui::input("Search", query, sizeof(query), ftui::InputFlags::Default, &submitted);
if (ftui::button("Go") || submitted) { /* run search */ }
```

For a multi-line text box:

```cpp
char notes[512] = "";
ftui::text_area("Notes##notes", notes, sizeof(notes), 6);
```

`Tab` / `Shift+Tab` cycle focus between visible text boxes without touching the mouse. In `text_area()`, `Enter` inserts a newline and `Ctrl+C` / `Ctrl+V` copy and paste selected text.

### Tabs

```cpp
static const char* sections[] = { "Login", "Notes", "Settings" };
int selected_tab = 0;

if (ftui::tabs(sections, 3, &selected_tab)) {
    // selected tab changed
}

if (selected_tab == 0) {
    ftui::input("Username", user, sizeof(user));
    ftui::input("Password", pw, sizeof(pw), ftui::InputFlags::Password);
} else if (selected_tab == 1) {
    ftui::text_area("Notes##notes", notes, sizeof(notes), 6);
} else {
    ftui::text("Settings page");
}
```

Tabs render a horizontal tab bar and update `selected_tab` in place.

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

### Row (columns)

Temporarily switches the layout to horizontal, splitting the available width into N equal cells separated by `item_spacing` gaps. Widgets called inside the lambda are placed left-to-right; after `fn()` returns the cursor advances below the tallest cell.

```cpp
ftui::row(3, [&]() {
    if (ftui::button("Left"))   { /* ... */ }
    if (ftui::button("Center")) { /* ... */ }
    if (ftui::button("Right"))  { /* ... */ }
});
```

Any widget type works inside a row. Excess widgets beyond `N` are placed anyway (they overflow to the right).

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
    { "Images", "*.png;*.jpg;*.bmp" },
    { "All Files", "*.*" },
};

std::string path = ftui::open_file_dialog("Open Image", filters, 2);
if (!path.empty()) {
    // path is a UTF-8 absolute file path
}
```

The dialog is modal and blocks the frame loop while open. On Linux it uses `zenity` when available.[^linux]

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

Call `set_style()` at any time - it takes effect from the next `begin()`.

### Custom theme

```cpp
ftui::Style s = ftui::default_dark_style(); // start from a preset
s.input_focus = {0.8f, 0.3f, 0.2f, 1.0f};   // override accent color
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

By default FTUI suppresses the console window via a linker pragma. To keep it (for example, during development):

```cpp
#define FTUI_KEEP_CONSOLE
#define FTUI_IMPLEMENTATION
#include "ftui.hpp"
```

---

## Keyboard shortcuts

| Key | Action |
|---|---|
| **Tab** | Focus next text box |
| **Shift+Tab** | Focus previous text box |
| **Enter** | Signals `enter_pressed` in the focused single-line input; inserts a newline in `text_area()` |
| **Backspace** | Deletes the previous character in the focused text box |
| **Ctrl+Q** | Quit |
| **:** | Enter command mode (only when no input field is focused) |
| **Escape** | Cancel command mode |

### Command mode

Press `:` when no input is focused to open a command prompt (shown at the bottom-left). Type a command and press **Enter** to execute it. **Escape** or a mouse click cancels.

| Command | Action |
|---|---|
| `:q` | Quit |
| `:td` | Switch to Default Dark theme |
| `:tc` | Switch to Catppuccin Mocha theme |
| `:tn` | Switch to Nord theme |
| `:tg` | Switch to Gruvbox Dark theme |
| `:to` | Switch to One Dark theme |

All built-in shortcuts (`Ctrl+Q` and command mode) share a single toggle:

```cpp
// Disable all built-in shortcuts (for example, for a shipped app)
ftui::set_quit_on_ctrl_q(false);
```

---

## Scrolling

FTUI scrolls automatically. When the total content height exceeds the window height a scrollbar appears on the right and content is clipped to the visible area. No setup required.

`Mouse wheel` scrolls 80 px per tick. The scrollbar thumb is draggable. Clicking the track above or below the thumb pages by one visible height.

---

## Non-goals for v1

Scrollable containers, nested layouts, drag-and-drop, markdown, animations, docking, theme editors. FTUI stays small on purpose.

[^linux]: FTUI also has a Linux backend using X11 + Cairo. Build with `g++ main.cpp -o app -std=c++17 $(pkg-config --cflags --libs cairo x11)`. If you use `open_file_dialog()` on Linux, install `zenity`.
