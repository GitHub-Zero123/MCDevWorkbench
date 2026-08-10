#pragma once

#include <string>

namespace mcdev::platform {

struct WindowSize {
    int width = 0;
    int height = 0;
};

[[nodiscard]] WindowSize initialWindowSize(int logicalWidth, int logicalHeight);
[[nodiscard]] std::string preferredUiFont();
void applyWindowTheme();

} // namespace mcdev::platform
