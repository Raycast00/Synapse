#pragma once

#include "imgui.h"

// Neverlose-style menu, adapted for iOS (point-based responsive layout).
namespace menu
{
    // Builds the embedded fonts and installs the dark style.
    // Must be called once after ImGui::CreateContext() and before the first frame.
    void init();

    // Draws the full-screen menu shell inside the given rectangle (screen points).
    // A click on the close button writes false to 'open'.
    void render(bool* open, const ImVec2& pos, const ImVec2& size);

    // Draws the small launcher pill that opens the menu (shown only when closed).
    void launcher(bool* open, const ImVec2& pos, const ImVec2& size);
}
