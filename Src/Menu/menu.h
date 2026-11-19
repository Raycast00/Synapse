#pragma once

#include "imgui.h"

// Strict iOS port of the nlka menu (examples/example_win32_directx11/neverlose_menu.cpp).
// The original 750x756 canvas is kept exactly and uniformly scaled to the screen.
namespace menu
{
    // Builds the embedded fonts and installs the dark style.
    // Must be called once after ImGui::CreateContext() and before the first frame.
    void init();

    // Draws the full menu. 'origin' is the screen position of canvas (0,0) and
    // 'scale' is the canvas-to-screen factor.
    void render(bool* open, const ImVec2& origin, float scale);

    // Draws the small launcher pill that opens the menu (shown only when closed).
    void launcher(bool* open, const ImVec2& pos, const ImVec2& size);
}
