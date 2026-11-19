<<<<<<< HEAD
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
=======
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
>>>>>>> 1324a15d57503dc315a2e04edf9268260ad9ac15
