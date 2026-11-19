<<<<<<< HEAD
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
=======
// Menu source watermark: @use_sberbank
// iOS port of the donor menu shell: the Neverlose-style sidebar, header and
// fading window. Tab contents are supplied by the host (main.mm), so the menu
// only owns layout, input and the shared styling.
#pragma once

#include "imgui.h"
#include "Menu/style.h"

namespace nl {

// Menu Scale choices shown in the header dropdown and the Settings tab.
inline constexpr const char* scale_names[] = { "Auto", "75%", "100%", "125%", "150%", "175%", "200%", "250%" };
inline constexpr float scale_values[] = { 0.0f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f };

class Menu {
public:
    // Draws the active tab between the sidebar and the window edge. `tab` is the
    // index of the sidebar entry (0 Aim, 1 Rage, 2 Visuals, 3 Misc, 4 Settings).
    void (*host_content)(Menu&, const ImVec2& origin, int tab, int subtab) = nullptr;

    // Call after ImGui::CreateContext() and nl::load_fonts().
    void init(const ImVec2& pos, float scale = 1.0f);
    // Call between ImGui::NewFrame() and ImGui::Render().
    void render();

    void set_visible(bool visible);
    void toggle_visible();
    bool visible() const { return visible_; }
    // 0..1 fade of the fading window; the menu stays on screen until it reaches 0.
    float fade() const { return visibility_; }

    void set_tab(int tab, int subtab = 0);
    int active_tab() const { return tab_; }

    void set_scale(float scale);
    float scale() const;

    // Top-left corner and size of the last drawn frame, in display points.
    ImVec2 pos() const { return pos_; }
    ImVec2 size() const;

    // Public so the Settings tab can edit them with regular Panel rows.
    bool auto_scale = true;
    int scale_choice = 0; // 0 = Auto, otherwise an index into scale_values()
    ImVec4 accent = ImGui::ColorConvertU32ToFloat4(style::col::accent_default);

    static constexpr float min_scale = 0.45f;
    static constexpr float max_scale = 3.0f;

private:
    void update_scale();
    void draw_sidebar(ImDrawList* dl, const ImVec2& o);
    void draw_header(ImDrawList* dl, const ImVec2& o);
    void draw_content(const ImVec2& o);
    void draw_style_popup();

    int tab_ = 2;
    int subtab_ = 0;
    float tab_switch_ = 1.0f;
    float tab_marker_y_ = -1.0f;
    float visibility_ = 0.0f;
    bool visible_ = false;
    ImVec2 pos_{};
    ImVec2 style_anchor_min_{};
    ImVec2 style_anchor_max_{};
    bool open_style_ = false;
    int last_scale_choice_ = 0;
    float pending_scale_ = 0.0f;
    float default_scale_ = 1.0f;
};

} // namespace nl
>>>>>>> 1324a15d57503dc315a2e04edf9268260ad9ac15
