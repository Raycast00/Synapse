#pragma once

// Neverlose-style ImGui menu, reworked for iOS (Metal / touch).
//
// Usage inside the ImGui frame:
//
//     if (menu::begin(g_open))
//     {
//         if (menu::page(menu::Page::Visuals))
//         {
//             menu::card("ENEMY");
//             menu::toggle("Enabled", &visuals::esp_enabled);
//             menu::endCard();
//         }
//         menu::end();
//     }

#include "imgui.h"

namespace menu
{
    enum class Page
    {
        Visuals,
        Aim,
        Rage,
        Misc,
        Settings
    };

    // Creates the fonts (embedded) and installs the theme style.
    // Call once, right after ImGui::CreateContext().
    void initialize();

    // Draws the floating launcher button and, when 'open' is true, the menu shell.
    // Returns true when the caller should submit page content (must be followed by end()).
    bool begin(bool& open);
    void end();

    // Starts the content of one page. Returns true only for the active page.
    bool page(Page id);

    // Content building blocks (valid between page() and end()).
    void card(const char* title);
    void endCard();

    bool toggle(const char* label, bool* value, bool enabled = true);
    bool toggleColor(const char* label, bool* value, float color[4], bool enabled = true);
    void color(const char* label, float color[4], bool enabled = true);
    bool slider(const char* label, float* value, float min_value, float max_value, const char* fmt, bool enabled = true);
    bool combo(const char* label, int* current, const char* const items[], int count, bool enabled = true);
    void text(const char* value);
    void caption(const char* value);

    // Optional: keep the shell clear of the notch / home indicator (points).
    void setSafeInsets(float top, float bottom);

    // Touch hit-testing helpers. Rects are (x, y, width, height) in screen points.
    bool   popupOpen();
    ImVec4 buttonRect();
    ImVec4 shellRect();
}
