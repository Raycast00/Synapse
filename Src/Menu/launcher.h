// Menu source watermark: @use_sberbank
#pragma once

#include "imgui.h"

namespace nl {

// Small floating pill that opens the menu. It draws itself in its own invisible
// full-screen window and reports where it was, so the iOS overlay can route touches
// to it. The metrics are independent of the menu's UI scale.
struct LauncherRect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool pressed = false;
};

LauncherRect launcher_button();

} // namespace nl
