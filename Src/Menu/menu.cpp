// Neverlose-style menu adapted to iOS.
//
// The original Windows menu used a fixed 750x756 canvas, Direct3D blur and
// textures. This port keeps the exact visual language (dark glass shell,
// sidebar, cards, animated toggles, select popups, color pickers) but uses a
// responsive point-based layout, touch-friendly hit areas and no GPU blur.
// All rows are bound to the real Synapse feature variables.

#define IMGUI_DEFINE_MATH_OPERATORS
#include "menu.h"
#include "menu_fonts.h"
#include "menu_icons.h"

#include "imgui.h"
#include "imgui_internal.h"

#include "Features/Aim/aim.h"
#include "Features/Rage/rage.h"
#include "Features/Visuals/visuals.h"
#include "Settings/settings.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    // ---------------------------------------------------------------------
    // Theme
    // ---------------------------------------------------------------------
    constexpr ImU32 C(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

    constexpr float kBody = 14.0f;
    constexpr float kControl = 13.0f;
    constexpr float kSmall = 11.0f;
    constexpr float kCaption = 9.5f;
    constexpr float kTitle = 15.0f;
    constexpr float kIcon = 12.5f;

    constexpr float kSidebarW = 150.0f;
    constexpr float kToolbarH = 48.0f;
    constexpr float kGap = 10.0f;
    constexpr float kTitleH = 16.0f;

    constexpr ImGuiColorEditFlags kColorFlags =
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel |
        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop |
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_PickerHueBar;

    enum class Ctl { Toggle, Select, Slider, Color, Text };

    struct Row
    {
        const char* label = "";
        Ctl type = Ctl::Toggle;
        bool* toggle = nullptr;
        float* slider = nullptr;
        float lo = 0.0f;
        float hi = 1.0f;
        const char* fmt = "%.0f";
        int* select = nullptr;
        const char* const* items = nullptr;
        int count = 0;
        float* color = nullptr;
        const char* text = nullptr;
    };

    struct Card
    {
        const char* title = "";
        const Row* rows = nullptr;
        int count = 0;
    };

    enum { PageRage = 0, PageAim = 1, PageVisuals = 2, PageSettings = 3 };

    struct MenuState
    {
        int page = PageRage;
        int visuals_tab = 0;
        float page_mix = 1.0f;
        float scroll = 0.0f;

        bool popup_open = false;
        int* popup_target = nullptr;
        const char* const* popup_items = nullptr;
        int popup_count = 0;
        ImVec2 popup_anchor{};
        float popup_width = 120.0f;
        int popup_opened_frame = 0;

        float open_mix = 0.0f;
        bool scroll_drag = false;
    } state;

    // ---------------------------------------------------------------------
    // Small helpers
    // ---------------------------------------------------------------------
    ImFont* StrongFont()
    {
        ImFont* font = menu_fonts::strong();
        return font != nullptr ? font : ImGui::GetFont();
    }

    float Motion(ImGuiID id, float target, float speed = 16.0f, float initial = -1.0f)
    {
        float* value = ImGui::GetStateStorage()->GetFloatRef(id, initial < 0.0f ? target : initial);
        *value = ImLerp(*value, target, 1.0f - std::exp(-speed * ImGui::GetIO().DeltaTime));
        if (std::fabs(*value - target) < 0.0005f)
            *value = target;
        return *value;
    }

    ImU32 Mix(ImU32 a, ImU32 b, float t)
    {
        return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(a),
                                                     ImGui::ColorConvertU32ToFloat4(b), ImSaturate(t)));
    }

    void Text(ImDrawList* d, ImVec2 p, ImU32 col, const char* value, float size = kBody, ImFont* font = nullptr)
    {
        d->AddText(font != nullptr ? font : ImGui::GetFont(), size, p, col, value);
    }

    void TextY(ImDrawList* d, float x, float y, float h, ImU32 col, const char* value,
               float size = kBody, ImFont* font = nullptr)
    {
        ImFont* f = font != nullptr ? font : ImGui::GetFont();
        const float th = f->CalcTextSizeA(size, FLT_MAX, 0.0f, value).y;
        Text(d, ImVec2(x, y + std::floor((h - th) * 0.5f)), col, value, size, f);
    }

    void TextClamp(ImDrawList* d, ImVec2 p, float size, float max_w, ImU32 col, const char* value)
    {
        ImFont* f = ImGui::GetFont();
        if (max_w <= 4.0f)
            return;
        if (f->CalcTextSizeA(size, FLT_MAX, 0.0f, value).x <= max_w)
        {
            Text(d, p, col, value, size, f);
            return;
        }

        char buffer[160];
        size_t length = ImStrlen(value);
        if (length >= sizeof(buffer))
            length = sizeof(buffer) - 1;
        memcpy(buffer, value, length);
        buffer[length] = 0;
        const float dots = f->CalcTextSizeA(size, FLT_MAX, 0.0f, "...").x;
        while (length > 3 && f->CalcTextSizeA(size, FLT_MAX, 0.0f, buffer).x + dots > max_w)
            buffer[--length] = 0;
        if (length + 3 < sizeof(buffer))
        {
            buffer[length++] = '.';
            buffer[length++] = '.';
            buffer[length++] = '.';
            buffer[length] = 0;
        }
        Text(d, p, col, buffer, size, f);
    }

    bool Hit(const char* id, ImVec2 p, ImVec2 size)
    {
        ImGui::SetCursorScreenPos(p);
        return ImGui::InvisibleButton(id, size);
    }

    void Chevron(ImDrawList* d, ImVec2 p, ImU32 color)
    {
        d->AddLine(p, p + ImVec2(3, 3), color, 1.3f);
        d->AddLine(p + ImVec2(3, 3), p + ImVec2(0, 6), color, 1.3f);
    }

    void Check(ImDrawList* d, ImVec2 p, ImU32 color)
    {
        d->AddLine(p, p + ImVec2(3, 3), color, 1.5f);
        d->AddLine(p + ImVec2(3, 3), p + ImVec2(8, -3), color, 1.5f);
    }

    void CloseCross(ImDrawList* d, ImVec2 c, ImU32 color)
    {
        d->AddLine(c + ImVec2(-4, -4), c + ImVec2(4, 4), color, 1.5f);
        d->AddLine(c + ImVec2(4, -4), c + ImVec2(-4, 4), color, 1.5f);
    }

    void FadeVertices(ImDrawList* d, int first, float alpha_mul)
    {
        for (int i = first; i < d->VtxBuffer.Size; ++i)
        {
            ImDrawVert& v = d->VtxBuffer[i];
            const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
            v.col = (v.col & 0x00ffffffu) | ((ImU32)(a * alpha_mul) << IM_COL32_A_SHIFT);
        }
    }

    // ---------------------------------------------------------------------
    // Row data (bound to the actual cheat settings)
    // ---------------------------------------------------------------------
    Row ToggleRow(const char* label, bool* value)
    {
        Row r;
        r.label = label;
        r.type = Ctl::Toggle;
        r.toggle = value;
        return r;
    }

    Row SelectRow(const char* label, int* value, const char* const* items, int count)
    {
        Row r;
        r.label = label;
        r.type = Ctl::Select;
        r.select = value;
        r.items = items;
        r.count = count;
        return r;
    }

    Row SliderRow(const char* label, float* value, float lo, float hi, const char* fmt)
    {
        Row r;
        r.label = label;
        r.type = Ctl::Slider;
        r.slider = value;
        r.lo = lo;
        r.hi = hi;
        r.fmt = fmt;
        return r;
    }

    Row ColorRow(const char* label, float* color)
    {
        Row r;
        r.label = label;
        r.type = Ctl::Color;
        r.color = color;
        return r;
    }

    Row TextRow(const char* label, const char* value)
    {
        Row r;
        r.label = label;
        r.type = Ctl::Text;
        r.text = value;
        return r;
    }

    // RAGE -----------------------------------------------------------------
    const Row kRagePlayer[] = {
        ToggleRow("Move Before Timer", &rage::MBT),
        ToggleRow("No Clip", &rage::flight),
        SliderRow("Speed", &rage::flight_speed, 1.0f, 20.0f, "%.1f"),
        ToggleRow("No Fall Damage", &rage::no_fall_damage),
        ToggleRow("Wallbang", &rage::wallbang),
    };
    const Row kRageWeapon[] = {
        ToggleRow("Infinity Ammo", &rage::infinity_ammo),
        ToggleRow("Rapid Fire", &rage::rapid_fire),
        ToggleRow("No Recoil", &rage::no_recoil),
        ToggleRow("No Spread", &rage::no_spread),
    };
    const Row kRageBomb[] = {
        ToggleRow("Fast Defuse", &rage::fast_defuse),
        ToggleRow("Auto Win", &rage::auto_win),
    };
    const Card kRageCards[] = {
        { "PLAYER", kRagePlayer, IM_ARRAYSIZE(kRagePlayer) },
        { "WEAPON", kRageWeapon, IM_ARRAYSIZE(kRageWeapon) },
        { "BOMB", kRageBomb, IM_ARRAYSIZE(kRageBomb) },
    };

    // AIM ------------------------------------------------------------------
    const Row kAimMain[] = {
        ToggleRow("Aimbot", &aim::aimbot),
        ToggleRow("Silent", &aim::silent),
        ToggleRow("Automatic Fire", &aim::automatic_fire),
        SliderRow("FOV", &aim::fov_radius, 15.0f, 180.0f, "%.0f"),
        SliderRow("Smooth", &aim::aimbot_smoothing, 0.0f, 100.0f, "%.0f%%"),
        SelectRow("Hitbox", &aim::current_aimbot_hitbox, aim::aimbot_hitboxes, IM_ARRAYSIZE(aim::aimbot_hitboxes)),
        ToggleRow("RCS", &aim::RCS),
    };
    const Row kAimExtra[] = {
        ToggleRow("Show FOV", &aim::draw_fov),
        ColorRow("FOV Color", aim::fov_color),
        ToggleRow("Visualize Bullets", &aim::aim_visualize_bullet),
    };
    const Row kAimAnti[] = {
        ToggleRow("Enabled", &aim::anti_aim),
        SliderRow("Pitch", &aim::anti_pitch, -90.0f, 180.0f, "%.0f"),
        SliderRow("Yaw", &aim::anti_yaw, -180.0f, 180.0f, "%.0f"),
    };
    const Card kAimCards[] = {
        { "AIMBOT", kAimMain, IM_ARRAYSIZE(kAimMain) },
        { "FOV & OTHER", kAimExtra, IM_ARRAYSIZE(kAimExtra) },
        { "ANTI-AIM", kAimAnti, IM_ARRAYSIZE(kAimAnti) },
    };

    // VISUALS --------------------------------------------------------------
    const Row kVisualPlayerA[] = {
        ToggleRow("Enabled", &visuals::esp_enabled),
        ToggleRow("Box", &visuals::box2D),
        ToggleRow("Filled", &visuals::boxFill),
        SelectRow("Box Type", &visuals::current_box_type, visuals::box_type, IM_ARRAYSIZE(visuals::box_type)),
        ToggleRow("Skeleton", &visuals::skeleton),
        ToggleRow("Health Bar", &visuals::healthBar),
    };
    const Row kVisualPlayerB[] = {
        ToggleRow("Health Text", &visuals::healthBarText),
        ToggleRow("Names", &visuals::names),
        ToggleRow("Weapon Text", &visuals::weaponText),
        ToggleRow("Weapon Icon", &visuals::weaponIcon),
        ToggleRow("Distance", &visuals::distance),
        ToggleRow("Tracer", &visuals::tracer),
    };
    const Row kVisualPlayerColors[] = {
        ColorRow("Box Color", visuals::box_color),
        ColorRow("Fill Color", visuals::box_fill_color),
        ColorRow("Skeleton Color", visuals::skeleton_color),
        ColorRow("Health Color", visuals::healthbar_color),
        ColorRow("Name Color", visuals::names_color),
        ColorRow("Weapon Color", visuals::weapon_text_color),
        ColorRow("Distance Color", visuals::distance_color),
        ColorRow("Tracer Color", visuals::tracer_color),
    };
    const Card kVisualPlayers[] = {
        { "PLAYER ESP", kVisualPlayerA, IM_ARRAYSIZE(kVisualPlayerA) },
        { "PLAYER INFO", kVisualPlayerB, IM_ARRAYSIZE(kVisualPlayerB) },
        { "COLORS", kVisualPlayerColors, IM_ARRAYSIZE(kVisualPlayerColors) },
    };

    const Row kVisualBomb[] = {
        ToggleRow("Bomb Timer", &visuals::bomb_timer_bar),
        ColorRow("Timer Color", visuals::bomb_timer_color),
    };
    const Row kVisualGrenade[] = {
        ToggleRow("Trajectory", &visuals::visualize_grenade_trajectory),
        ColorRow("Trajectory Color", visuals::grenade_trajectory_color),
    };
    const Card kVisualWorld[] = {
        { "BOMB", kVisualBomb, IM_ARRAYSIZE(kVisualBomb) },
        { "GRENADE", kVisualGrenade, IM_ARRAYSIZE(kVisualGrenade) },
    };

    // SETTINGS -------------------------------------------------------------
    const Row kSettingsMenu[] = {
        ToggleRow("Watermark", &settings::watermark),
        ToggleRow("Overlay", &settings::overlay),
    };
    const Row kSettingsAppearance[] = {
        SelectRow("Theme", &settings::current_theme, settings::theme_type, IM_ARRAYSIZE(settings::theme_type)),
    };
    const Row kSettingsAbout[] = {
        TextRow("Version", "v0.0.4b"),
        TextRow("Platform", "iOS"),
        TextRow("Author", "wurvt"),
    };
    const Card kSettingsCards[] = {
        { "MENU", kSettingsMenu, IM_ARRAYSIZE(kSettingsMenu) },
        { "APPEARANCE", kSettingsAppearance, IM_ARRAYSIZE(kSettingsAppearance) },
        { "ABOUT", kSettingsAbout, IM_ARRAYSIZE(kSettingsAbout) },
    };

    struct NavItem
    {
        const char* label;
        const char* icon;
        int page;
    };

    const NavItem kNav[] = {
        { "Rage", MENU_ICON_FIRE, PageRage },
        { "Aim", MENU_ICON_CROSSHAIRS, PageAim },
        { "Visuals", MENU_ICON_EYE, PageVisuals },
        { "Settings", MENU_ICON_SLIDERS, PageSettings },
    };

    // ---------------------------------------------------------------------
    // Controls
    // ---------------------------------------------------------------------
    void OpenSelect(int* target, const char* const* items, int count, ImVec2 anchor, float width)
    {
        const bool same = state.popup_open && state.popup_target == target;
        state.popup_open = !same;
        state.popup_target = target;
        state.popup_items = items;
        state.popup_count = count;
        state.popup_anchor = anchor;
        state.popup_width = width;
        if (!same)
            state.popup_opened_frame = ImGui::GetFrameCount();
    }

    void ToggleControl(ImDrawList* d, float right, float y, float h, bool* value, bool interactive)
    {
        const ImVec2 p(right, y + (h - 18.0f) * 0.5f);
        const ImGuiID id = ImGui::GetID("##tg");
        float response = 0.0f;
        if (interactive)
        {
            if (Hit("##tg", ImVec2(p.x - 6.0f, p.y - 7.0f), ImVec2(41.0f, 32.0f)))
                *value = !*value;
            response = ImGui::IsItemHovered() ? 1.0f : 0.0f;
        }

        const float on = Motion(id ^ 0x55aa721u, *value ? 1.0f : 0.0f, 19.0f);
        const float hover = Motion(id ^ 0x118a0u, response, 20.0f);

        d->AddRectFilled(p - ImVec2(1.0f + hover, 1.0f + hover), p + ImVec2(30.0f + hover, 19.0f + hover),
                         C(75, 126, 255, (int)(45 * hover)), 10.0f);
        d->AddRectFilled(p, p + ImVec2(29.0f, 18.0f), Mix(C(29, 33, 43), C(75, 126, 255), on), 9.0f);
        d->AddCircleFilled(p + ImVec2(ImLerp(9.0f, 20.0f, on), 9.0f), 7.0f,
                           Mix(C(133, 144, 156), C(248, 249, 252), on));
    }

    void SelectControl(ImDrawList* d, const Row& item, ImVec2 card, float width, float row_h, float y, bool interactive)
    {
        const float cw = ImMin(112.0f, width * 0.46f);
        const float ch = 24.0f;
        const ImVec2 cp(card.x + width - cw - 12.0f, y + (row_h - ch) * 0.5f);
        const ImGuiID id = ImGui::GetID("##select");
        float response = 0.0f;

        if (interactive)
        {
            const bool click = Hit("##select", ImVec2(cp.x, y), ImVec2(cw, row_h));
            response = ImGui::IsItemHovered() ? 1.0f : 0.0f;
            if (click)
                OpenSelect(item.select, item.items, item.count, ImVec2(cp.x, cp.y + ch * 0.5f), ImMax(120.0f, cw));
        }
        const float hover = Motion(id ^ 0x6161u, response, 16.0f);

        d->AddRectFilled(cp, cp + ImVec2(cw, ch), Mix(C(25, 28, 38), C(31, 38, 54), hover), 5.0f);
        d->AddRect(cp, cp + ImVec2(cw, ch), Mix(C(32, 35, 46), C(75, 126, 255), hover), 5.0f);

        const char* value = (item.items != nullptr && item.select != nullptr &&
                             *item.select >= 0 && *item.select < item.count)
                                ? item.items[*item.select]
                                : "Select";
        const float value_h = ImGui::GetFont()->CalcTextSizeA(kControl, FLT_MAX, 0.0f, value).y;
        TextClamp(d, ImVec2(cp.x + 7.0f, cp.y + std::floor((ch - value_h) * 0.5f)), kControl, cw - 26.0f,
                  C(170, 173, 184), value);
        d->AddLine(cp + ImVec2(cw - 13.0f, 9.0f), cp + ImVec2(cw - 9.0f, 13.0f), C(139, 143, 154), 1.0f);
        d->AddLine(cp + ImVec2(cw - 9.0f, 13.0f), cp + ImVec2(cw - 5.0f, 9.0f), C(139, 143, 154), 1.0f);
    }

    void SliderControl(ImDrawList* d, const Row& item, ImVec2 card, float width, float row_h, float y, bool interactive)
    {
        const float track_w = ImMin(70.0f, width * 0.34f);
        const float pill_w = 38.0f;
        const float x = card.x + width - 12.0f - pill_w - 8.0f - track_w;
        const float cy = y + row_h * 0.5f;
        const ImGuiID id = ImGui::GetID("##slider");

        if (interactive)
        {
            Hit("##slider", ImVec2(x - 6.0f, cy - 15.0f), ImVec2(track_w + 12.0f, 30.0f));
            if (ImGui::IsItemActive())
            {
                const float t = ImClamp((ImGui::GetIO().MousePos.x - x) / track_w, 0.0f, 1.0f);
                *item.slider = item.lo + t * (item.hi - item.lo);
            }
        }

        float norm = (item.hi > item.lo) ? (*item.slider - item.lo) / (item.hi - item.lo) : 0.0f;
        norm = ImClamp(norm, 0.0f, 1.0f);
        const float shown = Motion(id ^ 0xa8f1u, norm, 14.0f, norm);

        d->AddRectFilled(ImVec2(x, cy - 1.5f), ImVec2(x + track_w, cy + 1.5f), C(34, 38, 48), 2.0f);
        d->AddRectFilled(ImVec2(x, cy - 1.5f), ImVec2(x + track_w * shown, cy + 1.5f), C(75, 126, 255), 2.0f);
        d->AddCircleFilled(ImVec2(x + track_w * shown, cy), 6.0f, C(247, 248, 252));

        const ImVec2 pill(x + track_w + 8.0f, y + (row_h - 20.0f) * 0.5f);
        d->AddRectFilled(pill, pill + ImVec2(pill_w, 20.0f), C(25, 28, 38), 5.0f);

        char buffer[48];
        snprintf(buffer, sizeof(buffer), item.fmt, *item.slider);
        const float tw = ImGui::GetFont()->CalcTextSizeA(kControl, FLT_MAX, 0.0f, buffer).x;
        TextY(d, pill.x + (pill_w - tw) * 0.5f, pill.y, 20.0f, C(166, 169, 179), buffer, kControl);
    }

    void RowControl(ImDrawList* d, ImVec2 card, float width, float row_h, int row, const Row& item, bool interactive)
    {
        const float y = card.y + 3.0f + row * row_h;
        if (row > 0)
            d->AddLine(ImVec2(card.x + 12.0f, y), ImVec2(card.x + width - 12.0f, y), C(28, 31, 40));

        const bool blocked = !interactive;

        // Geometry of the right-hand control (used to clamp the label).
        float control_left = card.x + width - 12.0f - 29.0f;
        float label_size = kBody;
        if (item.type == Ctl::Toggle)
            control_left = card.x + width - 12.0f - 29.0f;
        else if (item.type == Ctl::Select)
            control_left = card.x + width - 12.0f - ImMin(112.0f, width * 0.46f);
        else if (item.type == Ctl::Slider)
            control_left = card.x + width - 12.0f - 38.0f - 8.0f - ImMin(70.0f, width * 0.34f);
        else if (item.type == Ctl::Color)
            control_left = card.x + width - 12.0f - 18.0f;
        else
            control_left = card.x + width * 0.62f;

        const float label_x = card.x + 12.0f;
        const float label_w = ImMax(0.0f, control_left - label_x - 8.0f);
        TextClamp(d, ImVec2(label_x, y + (row_h - kBody) * 0.5f - 1.0f), label_size, label_w,
                  item.type == Ctl::Text ? C(160, 164, 174) : C(207, 209, 218), item.label);

        ImGui::PushID(row);
        switch (item.type)
        {
        case Ctl::Toggle:
            ToggleControl(d, card.x + width - 12.0f - 29.0f, y, row_h, item.toggle, !blocked);
            break;
        case Ctl::Select:
            SelectControl(d, item, card, width, row_h, y, !blocked);
            break;
        case Ctl::Slider:
            SliderControl(d, item, card, width, row_h, y, !blocked);
            break;
        case Ctl::Color:
        {
            const ImVec2 swatch(card.x + width - 12.0f - 18.0f, y + (row_h - 18.0f) * 0.5f);
            if (blocked)
            {
                // Static preview while a select popup owns the input, so the
                // swatch cannot open its picker behind the popup.
                d->AddRectFilled(swatch, swatch + ImVec2(18.0f, 18.0f),
                                 ImGui::ColorConvertFloat4ToU32(
                                     ImVec4(item.color[0], item.color[1], item.color[2], item.color[3])),
                                 3.0f);
                d->AddRect(swatch, swatch + ImVec2(18.0f, 18.0f), C(48, 52, 64), 3.0f);
            }
            else
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
                ImGui::SetCursorScreenPos(swatch);
                ImGui::SetNextItemWidth(18.0f);
                ImGui::ColorEdit4("##color", item.color, kColorFlags);
                ImGui::PopStyleVar();
            }
            break;
        }
        case Ctl::Text:
            if (item.text != nullptr)
            {
                const float tw = ImGui::GetFont()->CalcTextSizeA(kControl, FLT_MAX, 0.0f, item.text).x;
                const float tx = card.x + width - 12.0f - ImMin(tw, width * 0.5f);
                TextClamp(d, ImVec2(tx, y + (row_h - kControl) * 0.5f - 1.0f),
                          kControl, width * 0.5f, C(146, 150, 162), item.text);
            }
            break;
        }
        ImGui::PopID();
    }

    void DrawCard(ImDrawList* d, ImVec2 p, float width, const Card& card, float row_h, bool interactive)
    {
        const float height = card.count * row_h + 6.0f;
        Text(d, p + ImVec2(12.0f, -15.0f), C(89, 94, 106), card.title, kCaption);
        d->AddRectFilled(p, p + ImVec2(width, height), C(17, 19, 27, 240), 13.0f);
        d->AddRect(p, p + ImVec2(width, height), C(31, 34, 44), 13.0f);

        // Row indices restart in every card, so scope the widget IDs by the card's
        // row array to keep toggles/sliders/selects from sharing one ImGui ID.
        ImGui::PushID(card.rows);
        for (int i = 0; i < card.count; ++i)
            RowControl(d, p, width, row_h, i, card.rows[i], interactive);
        ImGui::PopID();
    }

    // ---------------------------------------------------------------------
    // Select popup
    // ---------------------------------------------------------------------
    void PopupLayer(ImDrawList* d, ImVec2 display)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            state.popup_open = false;

        const float open = Motion(ImGui::GetID("##popup_open"), state.popup_open ? 1.0f : 0.0f, 20.0f, 0.0f);
        if (open < 0.002f)
        {
            if (!state.popup_open)
            {
                state.popup_target = nullptr;
                state.popup_items = nullptr;
                state.popup_count = 0;
            }
            return;
        }
        if (state.popup_target == nullptr || state.popup_items == nullptr || state.popup_count <= 0)
            return;

        const float row_h = 30.0f;
        const ImVec2 size(state.popup_width, state.popup_count * row_h + 8.0f);
        ImVec2 p(state.popup_anchor.x, state.popup_anchor.y - size.y * 0.5f);
        p.x = ImClamp(p.x, 10.0f, ImMax(10.0f, display.x - size.x - 10.0f));
        p.y = ImClamp(p.y, 10.0f, ImMax(10.0f, display.y - size.y - 10.0f));

        const int first = d->VtxBuffer.Size;
        d->AddRectFilled(p - ImVec2(5.0f, 2.0f), p + size + ImVec2(5.0f, 8.0f), C(0, 0, 0, 55), 18.0f);
        d->AddRectFilled(p, p + size, C(20, 20, 29, 245), 16.0f);
        d->AddRect(p, p + size, C(57, 61, 76, 205), 16.0f);
        d->AddLine(p + ImVec2(16.0f, 1.0f), p + ImVec2(size.x - 16.0f, 1.0f), C(255, 255, 255, 22));

        const bool accepts = state.popup_open && ImGui::GetFrameCount() > state.popup_opened_frame;
        bool hovered_any = false;
        bool close = false;

        for (int i = 0; i < state.popup_count; ++i)
        {
            const ImVec2 rp = p + ImVec2(4.0f, 4.0f + i * row_h);
            ImGui::PushID(9000 + i);
            const bool clicked = Hit("##popup_row", rp, ImVec2(size.x - 8.0f, row_h));
            hovered_any |= ImGui::IsItemHovered();
            const float hover = Motion(ImGui::GetItemID() ^ 0x9921u, ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
            ImGui::PopID();

            if (hover > 0.001f)
                d->AddRectFilled(rp, rp + ImVec2(size.x - 8.0f, row_h), C(75, 126, 255, (int)(25 * hover)), 10.0f);

            const bool selected = state.popup_target != nullptr && *state.popup_target == i;
            if (selected)
                Check(d, rp + ImVec2(14.0f, row_h * 0.5f), C(230, 233, 241));
            TextY(d, rp.x + 35.0f, rp.y, row_h, selected ? C(224, 229, 243) : C(182, 185, 196),
                  state.popup_items[i], kControl);

            if (accepts && clicked)
            {
                *state.popup_target = i;
                close = true;
            }
        }

        if (state.popup_open && ImGui::IsMouseClicked(0) && !hovered_any &&
            ImGui::GetFrameCount() > state.popup_opened_frame)
            close = true;
        if (close)
            state.popup_open = false;

        const float eased = 1.0f - std::pow(1.0f - open, 3.0f);
        const ImVec2 pivot(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
        for (int i = first; i < d->VtxBuffer.Size; ++i)
        {
            ImDrawVert& v = d->VtxBuffer[i];
            v.pos = pivot + (v.pos - pivot) * ImLerp(0.96f, 1.0f, eased) + ImVec2(4.0f * (1.0f - eased), 0.0f);
            const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
            v.col = (v.col & 0x00ffffffu) | ((ImU32)(a * eased) << IM_COL32_A_SHIFT);
        }
    }

    // ---------------------------------------------------------------------
    // Navigation
    // ---------------------------------------------------------------------
    void ChangePage(int page)
    {
        if (state.page == page)
            return;
        state.page = page;
        state.page_mix = 0.0f;
        state.scroll = 0.0f;
        state.popup_open = false;
    }

    void NavItemDraw(ImDrawList* d, ImVec2 p, ImVec2 size, const NavItem& item, bool selected, bool horizontal)
    {
        ImGui::PushID(item.page);
        const bool clicked = Hit("##nav", p, size);
        const ImGuiID id = ImGui::GetItemID();
        const float response = ImGui::IsItemHovered() ? 0.5f : 0.0f;
        const float r = Motion(id ^ 0x7771u, selected ? 1.0f : response);
        ImGui::PopID();

        if (clicked)
            ChangePage(item.page);

        if (r > 0.001f)
            d->AddRectFilled(p, p + size, Mix(C(18, 21, 30, 0), C(39, 43, 54), r), 6.0f);

        if (horizontal)
        {
            const float label_w = ImGui::GetFont()->CalcTextSizeA(kBody, FLT_MAX, 0.0f, item.label).x;
            const float icon_w = ImGui::GetFont()->CalcTextSizeA(kIcon, FLT_MAX, 0.0f, item.icon).x;
            const float total = icon_w + 6.0f + label_w;
            const float x = p.x + (size.x - total) * 0.5f;
            TextY(d, x, p.y, size.y, Mix(C(137, 142, 153), C(82, 141, 255), r), item.icon, kIcon);
            TextY(d, x + icon_w + 6.0f, p.y, size.y, Mix(C(145, 149, 159), C(226, 228, 235), r), item.label, kBody);
        }
        else
        {
            TextY(d, p.x + 10.0f, p.y, size.y, Mix(C(137, 142, 153), C(82, 141, 255), r), item.icon, kIcon);
            TextY(d, p.x + 31.0f, p.y, size.y, Mix(C(145, 149, 159), C(226, 228, 235), r), item.label, kBody);
        }
    }

    void DrawSidebar(ImDrawList* d, ImVec2 base, ImVec2 size, bool* open)
    {
        d->AddRectFilled(base, base + ImVec2(kSidebarW, size.y), C(18, 21, 30, 250), 14.0f, ImDrawFlags_RoundCornersLeft);
        d->AddRectFilled(base + ImVec2(kSidebarW - 13.0f, 0.0f), base + ImVec2(kSidebarW, size.y), C(18, 21, 30, 250));
        d->AddLine(base + ImVec2(kSidebarW, 0.0f), base + ImVec2(kSidebarW, size.y), C(30, 33, 43));

        d->AddRectFilled(base + ImVec2(13.0f, 12.0f), base + ImVec2(43.0f, 42.0f), C(8, 27, 48), 7.0f);
        Text(d, base + ImVec2(19.0f, 18.0f), C(94, 185, 255), "SY", kTitle, StrongFont());
        Text(d, base + ImVec2(51.0f, 12.0f), C(228, 230, 236), "Synapse", kTitle, StrongFont());
        Text(d, base + ImVec2(52.0f, 30.0f), C(91, 96, 108), "iOS build", kSmall);
        d->AddLine(base + ImVec2(10.0f, 54.0f), base + ImVec2(kSidebarW - 10.0f, 54.0f), C(28, 31, 41));

        Text(d, base + ImVec2(15.0f, 64.0f), C(91, 96, 108), "COMBAT", kCaption);
        NavItemDraw(d, base + ImVec2(7.0f, 78.0f), ImVec2(kSidebarW - 14.0f, 30.0f), kNav[0], state.page == PageRage, false);
        NavItemDraw(d, base + ImVec2(7.0f, 112.0f), ImVec2(kSidebarW - 14.0f, 30.0f), kNav[1], state.page == PageAim, false);

        Text(d, base + ImVec2(15.0f, 149.0f), C(91, 96, 108), "VISUALS", kCaption);
        NavItemDraw(d, base + ImVec2(7.0f, 163.0f), ImVec2(kSidebarW - 14.0f, 30.0f), kNav[2], state.page == PageVisuals, false);

        Text(d, base + ImVec2(15.0f, 200.0f), C(91, 96, 108), "CONFIG", kCaption);
        NavItemDraw(d, base + ImVec2(7.0f, 214.0f), ImVec2(kSidebarW - 14.0f, 30.0f), kNav[3], state.page == PageSettings, false);

        Text(d, base + ImVec2(15.0f, size.y - 26.0f), C(70, 74, 84), "v0.0.4b", kSmall);
        (void)open;
    }

    void DrawTopBar(ImDrawList* d, ImVec2 base, ImVec2 size, bool* open)
    {
        d->AddRectFilled(base, base + ImVec2(size.x, 48.0f), C(11, 13, 20, 248), 14.0f,
                         ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersTopRight);
        d->AddRectFilled(base + ImVec2(13.0f, 11.0f), base + ImVec2(39.0f, 37.0f), C(8, 27, 48), 7.0f);
        Text(d, base + ImVec2(18.0f, 15.0f), C(94, 185, 255), "SY", 14.0f, StrongFont());
        Text(d, base + ImVec2(47.0f, 8.0f), C(228, 230, 236), "Synapse", 14.0f, StrongFont());
        Text(d, base + ImVec2(48.0f, 26.0f), C(91, 96, 108), "iOS build", kCaption);

        const ImVec2 close_c(base.x + size.x - 24.0f, base.y + 24.0f);
        const bool close = Hit("##close_top", close_c - ImVec2(22.0f, 20.0f), ImVec2(44.0f, 40.0f));
        if (ImGui::IsItemHovered())
            d->AddCircleFilled(close_c, 13.0f, C(39, 43, 54));
        CloseCross(d, close_c, C(180, 184, 194));
        if (close)
            *open = false;

        const float cell_w = (size.x - 16.0f) / 4.0f;
        for (int i = 0; i < 4; ++i)
            NavItemDraw(d, base + ImVec2(8.0f + i * cell_w, 50.0f), ImVec2(cell_w, 40.0f), kNav[i],
                        state.page == kNav[i].page, true);
        d->AddLine(base + ImVec2(0.0f, 91.0f), base + ImVec2(size.x, 91.0f), C(27, 30, 39));
    }

    void DrawToolbar(ImDrawList* d, ImVec2 base, ImVec2 size, bool* open)
    {
        d->AddRectFilled(base + ImVec2(kSidebarW, 0.0f), base + ImVec2(size.x, kToolbarH), C(11, 13, 20, 245),
                         14.0f, ImDrawFlags_RoundCornersTopRight);
        d->AddLine(base + ImVec2(kSidebarW, kToolbarH), base + ImVec2(size.x, kToolbarH), C(27, 30, 39));

        const char* titles[] = { "RAGE", "AIM", "VISUALS", "SETTINGS" };
        d->AddRectFilled(base + ImVec2(kSidebarW + 14.0f, 20.0f), base + ImVec2(kSidebarW + 17.0f, 28.0f),
                         C(78, 131, 255), 1.5f);
        TextY(d, base.x + kSidebarW + 26.0f, base.y, kToolbarH, C(225, 227, 233), titles[state.page], kBody);

        const ImVec2 close_c(base.x + size.x - 24.0f, base.y + kToolbarH * 0.5f);
        const bool close = Hit("##close", close_c - ImVec2(22.0f, 20.0f), ImVec2(44.0f, 40.0f));
        if (ImGui::IsItemHovered())
            d->AddCircleFilled(close_c, 13.0f, C(39, 43, 54));
        CloseCross(d, close_c, C(180, 184, 194));
        if (close)
            *open = false;
    }

    void DrawSubTabs(ImDrawList* d, ImVec2 p, float* content_top)
    {
        const char* labels[2] = { "Players", "World" };
        for (int i = 0; i < 2; ++i)
        {
            const ImVec2 chip(p.x + i * 92.0f, p.y);
            const bool selected = state.visuals_tab == i;
            ImGui::PushID(700 + i);
            const bool clicked = Hit("##subtab", chip, ImVec2(86.0f, 26.0f));
            const ImGuiID id = ImGui::GetItemID();
            const float hover = Motion(id ^ 0x3131u, ImGui::IsItemHovered() ? 1.0f : 0.0f, 18.0f);
            ImGui::PopID();
            if (clicked)
            {
                state.visuals_tab = i;
                state.scroll = 0.0f;
                state.popup_open = false;
            }
            const ImU32 fill = selected ? C(39, 43, 54) : Mix(C(24, 27, 37, 0), C(28, 31, 42), hover);
            d->AddRectFilled(chip, chip + ImVec2(86.0f, 26.0f), fill, 6.0f);
            TextY(d, chip.x, chip.y, 26.0f, selected ? C(226, 228, 235) : C(145, 149, 159), labels[i], kControl);
        }
        *content_top += 34.0f;
    }

    // ---------------------------------------------------------------------
    // Page layout
    // ---------------------------------------------------------------------
    struct PlacedCard
    {
        const Card* card = nullptr;
        ImVec2 pos{};
        float width = 0.0f;
    };

    void DrawContent(ImDrawList* d, ImVec2 content, ImVec2 content_size, bool portrait)
    {
        if (state.page == PageVisuals)
            DrawSubTabs(d, content, &content.y);
        content_size.y = ImMax(40.0f, content_size.y - (state.page == PageVisuals ? 34.0f : 0.0f));

        const Card* cards = nullptr;
        int card_count = 0;
        if (state.page == PageRage) { cards = kRageCards; card_count = IM_ARRAYSIZE(kRageCards); }
        else if (state.page == PageAim) { cards = kAimCards; card_count = IM_ARRAYSIZE(kAimCards); }
        else if (state.page == PageSettings) { cards = kSettingsCards; card_count = IM_ARRAYSIZE(kSettingsCards); }
        else
        {
            cards = state.visuals_tab == 0 ? kVisualPlayers : kVisualWorld;
            card_count = state.visuals_tab == 0 ? IM_ARRAYSIZE(kVisualPlayers) : IM_ARRAYSIZE(kVisualWorld);
        }

        const float row_h = portrait ? 28.0f : 30.0f;
        int columns = portrait ? 1 : (content_size.x >= 500.0f ? 3 : 2);
        if (content_size.x < 330.0f)
            columns = 1;
        const float card_w = (content_size.x - (columns - 1) * kGap) / columns;

        PlacedCard placed[8];
        float column_y[4] = { content.y, content.y, content.y, content.y };
        int max_cards = card_count < 8 ? card_count : 8;

        for (int i = 0; i < max_cards; ++i)
        {
            int best = 0;
            for (int c = 1; c < columns; ++c)
                if (column_y[c] < column_y[best] - 0.1f)
                    best = c;

            placed[i].card = &cards[i];
            placed[i].pos = ImVec2(content.x + best * (card_w + kGap), column_y[best] + kTitleH);
            placed[i].width = card_w;
            column_y[best] += kTitleH + cards[i].count * row_h + 6.0f + kGap;
        }

        float content_h = 0.0f;
        for (int c = 0; c < columns; ++c)
            content_h = ImMax(content_h, column_y[c] - content.y);
        const float max_scroll = ImMax(0.0f, content_h - content_size.y);

        // Touch scrolling. A full-area InvisibleButton here would steal clicks from
        // the rows, so the drag is tracked from the raw mouse state and only starts
        // on empty space. Sliders/toggles keep ownership of their own drags.
        const ImGuiIO& io = ImGui::GetIO();
        if (max_scroll > 0.0f)
        {
            if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(content, content + content_size) &&
                !ImGui::IsAnyItemHovered())
                state.scroll_drag = true;
            if (!ImGui::IsMouseDown(0) || ImGui::IsAnyItemActive())
                state.scroll_drag = false;
            if (state.scroll_drag)
                state.scroll -= io.MouseDelta.y;
            state.scroll -= io.MouseWheel * row_h * 2.0f;
        }
        else
        {
            state.scroll_drag = false;
        }
        state.scroll = ImClamp(state.scroll, 0.0f, max_scroll);

        ImGui::PushClipRect(content, content + content_size, true);

        const int content_first = d->VtxBuffer.Size;
        const bool interactive = !state.popup_open;
        for (int i = 0; i < max_cards; ++i)
        {
            ImVec2 p = placed[i].pos;
            p.y -= state.scroll;
            DrawCard(d, p, placed[i].width, *placed[i].card, row_h, interactive);
        }

        // Page transition: fade + tiny slide, applied to the content vertices only.
        state.page_mix = ImLerp(state.page_mix, 1.0f, 1.0f - std::exp(-14.0f * ImGui::GetIO().DeltaTime));
        if (state.page_mix < 0.999f)
        {
            const float eased = 1.0f - std::pow(1.0f - state.page_mix, 3.0f);
            for (int i = content_first; i < d->VtxBuffer.Size; ++i)
            {
                ImDrawVert& v = d->VtxBuffer[i];
                v.pos.x += 7.0f * (1.0f - eased);
                const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
                v.col = (v.col & 0x00ffffffu) | ((ImU32)(a * eased) << IM_COL32_A_SHIFT);
            }
        }

        if (max_scroll > 0.0f)
        {
            const float ratio = content_size.y / content_h;
            const float thumb_h = ImMax(24.0f, content_size.y * ratio);
            const float thumb_y = content.y + (content_size.y - thumb_h) * (state.scroll / max_scroll);
            d->AddRectFilled(ImVec2(content.x + content_size.x - 4.0f, thumb_y),
                             ImVec2(content.x + content_size.x, thumb_y + thumb_h), C(78, 131, 255, 130), 2.0f);
        }

        ImGui::PopClipRect();
    }
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------
namespace menu
{
    void init()
    {
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;

        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 10.0f;
        style.ChildRounding = 10.0f;
        style.FrameRounding = 6.0f;
        style.PopupRounding = 10.0f;
        style.GrabRounding = 6.0f;
        style.TabRounding = 6.0f;
        style.ScrollbarRounding = 6.0f;
        style.WindowBorderSize = 0.0f;
        style.FrameBorderSize = 0.0f;
        style.PopupBorderSize = 1.0f;
        style.WindowPadding = ImVec2(10.0f, 10.0f);
        style.FramePadding = ImVec2(8.0f, 5.0f);
        style.ItemSpacing = ImVec2(8.0f, 7.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
        style.ScrollbarSize = 10.0f;
        style.GrabMinSize = 12.0f;

        ImVec4* c = style.Colors;
        c[ImGuiCol_Text] = ImVec4(0.86f, 0.87f, 0.90f, 1.00f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.57f, 0.59f, 0.63f, 1.00f);
        c[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.06f, 0.09f, 0.98f);
        c[ImGuiCol_ChildBg] = ImVec4(0.05f, 0.06f, 0.09f, 0.00f);
        c[ImGuiCol_PopupBg] = ImVec4(0.078f, 0.082f, 0.110f, 0.98f);
        c[ImGuiCol_Border] = ImVec4(0.18f, 0.19f, 0.24f, 1.00f);
        c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_FrameBg] = ImVec4(0.094f, 0.106f, 0.145f, 1.00f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.13f, 0.15f, 0.21f, 1.00f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.18f, 0.25f, 1.00f);
        c[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.06f, 0.09f, 1.00f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.05f, 0.06f, 0.09f, 1.00f);
        c[ImGuiCol_MenuBarBg] = ImVec4(0.05f, 0.06f, 0.09f, 1.00f);
        c[ImGuiCol_ScrollbarBg] = ImVec4(0.04f, 0.05f, 0.07f, 0.60f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.22f, 0.28f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.31f, 0.39f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.31f, 0.43f, 0.78f, 1.00f);
        c[ImGuiCol_CheckMark] = ImVec4(0.31f, 0.51f, 1.00f, 1.00f);
        c[ImGuiCol_SliderGrab] = ImVec4(0.31f, 0.51f, 1.00f, 1.00f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.64f, 1.00f, 1.00f);
        c[ImGuiCol_Button] = ImVec4(0.10f, 0.12f, 0.17f, 1.00f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.15f, 0.18f, 0.25f, 1.00f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.21f, 0.29f, 1.00f);
        c[ImGuiCol_Header] = ImVec4(0.15f, 0.18f, 0.25f, 1.00f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.24f, 0.33f, 1.00f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.25f, 0.31f, 0.43f, 1.00f);
        c[ImGuiCol_Separator] = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
        c[ImGuiCol_SeparatorHovered] = ImVec4(0.31f, 0.51f, 1.00f, 0.60f);
        c[ImGuiCol_SeparatorActive] = ImVec4(0.31f, 0.51f, 1.00f, 1.00f);
        c[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_ResizeGripHovered] = ImVec4(0.31f, 0.51f, 1.00f, 0.60f);
        c[ImGuiCol_ResizeGripActive] = ImVec4(0.31f, 0.51f, 1.00f, 0.90f);
        c[ImGuiCol_Tab] = ImVec4(0.10f, 0.12f, 0.17f, 1.00f);
        c[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.24f, 0.33f, 1.00f);
        c[ImGuiCol_TabSelected] = ImVec4(0.25f, 0.31f, 0.43f, 1.00f);
        c[ImGuiCol_TabSelectedOverline] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_PlotLines] = ImVec4(0.31f, 0.51f, 1.00f, 1.00f);
        c[ImGuiCol_PlotLinesHovered] = ImVec4(0.40f, 0.64f, 1.00f, 1.00f);
        c[ImGuiCol_PlotHistogram] = ImVec4(0.31f, 0.51f, 1.00f, 1.00f);
        c[ImGuiCol_PlotHistogramHovered] = ImVec4(0.40f, 0.64f, 1.00f, 1.00f);
        c[ImGuiCol_TableHeaderBg] = ImVec4(0.10f, 0.12f, 0.17f, 1.00f);
        c[ImGuiCol_TableBorderStrong] = ImVec4(0.18f, 0.19f, 0.24f, 1.00f);
        c[ImGuiCol_TableBorderLight] = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
        c[ImGuiCol_TextSelectedBg] = ImVec4(0.31f, 0.51f, 1.00f, 0.35f);
        c[ImGuiCol_NavCursor] = ImVec4(0.31f, 0.51f, 1.00f, 1.00f);

        menu_fonts::add();
    }

    void launcher(bool* open, const ImVec2& pos, const ImVec2& size)
    {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##synapse_launcher", nullptr, flags))
        {
            const ImVec2 p = ImGui::GetWindowPos();
            const bool click = Hit("##launch", p, size);
            const ImGuiID id = ImGui::GetItemID();
            const float hover = Motion(id ^ 0x51a0u, ImGui::IsItemHovered() ? 1.0f : 0.0f, 18.0f);
            ImDrawList* d = ImGui::GetWindowDrawList();

            d->AddRectFilled(p, p + size, Mix(C(20, 22, 31, 225), C(27, 30, 41, 245), hover), 8.0f);
            d->AddRect(p, p + size, Mix(C(40, 44, 56), C(75, 126, 255), hover), 8.0f);
            d->AddRectFilled(p + ImVec2(11.0f, size.y * 0.5f - 4.0f), p + ImVec2(19.0f, size.y * 0.5f + 4.0f),
                             C(75, 126, 255), 2.0f);
            TextY(d, p.x + 27.0f, p.y, size.y, C(220, 223, 231), "MENU", kControl);
            if (click)
                *open = true;
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    void render(bool* open, const ImVec2& pos, const ImVec2& size)
    {
        if (open == nullptr || !*open)
        {
            state.open_mix = 0.0f;
            state.scroll_drag = false;
            state.popup_open = false;
            state.popup_target = nullptr;
            state.popup_items = nullptr;
            state.popup_count = 0;
            return;
        }
        if (size.x < 240.0f || size.y < 220.0f)
            return;

        ImGuiIO& io = ImGui::GetIO();
        state.open_mix = ImLerp(state.open_mix, 1.0f, 1.0f - std::exp(-18.0f * io.DeltaTime));
        if (state.open_mix > 0.999f)
            state.open_mix = 1.0f;

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##synapse_menu", nullptr, flags))
        {
            ImDrawList* d = ImGui::GetWindowDrawList();
            const int frame_first = d->VtxBuffer.Size;
            const ImVec2 base = pos;

            // Dim the scene behind the menu (replaces the Windows D3D blur).
            d->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, C(9, 11, 18, 110));

            // Soft shadow around the shell.
            d->AddRectFilled(base - ImVec2(2.0f, 2.0f), base + size + ImVec2(2.0f, 4.0f), C(0, 0, 0, 60), 16.0f);
            d->AddRectFilled(base - ImVec2(6.0f, 5.0f), base + size + ImVec2(6.0f, 9.0f), C(0, 0, 0, 38), 19.0f);

            // Shell.
            d->AddRectFilled(base, base + size, C(13, 15, 22, 247), 14.0f);

            if (size.y > size.x)
            {
                DrawTopBar(d, base, size, open);
                DrawContent(d, base + ImVec2(8.0f, 97.0f),
                            ImVec2(size.x - 16.0f, size.y - 105.0f), true);
            }
            else
            {
                DrawSidebar(d, base, size, open);
                DrawToolbar(d, base, size, open);
                DrawContent(d, base + ImVec2(kSidebarW + 8.0f, kToolbarH + 8.0f),
                            ImVec2(size.x - kSidebarW - 16.0f, size.y - kToolbarH - 16.0f), false);
            }

            // Keep the outer border crisp above the sidebar/toolbar fills.
            d->AddRect(base, base + size, C(31, 34, 44), 14.0f);

            PopupLayer(d, io.DisplaySize);

            if (state.open_mix < 0.999f)
                FadeVertices(d, frame_first, state.open_mix);
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
}
