// Menu source watermark: @use_sberbank
#include "Menu/menu.h"

#include <algorithm>
#include <cmath>

#include "imgui_internal.h"
#include "Menu/blur.h"
#include "Menu/draw.h"
#include "Menu/fonts.h"
#include "Menu/style.h"
#include "Menu/widgets.h"

namespace nl {
namespace {

using namespace nl::style;
using draw::snap;

constexpr const char* k_tab_names[] = { "Aim", "Rage", "Visuals", "Misc", "Settings" };
constexpr const char* k_tab_icons[] = { icon::crosshairs, icon::mouse, icon::image, nullptr, icon::gear };
constexpr int k_tab_count = 5;

// Menu coordinates (unscaled, relative to the top-left corner `o`) to whole screen pixels.
ImVec2 at(const ImVec2& o, float x, float y) { return ImVec2(o.x + snap(px(x)), o.y + snap(px(y))); }

ImVec4 to_vec4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

float text_cap_y_centered(ImFont* font, float size, float center_y)
{
    return center_y - draw::cap_height(font, size) * 0.5f;
}

void draw_style_popup_rows(Menu& menu)
{
    ui::popup::group([&] {
        ui::popup::color("Accent", &menu.accent, true, icon::fill_drip, icon::gear);
        ui::popup::value("Menu Scale", &menu.scale_choice, scale_names, true, icon::scale);
        ui::popup::toggle("Auto Scale", &menu.auto_scale, icon::expand);
    });
    ui::popup::group([&] {
        if (ui::popup::item("Close menu", icon::xmark))
            menu.set_visible(false);
    });
}

} // namespace

void apply_imgui_style()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(0.0f, 0.0f);
    s.WindowBorderSize = 0.0f;
    s.WindowRounding = layout::rounding;
    s.PopupRounding = 6.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameRounding = 4.0f;
    s.FrameBorderSize = 0.0f;
    s.GrabRounding = 4.0f;
    s.ItemSpacing = ImVec2(0.0f, 0.0f);
    s.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    s.ScaleAllSizes(ui_scale);
    s.FontScaleDpi = ui_scale;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = to_vec4(col::label);
    c[ImGuiCol_TextDisabled] = to_vec4(col::section_header);
    c[ImGuiCol_WindowBg] = to_vec4(col::main_bg);
    c[ImGuiCol_PopupBg] = to_vec4(col::popup_bg);
    c[ImGuiCol_Border] = to_vec4(col::popup_border);
    c[ImGuiCol_FrameBg] = to_vec4(col::combo_bg);
    c[ImGuiCol_FrameBgHovered] = to_vec4(col::combo_bg_hovered);
    c[ImGuiCol_FrameBgActive] = to_vec4(col::combo_bg_hovered);
    c[ImGuiCol_Button] = to_vec4(col::combo_bg);
    c[ImGuiCol_ButtonHovered] = to_vec4(col::combo_bg_hovered);
    c[ImGuiCol_ButtonActive] = to_vec4(col::popup_item_hover);
    c[ImGuiCol_CheckMark] = to_vec4(col::accent);
    c[ImGuiCol_SliderGrab] = to_vec4(col::accent);
    c[ImGuiCol_SliderGrabActive] = to_vec4(col::toggle_on_knob);
    c[ImGuiCol_Header] = to_vec4(col::popup_item_hover);
    c[ImGuiCol_HeaderHovered] = to_vec4(col::popup_item_hover);
    c[ImGuiCol_HeaderActive] = to_vec4(col::tab_bg_active);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.70f, 0.84f, 1.0f, 0.30f);
    c[ImGuiCol_NavCursor] = to_vec4(col::accent);
}

void Menu::init(const ImVec2& pos, float scale)
{
    (void)pos; // the menu keeps itself centered on the display
    default_scale_ = ImClamp(scale, min_scale, max_scale);
    set_scale(default_scale_);
    last_scale_choice_ = scale_choice;
}

ImVec2 Menu::size() const
{
    return ImVec2(snap(layout::width * ui_scale), snap(layout::height * ui_scale));
}

void Menu::set_visible(bool visible)
{
    visible_ = visible;
    if (!visible)
        visibility_ = 0.0f;
}

void Menu::toggle_visible() { set_visible(!visible_); }

void Menu::set_tab(int tab, int subtab)
{
    if (tab == tab_ && subtab == subtab_)
        return;
    tab_ = ImClamp(tab, 0, k_tab_count - 1);
    subtab_ = subtab;
    tab_switch_ = 0.0f;
}

void Menu::set_scale(float scale)
{
    ui_scale = ImClamp(scale, min_scale, max_scale);
    apply_imgui_style();
}

float Menu::scale() const { return ui_scale; }

void Menu::update_scale()
{
    if (scale_choice != last_scale_choice_) {
        last_scale_choice_ = scale_choice;
        if (scale_choice <= 0) {
            auto_scale = true;
        } else {
            auto_scale = false;
            pending_scale_ = scale_values[ImClamp(scale_choice, 0, IM_ARRAYSIZE(scale_values) - 1)];
        }
    }
    if (pending_scale_ > 0.0f) {
        set_scale(pending_scale_);
        pending_scale_ = 0.0f;
    }
    if (!auto_scale)
        return;

    // The reference layout is 633x485; fit it into the phone screen with a margin.
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (display.x <= 1.0f || display.y <= 1.0f)
        return;
    float fit = ImMin((display.x - px(32.0f)) / layout::width, (display.y - px(32.0f)) / layout::height);
    fit = ImClamp(fit, min_scale, 1.6f);
    if (std::fabs(fit - ui_scale) > 0.004f)
        set_scale(fit);
}

void Menu::render()
{
    if (!visible_ && visibility_ <= 0.0f)
        return;

    const ImGuiIO& io = ImGui::GetIO();
    const float dt = ImMax(io.DeltaTime, 0.0f);
    col::accent = ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 1.0f));
    update_scale();

    visibility_ = ImClamp(visibility_ + (visible_ ? dt : -dt) * 7.0f, 0.0f, 1.0f);
    if (visibility_ <= 0.0f)
        return;
    if (!visible_)
        ImGui::ClosePopupsExceptModals();

    const ImVec2 sz = size();
    const ImVec2 display = io.DisplaySize;
    pos_.x = std::floor((display.x - sz.x) * 0.5f);
    pos_.y = std::floor((display.y - sz.y) * 0.5f);
    if (!visible_) {
        // Keep the fading window on the last valid position when the display shrank.
        pos_.x = ImMax(pos_.x, 0.0f);
        pos_.y = ImMax(pos_.y, 0.0f);
    }

    ImGui::SetNextWindowPos(pos_);
    ImGui::SetNextWindowSize(sz);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(layout::rounding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav;
    ImGui::Begin("##synapse", nullptr, flags);
    ImGui::PopStyleVar(4);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;
    const ImVec2 o(std::floor(window->Pos.x), std::floor(window->Pos.y));
    pos_ = o;
    const ImVec2 max(o.x + sz.x, o.y + sz.y);
    const int vtx_begin = dl->VtxBuffer.Size;

    const ImVec2 main_min = at(o, layout::main_x, 0.0f);
    const ImVec2 side_max(main_min.x + px(layout::main_rounding), max.y);
    const float side_r = px(layout::rounding);
    const float panel_a = blur::available() ? 0.52f : 0.68f;

    dl->PushClipRectFullScreen();
    draw::soft_shadow(dl, o, max, side_r, px(layout::shadow_size), col::shadow);
    dl->PopClipRect();
    blur::image(dl, o, side_max, side_r, ImDrawFlags_RoundCornersLeft);
    draw::rect_gradient_v(dl, o, side_max, draw::with_alpha(rgb(18, 24, 38), panel_a),
                          draw::with_alpha(rgb(12, 16, 26), panel_a + 0.06f), side_r, ImDrawFlags_RoundCornersLeft);
    draw::rect_gradient_v(dl, o, side_max, rgb(215, 228, 255, 28), rgb(160, 180, 220, 12), side_r,
                          ImDrawFlags_RoundCornersLeft);
    draw::border(dl, o, side_max, rgb(255, 255, 255, 28), side_r, draw::hairline());

    dl->AddRectFilled(main_min, max, col::main_bg, px(layout::main_rounding));
    draw::border(dl, main_min, max, col::main_border, px(layout::main_rounding), draw::hairline());

    draw_sidebar(dl, o);
    draw_header(dl, o);
    draw_content(o);
    draw_style_popup();

    if (visibility_ < 1.0f) {
        const float e = visibility_ * (2.0f - visibility_);
        for (int i = vtx_begin; i < dl->VtxBuffer.Size; ++i) {
            ImDrawVert& v = dl->VtxBuffer[i];
            v.col = draw::with_alpha(v.col, e);
            v.pos.y += (1.0f - e) * px(8.0f);
        }
    }
    ImGui::End();
}

void Menu::draw_sidebar(ImDrawList* dl, const ImVec2& o)
{
    const Fonts& f = fonts();

    // Logo and product name
    dl->AddRectFilled(at(o, layout::logo_x, layout::logo_y),
                      at(o, layout::logo_x + layout::logo_w, layout::logo_y + layout::logo_h), col::logo_bg,
                      px(layout::logo_rounding));
    const float logo_w = draw::text_width(f.black, font_size::logo, "S");
    draw::text_glow(dl, f.black, font_size::logo,
                    ImVec2(o.x + px(layout::logo_text_cx) - logo_w * 0.5f, o.y + px(layout::logo_text_cap_y)),
                    col::logo_text, col::logo_glow, px(layout::logo_glow_radius), "S");
    draw::text(dl, f.semibold, font_size::title, at(o, layout::title_x, layout::title_cap_y), col::title, "Synapse");
    draw::text(dl, f.medium, font_size::subtitle, at(o, layout::subtitle_x, layout::subtitle_cap_y), col::subtitle,
               "Standoff 2");

    const float sep_x0 = o.x + px(layout::sidebar_sep_x0), sep_x1 = o.x + px(layout::sidebar_sep_x1);
    draw::hline(dl, sep_x0, sep_x1, o.y + px(layout::sidebar_sep_top_y), col::sidebar_separator);
    draw::hline(dl, sep_x0, sep_x1, o.y + px(layout::sidebar_sep_bottom_y), col::sidebar_separator);

    draw::text(dl, f.medium, font_size::sidebar_header, at(o, layout::sidebar_header_x, layout::aimbot_header_cap_y),
               col::sidebar_header, "AIMBOT");
    draw::text(dl, f.medium, font_size::sidebar_header, at(o, layout::sidebar_header_x, layout::common_header_cap_y),
               col::sidebar_header, "COMMON");

    const float dt = ImGui::GetIO().DeltaTime;

    // One highlight slides between the tabs instead of each tab fading its own.
    {
        const float target_y = layout::tab_top_y[tab_];
        if (tab_marker_y_ < 0.0f)
            tab_marker_y_ = target_y;
        tab_marker_y_ = ImLerp(tab_marker_y_, target_y, 1.0f - std::exp(-16.0f * dt));
        if (std::fabs(tab_marker_y_ - target_y) < 0.01f)
            tab_marker_y_ = target_y;
        // Stretches a little while moving, like it is being pulled.
        const float stretch = ImMin(std::fabs(target_y - tab_marker_y_) * 0.25f, 6.0f);
        const float y0 = target_y < tab_marker_y_ ? tab_marker_y_ - stretch : tab_marker_y_;
        const float y1 = target_y > tab_marker_y_ ? tab_marker_y_ + layout::tab_h + stretch
                                                  : tab_marker_y_ + layout::tab_h;
        const ImVec2 mmin = at(o, layout::tab_x0, y0), mmax = at(o, layout::tab_x1, y1);
        dl->AddRectFilled(mmin, mmax, rgb(210, 225, 255, 36), px(layout::tab_rounding));
        draw::border(dl, mmin, mmax, rgb(255, 255, 255, 30), px(layout::tab_rounding), draw::hairline());
    }

    for (int t = 0; t < k_tab_count; ++t) {
        const bool selected = tab_ == t;
        const ImVec2 min = at(o, layout::tab_x0, layout::tab_top_y[t]);
        const ImVec2 max = at(o, layout::tab_x1, layout::tab_top_y[t] + layout::tab_h);
        ImGui::PushID(t);
        const ImGuiID id = ImGui::GetID("##tab");
        const ui::ItemState s = ui::item_behavior(id, min, max);
        if (s.pressed)
            set_tab(t);
        const float a = ui::animate(id + 1, selected ? 1.0f : 0.0f, 20.0f);
        const float h = ui::animate(id + 2, s.hovered ? 1.0f : 0.0f, 16.0f) * (1.0f - a);
        ImGui::PopID();

        if (h > 0.0f)
            dl->AddRectFilled(min, max, draw::with_alpha(col::tab_bg_hovered, h), px(layout::tab_rounding));

        const float cy = (min.y + max.y) * 0.5f;
        const ImU32 icon_col =
            draw::lerp_color(draw::lerp_color(col::tab_icon, col::tab_text_hovered, h), col::tab_icon_active, a);
        const ImU32 text_col =
            draw::lerp_color(draw::lerp_color(col::tab_text, col::tab_text_hovered, h), col::tab_text_active, a);
        if (k_tab_icons[t])
            draw::icon(dl, f.icons, font_size::icon, ImVec2(o.x + px(layout::tab_icon_cx), cy + px(layout::tab_icon_dy)),
                       icon_col, k_tab_icons[t]);
        else
            draw::icon_swap_square(dl, ImVec2(o.x + px(layout::misc_icon_cx), cy), icon_col,
                                   draw::lerp_color(col::sidebar_icon_hole, col::tab_icon_cutout, a));

        draw::text(dl, f.medium, font_size::tab,
                   ImVec2(snap(o.x + px(layout::tab_text_x + 2.0f * a + 1.0f * h)),
                          text_cap_y_centered(f.medium, font_size::tab, cy) + px(layout::tab_text_dy)),
                   text_col, k_tab_names[t]);
    }

    // Profile
    const ImVec2 profile_min = at(o, layout::tab_x0, layout::sidebar_sep_bottom_y + 6.0f);
    const ImVec2 profile_max = at(o, layout::tab_x1, layout::height - 4.0f);
    const ui::ItemState profile = ui::item_behavior(ImGui::GetID("##profile"), profile_min, profile_max);
    if (profile.pressed) {
        style_anchor_min_ = profile_min;
        style_anchor_max_ = profile_max;
        open_style_ = true;
    }
    const float ph = ui::animate(ImGui::GetID("##profile_h"), profile.hovered ? 1.0f : 0.0f, 16.0f);
    if (ph > 0.0f)
        dl->AddRectFilled(profile_min, profile_max, draw::with_alpha(col::tab_bg_hovered, ph), px(layout::tab_rounding));

    {
        const ImVec2 c(o.x + px(layout::avatar_cx), o.y + px(layout::avatar_cy));
        const float r = px(layout::avatar_r);
        dl->AddCircleFilled(c, r, col::logo_bg, 40);
        dl->AddCircle(c, r, draw::with_alpha(IM_COL32_WHITE, 26), 40, draw::hairline());
        const float w = draw::text_width(f.black, font_size::title, "S");
        draw::text(dl, f.black, font_size::title,
                   ImVec2(c.x - w * 0.5f, c.y - draw::cap_height(f.black, font_size::title) * 0.5f), col::logo_text, "S");
    }
    draw::text(dl, f.medium, font_size::tab, at(o, layout::username_x, layout::username_cap_y), col::username, "Synapse");
    draw::text(dl, f.medium, font_size::small, at(o, layout::days_x, layout::days_cap_y), col::subscription,
               "iOS build");
    draw::chevron_right(dl, ImVec2(o.x + px(layout::profile_chevron_cx), o.y + px(layout::profile_chevron_cy)),
                        px(1.8f), px(3.6f), draw::lerp_color(col::profile_chevron, col::tab_text_active, ph), px(1.6f));
}

void Menu::draw_header(ImDrawList* dl, const ImVec2& o)
{
    const Fonts& f = fonts();

    // Style group: the accent swatch and the scale dropdown.
    const char* label = "Style";
    const float text_w = draw::text_width(f.medium, font_size::text, label);
    const float chevron_x = o.x + px(layout::preset_text_x + layout::preset_chevron_gap) + text_w;
    const ImVec2 gmin = at(o, layout::group_x, layout::group_y);
    const ImVec2 gmax(snap(chevron_x + px(layout::preset_right_pad)), at(o, 0.0f, layout::group_y + layout::group_h).y);
    const float gcy = (gmin.y + gmax.y) * 0.5f;
    const float group_rounding = px(layout::group_rounding);
    const ImGuiID group_id = ImGui::GetID("##style_group");
    const ui::ItemState gs = ui::item_behavior(group_id, gmin, gmax);
    const bool open = ImGui::IsPopupOpen(ImGui::GetID("##style_popup"), ImGuiPopupFlags_None);
    if (gs.pressed && !open) {
        style_anchor_min_ = gmin;
        style_anchor_max_ = gmax;
        open_style_ = true;
    }
    const float gh = ui::animate(group_id + 1, (gs.hovered || open) ? 1.0f : 0.0f, 16.0f);
    dl->AddRectFilled(gmin, gmax, draw::lerp_color(col::group_bg, col::group_hovered, gh), group_rounding);

    // Accent swatch in the save button slot.
    {
        const float size = snap(px(12.0f));
        const ImVec2 swmin(snap(o.x + px(layout::save_icon_cx) - size * 0.5f), snap(gcy - size * 0.5f));
        const ImVec2 swmax(swmin.x + size, swmin.y + size);
        ImVec4 opaque(accent.x, accent.y, accent.z, 1.0f);
        dl->AddRectFilled(ImVec2(swmin.x - 1.0f, swmin.y - 1.0f), ImVec2(swmax.x + 1.0f, swmax.y + 1.0f),
                          col::toggle_outline, px(4.5f));
        dl->AddRectFilled(swmin, swmax, ImGui::ColorConvertFloat4ToU32(opaque), px(3.5f));
    }
    draw::text(dl, f.medium, font_size::text, at(o, layout::preset_text_x, layout::preset_cap_y),
               draw::lerp_color(col::preset_text, col::tab_text_active, gh * 0.6f), label);

    const ImU32 chevron_col = draw::lerp_color(col::preset_chevron, col::tab_text_active, gh * 0.6f);
    draw::chevron_down(dl, ImVec2(chevron_x, gcy + px(layout::preset_chevron_dy)), px(3.0f), px(1.5f), chevron_col,
                       px(1.4f));

    const float line = draw::hairline();
    draw::vline(dl, o.x + px(layout::group_sep_x) - line * 0.5f, gmin.y + line, gmax.y - line, col::group_border);
    draw::border(dl, gmin, gmax, draw::lerp_color(col::group_border, col::accent, gh * 0.35f), group_rounding, line);

    // Close button in the search slot.
    const ImVec2 sc(o.x + px(layout::search_cx), o.y + px(layout::search_cy));
    const float hit = px(10.0f);
    const ImGuiID close_id = ImGui::GetID("##close");
    const ui::ItemState cs =
        ui::item_behavior(close_id, ImVec2(sc.x - hit, sc.y - hit), ImVec2(sc.x + hit, sc.y + hit));
    if (cs.pressed)
        set_visible(false);
    const float ch = ui::animate(close_id + 1, cs.hovered ? 1.0f : 0.0f, 16.0f);
    if (ch > 0.0f)
        dl->AddCircleFilled(sc, px(10.0f) * (0.7f + 0.3f * ch), draw::with_alpha(col::group_hovered, ch), 24);
    draw::icon(dl, f.icons, font_size::icon * (1.0f + 0.08f * ch), sc,
               draw::lerp_color(col::search_icon, col::tab_text_active, ch), icon::xmark);

    draw::hline(dl, o.x + px(layout::header_sep_x0), o.x + px(layout::header_sep_x1), o.y + px(layout::header_sep_y),
                col::header_separator);
}

void Menu::draw_content(const ImVec2& o)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    tab_switch_ = ImMin(1.0f, tab_switch_ + ImGui::GetIO().DeltaTime * 6.0f);
    const int v0 = dl->VtxBuffer.Size;

    ImGui::PushID(tab_);
    if (host_content)
        host_content(*this, o, tab_, subtab_);
    ImGui::PopID();

    if (tab_switch_ < 1.0f) {
        const float e = tab_switch_ * (2.0f - tab_switch_);
        for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
            ImDrawVert& v = dl->VtxBuffer[i];
            v.col = draw::with_alpha(v.col, e);
            v.pos.y += (1.0f - e) * px(6.0f);
        }
    }
}

void Menu::draw_style_popup()
{
    const ImGuiID id = ImGui::GetID("##style_popup");
    if (open_style_) {
        ImGui::OpenPopup(id);
        open_style_ = false;
    }
    if (!ui::begin_popup_id(id, style_anchor_min_, style_anchor_max_, px(214.0f)))
        return;
    draw_style_popup_rows(*this);
    ui::end_popup();
}

} // namespace nl
