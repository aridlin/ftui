#include <functional>
#include <initializer_list>
#include <type_traits>

#include "ftui.hpp"

using InputSig = bool(*)(const char*, char*, int, ftui::InputFlags, bool*);
using TextAreaSig = bool(*)(const char*, char*, int, int);
using TabsSig = bool(*)(const char* const*, int, int*);
using RowIntSig = void(*)(int, std::function<void()>);
using ChildSig = void(*)(const ftui::Config&, std::function<void()>);

static_assert(std::is_same_v<decltype(ftui::Config{}.title), const char*>);
static_assert(std::is_same_v<decltype(&ftui::input), InputSig>);
static_assert(std::is_same_v<decltype(&ftui::text_area), TextAreaSig>);
static_assert(std::is_same_v<decltype(&ftui::tabs), TabsSig>);
static_assert(std::is_same_v<decltype(static_cast<RowIntSig>(&ftui::row)), RowIntSig>);
static_assert(std::is_same_v<decltype(&ftui::open_child_window), ChildSig>);

int main() {
    return 0;
}
