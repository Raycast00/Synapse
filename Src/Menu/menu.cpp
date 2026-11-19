// Strict iOS port of the nlka menu.
//
// Source: C:\Users\krzhs\OneDrive\Documents\nlka\examples\example_win32_directx11\
//           neverlose_menu.cpp / neverlose_menu.hpp / neverlose_menu_internal.hpp
//
// The layout, colors, animations, cards, rows and page content are kept exactly
// as in the original 750x756 canvas. The canvas is uniformly scaled to the iOS
// screen. The only substitutions:
//   * Direct3D blur  -> full-screen dim layer (no scene texture available);
//   * avatar texture -> small vector avatar drawn in the same rectangle;
//   * the decorative magnifier in the toolbar is a close button (iOS has no Esc);
//   * controls whose feature does not exist in Synapse keep local dummy state.

#define IMGUI_DEFINE_MATH_OPERATORS
#include "menu.h"
#include "menu_fonts.h"
#include "menu_icons.h"

#include "imgui.h"
#include "imgui_internal.h"

#include "Features/Aim/aim.h"
#include "Features/Rage/rage.h"
#include "Features/Visuals/visuals.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    // ---------------------------------------------------------------------
    // Canvas / theme (values copied from nlka)
    // ---------------------------------------------------------------------
    constexpr ImVec2 kCanvas(750.0f, 756.0f);
    constexpr ImVec2 kShell(748.0f, 576.0f);
    constexpr float kTextBody = 15.0f;
    constexpr float kTextControl = 14.0f;
    constexpr float kTextSmall = 12.0f;
    constexpr float kTextCaption = 10.0f;
    constexpr float kTextTitle = 16.0f;
    constexpr float kTextIcon = 13.0f;
    constexpr ImU32 C(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

    enum class NeverlosePage
    {
        Rage,
        Legit,
        Players,
        World,
        Inventory,
        Miscellaneous
    };

    enum class NeverloseControl
    {
        Toggle,
        Select,
        MultiSelect,
        Slider,
        Chevron,
        Color,
        Disabled
    };

    struct Row
    {
        const char* label;
        NeverloseControl type;
        const char* value;
        float amount;

        // Optional bindings to the real Synapse variables. Rows without a
        // binding keep the original local demo state, so they still animate.
        bool* bind_toggle = nullptr;
        float* bind_slider = nullptr;
        float bind_lo = 0.0f;
        float bind_hi = 1.0f;
    };

    struct Popup
    {
        bool open = false;
        bool multi = false;
        int owner = -1;
        int selected = 0;
        unsigned mask = 0x07;
        ImVec2 anchor{};
        float width = 134.0f;
        int opened_frame = 0;
    };

    struct State
    {
        NeverlosePage page = NeverlosePage::Rage;
        NeverlosePage previous = NeverlosePage::Rage;
        float page_mix = 1.0f;
        bool account = false;
        bool toggles[192]{};
        bool slider_initialized[192]{};
        int selects[192]{};
        float sliders[192]{};
        Popup popup;
        float open_mix = 0.0f;
        ImVec2 popup_rect_min{};
        ImVec2 popup_rect_max{};

        State()
        {
            const int enabled[] = {
                0, 1, 2, 3, 18, 19,
                48, 57,
                79, 80, 88, 89, 95,
                120, 121, 122, 123, 124, 127, 128, 131, 132, 134, 135
            };
            for (int id : enabled) toggles[id] = true;
        }
    } state;

    // ---------------------------------------------------------------------
    // Canvas -> screen transform
    // ---------------------------------------------------------------------
    struct View
    {
        ImVec2 origin{};
        float scale = 1.0f;
    } view;

    ImVec2 P(float x, float y) { return ImVec2(view.origin.x + x * view.scale, view.origin.y + y * view.scale); }
    ImVec2 P(const ImVec2& v) { return P(v.x, v.y); }
    ImVec2 SZ(const ImVec2& v) { return ImVec2(v.x * view.scale, v.y * view.scale); }
    ImVec2 SZ(float x, float y) { return SZ(ImVec2(x, y)); }
    float S(float v) { return v * view.scale; }

    // ---------------------------------------------------------------------
    // Helpers (same math as nlka, with the canvas transform)
    // ---------------------------------------------------------------------
    float Motion(ImGuiID id, float target, float speed = 16.0f, float initial = -1.0f)
    {
        float* value = ImGui::GetStateStorage()->GetFloatRef(id, initial < 0.0f ? target : initial);
        *value = ImLerp(*value, target, 1.0f - std::exp(-speed * ImGui::GetIO().DeltaTime));
        if (std::fabs(*value - target) < 0.0005f) *value = target;
        return *value;
    }

    ImU32 Mix(ImU32 a, ImU32 b, float t)
    {
        return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(a), ImGui::ColorConvertU32ToFloat4(b), ImSaturate(t)));
    }

    void Text(ImDrawList* d, ImVec2 p, ImU32 color, const char* value, float size = kTextBody, ImFont* font = nullptr)
    {
        d->AddText(font ? font : ImGui::GetFont(), S(size), P(p), color, value);
    }

    void TextY(ImDrawList* d, float x, float y, float h, ImU32 color, const char* value, float size = kTextBody, ImFont* font = nullptr)
    {
        ImFont* f = font ? font : ImGui::GetFont();
        const float th = f->CalcTextSizeA(S(size), FLT_MAX, 0.0f, value).y / view.scale;
        Text(d, ImVec2(x, y + std::floor((h - th) * 0.5f)), color, value, size, f);
    }

    bool Hit(const char* id, ImVec2 p, ImVec2 size)
    {
        ImGui::SetCursorScreenPos(P(p));
        return ImGui::InvisibleButton(id, SZ(size));
    }

    void Dots(ImDrawList* d, ImVec2 p)
    {
        for (int i = 0; i < 3; ++i) d->AddCircleFilled(P(p + ImVec2(i * 3.0f, 0)), S(1.0f), C(187, 191, 202));
    }

    void Chevron(ImDrawList* d, ImVec2 p, ImU32 color)
    {
        d->AddLine(P(p), P(p + ImVec2(3, 3)), color, S(1.3f));
        d->AddLine(P(p + ImVec2(3, 3)), P(p + ImVec2(0, 6)), color, S(1.3f));
    }

    void Check(ImDrawList* d, ImVec2 p, ImU32 color)
    {
        d->AddLine(P(p), P(p + ImVec2(3, 3)), color, S(1.5f));
        d->AddLine(P(p + ImVec2(3, 3)), P(p + ImVec2(8, -3)), color, S(1.5f));
    }

    void CloseCross(ImDrawList* d, ImVec2 c, ImU32 color)
    {
        d->AddLine(P(c + ImVec2(-4, -4)), P(c + ImVec2(4, 4)), color, S(1.5f));
        d->AddLine(P(c + ImVec2(4, -4)), P(c + ImVec2(-4, 4)), color, S(1.5f));
    }

    // No avatar texture on iOS: draw the same rounded square with a silhouette.
    void Avatar(ImDrawList* d, ImVec2 p, float size, float rounding)
    {
        d->AddRectFilled(P(p), P(p + ImVec2(size, size)), C(9, 30, 54), S(rounding));
        d->AddRect(P(p), P(p + ImVec2(size, size)), C(38, 84, 132), S(rounding));
        const ImVec2 c = p + ImVec2(size * 0.5f, size * 0.5f);
        d->AddCircleFilled(P(ImVec2(c.x, p.y + size * 0.37f)), S(size * 0.14f), C(94, 185, 255));
        d->AddRectFilled(P(ImVec2(p.x + size * 0.26f, p.y + size * 0.60f)),
                         P(ImVec2(p.x + size * 0.74f, p.y + size * 0.94f)),
                         C(94, 185, 255), S(size * 0.18f));
    }

    void Card(ImDrawList* d, ImVec2 p, ImVec2 size, const char* title)
    {
        Text(d, p + ImVec2(12, -18), C(89, 94, 106), title, kTextCaption);
        d->AddRectFilled(P(p), P(p + size), C(17, 19, 27, 224), S(14.0f));
        d->AddRect(P(p), P(p + size), C(31, 34, 44), S(14.0f));
    }

    void Toggle(ImDrawList* d, int id, ImVec2 p, bool enabled = true, bool* bind = nullptr)
    {
        bool& value = bind != nullptr ? *bind : state.toggles[id];
        ImGui::PushID(id);
        if (enabled && Hit("##toggle", p - ImVec2(5, 6), ImVec2(39, 30))) value = !value;
        const ImGuiID iid = ImGui::GetItemID();
        const float on = Motion(iid ^ 0x55aa721u, value ? 1.0f : 0.0f, 19.0f);
        const float hover = Motion(iid ^ 0x118a0u, enabled && ImGui::IsItemHovered() ? 1.0f : 0.0f, 20.0f);
        ImGui::PopID();
        d->AddRectFilled(P(p - ImVec2(1 + hover, 1 + hover)), P(p + ImVec2(30 + hover, 19 + hover)), C(75, 126, 255, (int)(45 * hover)), S(10));
        d->AddRectFilled(P(p), P(p + ImVec2(29, 18)), Mix(C(29, 33, 43), C(75, 126, 255), on), S(9));
        d->AddCircleFilled(P(p + ImVec2(ImLerp(9.0f, 20.0f, on), 9)), S(7), Mix(C(133, 144, 156), C(248, 249, 252), on));
    }

    void OpenPopup(int owner, bool multi, ImVec2 anchor, float width)
    {
        const bool same = state.popup.open && state.popup.owner == owner;
        state.popup.open = !same;
        state.popup.owner = owner;
        state.popup.multi = multi;
        state.popup.anchor = anchor;
        state.popup.width = width;
        state.popup.opened_frame = ImGui::GetFrameCount();
    }

    void RowControl(ImDrawList* d, ImVec2 card, float width, int row, const Row& item, int& control_id)
    {
        const float y = card.y + row * 37.0f;
        if (row) d->AddLine(P(card.x + 12, y), P(card.x + width - 12, y), C(28, 31, 40), S(1.0f));
        const bool disabled = item.type == NeverloseControl::Disabled;
        TextY(d, card.x + 13, y, 37, disabled ? C(92, 96, 107) : C(207, 209, 218), item.label, kTextBody);
        const int id = control_id++ + static_cast<int>(state.page) * 24;
        switch (item.type)
        {
        case NeverloseControl::Toggle:
            Toggle(d, id, ImVec2(card.x + width - 43, y + 9), true, item.bind_toggle);
            break;
        case NeverloseControl::Disabled:
            Toggle(d, id, ImVec2(card.x + width - 43, y + 9), false);
            break;
        case NeverloseControl::Select:
        case NeverloseControl::MultiSelect:
        {
            const float cw = ImMin(134.0f, width * 0.48f);
            const ImVec2 cp(card.x + width - cw - 13, y + 7);
            ImGui::PushID(id);
            const bool click = Hit("##select", cp, ImVec2(cw, 23));
            const ImGuiID iid = ImGui::GetItemID();
            const float response = Motion(iid ^ 0x6161u, ImGui::IsItemHovered() ? 1.0f : 0.0f);
            ImGui::PopID();
            if (click) OpenPopup(id, item.type == NeverloseControl::MultiSelect, cp, item.type == NeverloseControl::MultiSelect ? 134.0f : cw);
            d->AddRectFilled(P(cp), P(cp + ImVec2(cw, 23)), Mix(C(25, 28, 38), C(31, 38, 54), response), S(5));
            d->AddRect(P(cp), P(cp + ImVec2(cw, 23)), Mix(C(32, 35, 46), C(75, 126, 255), response), S(5));
            TextY(d, cp.x + 7, cp.y, 23, C(170, 173, 184), item.value ? item.value : "Select", kTextControl);
            d->AddLine(P(cp + ImVec2(cw - 13, 9)), P(cp + ImVec2(cw - 9, 13)), C(139, 143, 154), S(1.0f));
            d->AddLine(P(cp + ImVec2(cw - 9, 13)), P(cp + ImVec2(cw - 5, 9)), C(139, 143, 154), S(1.0f));
            break;
        }
        case NeverloseControl::Slider:
        {
            ImVec2 start(card.x + width - 145, y + 18);
            const float tw = 80.0f;
            float value;
            if (item.bind_slider != nullptr)
            {
                const float range = item.bind_hi - item.bind_lo;
                value = range > 0.0f ? (*item.bind_slider - item.bind_lo) / range : 0.0f;
            }
            else
            {
                if (!state.slider_initialized[id])
                {
                    state.sliders[id] = item.amount;
                    state.slider_initialized[id] = true;
                }
                value = state.sliders[id];
            }
            ImGui::PushID(id);
            Hit("##slider", start - ImVec2(5, 8), ImVec2(tw + 10, 19));
            if (ImGui::IsItemActive())
            {
                value = ImClamp((ImGui::GetIO().MousePos.x - P(start).x) / S(tw), 0.0f, 1.0f);
                if (item.bind_slider != nullptr)
                    *item.bind_slider = item.bind_lo + value * (item.bind_hi - item.bind_lo);
                else
                    state.sliders[id] = value;
            }
            const float shown = Motion(ImGui::GetItemID() ^ 0xa8f1u, value, 14.0f, item.amount);
            ImGui::PopID();
            d->AddRectFilled(P(start), P(start + ImVec2(tw, 3)), C(34, 38, 48), S(2));
            d->AddRectFilled(P(start), P(start + ImVec2(tw * shown, 3)), C(75, 126, 255), S(2));
            d->AddCircleFilled(P(start + ImVec2(tw * shown, 1.5f)), S(5.5f), C(247, 248, 252));
            const ImVec2 pill(card.x + width - 55, y + 8);
            d->AddRectFilled(P(pill), P(pill + ImVec2(42, 21)), C(25, 28, 38), S(5));
            const char* pill_text = item.value ? item.value : "0%";
            const float pill_width = ImGui::GetFont()->CalcTextSizeA(S(kTextControl), FLT_MAX, 0.0f, pill_text).x / view.scale;
            TextY(d, pill.x + (42.0f - pill_width) * 0.5f, pill.y, 21, C(166, 169, 179), pill_text, kTextControl);
            break;
        }
        case NeverloseControl::Color:
            d->AddRectFilled(P(ImVec2(card.x + width - 63, y + 11)), P(ImVec2(card.x + width - 48, y + 26)), C(102, 124, 246), S(5));
            Toggle(d, id, ImVec2(card.x + width - 43, y + 9));
            break;
        case NeverloseControl::Chevron:
            Chevron(d, ImVec2(card.x + width - 22, y + 15), C(187, 190, 199));
            break;
        }
    }

    void DrawRows(ImDrawList* d, ImVec2 card, float width, const Row* rows, int count, int& control_id)
    {
        for (int i = 0; i < count; ++i) RowControl(d, card, width, i, rows[i], control_id);
    }

    void PopupLayer(ImDrawList* d)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) state.popup.open = false;
        const float open = Motion(ImGui::GetID("##popup_open"), state.popup.open ? 1.0f : 0.0f, 20.0f, 0.0f);
        if (open < 0.002f)
        {
            state.popup_rect_min = ImVec2(0, 0);
            state.popup_rect_max = ImVec2(0, 0);
            return;
        }
        static const char* const multi_items[] = { "Head", "Chest", "Stomach", "Arms", "Legs", "Feet" };
        static const char* const single_items[] = { "Off", "Latency", "Performance" };
        const char* const* options = state.popup.multi ? multi_items : single_items;
        const int count = state.popup.multi ? 6 : 3;
        const ImVec2 size(state.popup.width, count * 32.0f + 8.0f);
        ImVec2 p(state.popup.anchor.x, state.popup.anchor.y + (23.0f - size.y) * 0.5f);
        p.x = ImClamp(p.x, 10.0f, kCanvas.x - size.x - 10.0f);
        p.y = ImClamp(p.y, 10.0f, kCanvas.y - size.y - 10.0f);
        state.popup_rect_min = p;
        state.popup_rect_max = p + size;

        const int first = d->VtxBuffer.Size;
        d->AddRectFilled(P(p - ImVec2(5, 2)), P(p + size + ImVec2(5, 8)), C(0, 0, 0, 55), S(18));
        d->AddRectFilled(P(p), P(p + size), C(20, 20, 29, 235), S(16));
        d->AddRect(P(p), P(p + size), C(57, 61, 76, 205), S(16));
        d->AddLine(P(p + ImVec2(16, 1)), P(p + ImVec2(size.x - 16, 1)), C(255, 255, 255, 22), S(1.0f));
        const bool accepts = ImGui::GetFrameCount() > state.popup.opened_frame;
        bool hovered_any = false;
        for (int i = 0; i < count; ++i)
        {
            ImVec2 rp = p + ImVec2(4, 4 + i * 32.0f);
            ImGui::PushID(5000 + i);
            const bool clicked = Hit("##popup_row", rp, ImVec2(size.x - 8, 32));
            hovered_any |= ImGui::IsItemHovered();
            const float hover = Motion(ImGui::GetItemID() ^ 0x9921u, ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
            ImGui::PopID();
            if (hover > 0.001f) d->AddRectFilled(P(rp), P(rp + ImVec2(size.x - 8, 32)), C(75, 126, 255, (int)(25 * hover)), S(10));
            const bool selected = state.popup.multi ? (state.popup.mask & (1u << i)) != 0 : state.popup.selected == i;
            if (selected) Check(d, rp + ImVec2(14, 16), C(230, 233, 241));
            TextY(d, rp.x + 35, rp.y, 32, selected ? C(224, 229, 243) : C(182, 185, 196), options[i], kTextControl);
            if (accepts && clicked)
            {
                if (state.popup.multi) state.popup.mask ^= 1u << i;
                else { state.popup.selected = i; state.popup.open = false; }
            }
        }
        if (state.popup.open && accepts && ImGui::IsMouseClicked(0) && !hovered_any)
            state.popup.open = false;
        const float eased = 1.0f - std::pow(1.0f - open, 3.0f);
        const ImVec2 pivot = P(p + size * 0.5f);
        for (int i = first; i < d->VtxBuffer.Size; ++i)
        {
            ImDrawVert& v = d->VtxBuffer[i];
            v.pos = pivot + (v.pos - pivot) * ImLerp(0.96f, 1.0f, eased) + ImVec2(S(4.0f) * (1.0f - eased), 0);
            const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
            v.col = (v.col & 0x00ffffffu) | ((ImU32)(a * eased) << IM_COL32_A_SHIFT);
        }
    }

    void ChangePage(NeverlosePage next)
    {
        if (state.page == next) return;
        state.previous = state.page;
        state.page = next;
        state.page_mix = 0.0f;
        state.popup.open = false;
    }

    void Sidebar(ImDrawList* d, ImVec2 base)
    {
        ImFont* strong = menu_fonts::strong();
        d->AddRectFilled(P(base), P(base + ImVec2(158, 576)), C(18, 21, 30, 231), S(14), ImDrawFlags_RoundCornersLeft);
        d->AddRectFilled(P(base + ImVec2(145, 0)), P(base + ImVec2(158, 576)), C(18, 21, 30, 231));
        d->AddLine(P(base + ImVec2(158, 0)), P(base + ImVec2(158, 576)), C(30, 33, 43), S(1.0f));
        d->AddRectFilled(P(base + ImVec2(15, 11)), P(base + ImVec2(45, 43)), C(8, 27, 48), S(7));
        Text(d, base + ImVec2(21, 18), C(94, 185, 255), "NL", kTextTitle, strong);
        Text(d, base + ImVec2(53, 14), C(228, 230, 236), "Neverlose", kTextTitle, strong);
        Text(d, base + ImVec2(53, 33), C(91, 96, 108), "Counter-Strike 2", 8);
        d->AddLine(P(base + ImVec2(10, 56)), P(base + ImVec2(147, 56)), C(28, 31, 41), S(1.0f));
        Text(d, base + ImVec2(16, 67), C(91, 96, 108), "AIMBOT", kTextCaption);
        Text(d, base + ImVec2(16, 169), C(91, 96, 108), "COMMON", kTextCaption);

        auto nav = [&](int id, const char* icon, const char* label, float y, NeverlosePage target, bool selected, float indent = 0.0f)
        {
            const ImVec2 p = base + ImVec2(7 + indent, y);
            ImGui::PushID(id);
            const bool clicked = Hit("##nav", p, ImVec2(140 - indent, 30));
            const ImGuiID iid = ImGui::GetItemID();
            const float r = Motion(iid ^ 0x7771u, selected ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f);
            ImGui::PopID();
            if (clicked) ChangePage(target);
            if (r > 0.001f) d->AddRectFilled(P(p), P(p + ImVec2(140 - indent, 30)), Mix(C(18, 21, 30, 0), C(39, 43, 54), r), S(6));
            TextY(d, p.x + 10, p.y, 30, Mix(C(137, 142, 153), C(82, 141, 255), r), icon, kTextIcon);
            TextY(d, p.x + 31, p.y, 30, Mix(C(145, 149, 159), C(226, 228, 235), r), label, kTextBody);
        };

        const bool visuals = state.page == NeverlosePage::Players || state.page == NeverlosePage::World;
        nav(0, MENU_ICON_CROSSHAIRS, "Rage", 84, NeverlosePage::Rage, state.page == NeverlosePage::Rage);
        nav(1, MENU_ICON_MOUSE, "Legit", 120, NeverlosePage::Legit, state.page == NeverlosePage::Legit);
        nav(2, MENU_ICON_IMAGE, "Visuals", 190, NeverlosePage::Players, visuals);
        const float expand = Motion(ImGui::GetID("##visual_expand"), visuals ? 1.0f : 0.0f, 18.0f);
        if (expand > 0.02f)
        {
            const int first = d->VtxBuffer.Size;
            nav(3, MENU_ICON_USER, "Players", 226, NeverlosePage::Players, state.page == NeverlosePage::Players, 14);
            nav(4, MENU_ICON_GLOBE, "World", 262, NeverlosePage::World, state.page == NeverlosePage::World, 14);
            for (int i = first; i < d->VtxBuffer.Size; ++i)
            {
                ImDrawVert& v = d->VtxBuffer[i];
                const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
                v.col = (v.col & 0xffffffu) | ((ImU32)(a * expand) << IM_COL32_A_SHIFT);
            }
        }
        const float shift = 72.0f * expand;
        nav(5, MENU_ICON_LAYER_GROUP, "Inventory", 226 + shift, NeverlosePage::Inventory, state.page == NeverlosePage::Inventory);
        nav(6, MENU_ICON_SLIDERS_H, "Miscellaneous", 262 + shift, NeverlosePage::Miscellaneous, state.page == NeverlosePage::Miscellaneous);

        const ImVec2 account = base + ImVec2(7, 531);
        if (Hit("##account", account, ImVec2(140, 38))) state.account = !state.account;
        const float ar = Motion(ImGui::GetItemID() ^ 0x1932u, state.account ? 1.0f : ImGui::IsItemHovered() ? 0.5f : 0.0f);
        if (ar > 0.001f) d->AddRectFilled(P(account), P(account + ImVec2(140, 38)), C(39, 43, 54, (int)(235 * ar)), S(6));
        Avatar(d, account + ImVec2(7, 5), 28.0f, 14.0f);
        Text(d, account + ImVec2(43, 4), C(225, 227, 233), "XPTNotFound", kTextControl);
        Text(d, account + ImVec2(43, 20), C(111, 116, 128), "3 days left", kTextSmall);
        Chevron(d, account + ImVec2(132, 15), C(181, 185, 195));
    }

    void Toolbar(ImDrawList* d, ImVec2 base, bool* open)
    {
        d->AddRectFilled(P(base + ImVec2(158, 0)), P(base + ImVec2(748, 56)), C(11, 13, 20, 225), S(14), ImDrawFlags_RoundCornersTopRight);
        d->AddLine(P(base + ImVec2(158, 56)), P(base + ImVec2(748, 56)), C(27, 30, 39), S(1.0f));
        const ImVec2 profile = base + ImVec2(169, 14);
        d->AddRectFilled(P(profile), P(profile + ImVec2(166, 30)), C(17, 19, 27), S(6));
        d->AddRect(P(profile), P(profile + ImVec2(166, 30)), C(28, 31, 41), S(6));
        TextY(d, profile.x + 12, profile.y, 30, C(194, 197, 206), MENU_ICON_SAVE, kTextIcon);
        TextY(d, profile.x + 48, profile.y, 30, C(184, 187, 197), "Nonprime 7.2", kTextControl);
        Chevron(d, profile + ImVec2(148, 11), C(130, 135, 146));
        if (state.page == NeverlosePage::Rage || state.page == NeverlosePage::Legit)
        {
            const ImVec2 global = base + ImVec2(348, 14);
            d->AddRectFilled(P(global), P(global + ImVec2(85, 30)), C(17, 19, 27), S(6));
            d->AddRect(P(global), P(global + ImVec2(85, 30)), C(28, 31, 41), S(6));
            TextY(d, global.x + 13, global.y, 30, C(184, 187, 197), "Global", kTextControl);
            Chevron(d, global + ImVec2(68, 11), C(130, 135, 146));
        }

        // The original magnifier is decorative; on iOS this spot is the close button.
        const ImVec2 close = base + ImVec2(719, 28);
        ImGui::PushID(4242);
        const bool clicked = Hit("##close", close - ImVec2(15, 15), ImVec2(30, 30));
        const float hover = Motion(ImGui::GetItemID() ^ 0x5150u, ImGui::IsItemHovered() ? 1.0f : 0.0f, 18.0f);
        ImGui::PopID();
        if (hover > 0.001f) d->AddCircleFilled(P(close), S(13), C(39, 43, 54, (int)(235 * hover)));
        CloseCross(d, close, C(180, 184, 194));
        if (clicked) *open = false;
    }

    void Rage(ImDrawList* d, ImVec2 b, int& id)
    {
        static const Row main[] = {
            {"Enabled", NeverloseControl::Toggle, nullptr, 0, &aim::aimbot},
            {"Silent Aim", NeverloseControl::Toggle, nullptr, 0, &aim::silent},
            {"Automatic Fire", NeverloseControl::Toggle, nullptr, 0, &aim::automatic_fire},
            {"Aim Through Walls", NeverloseControl::Toggle, nullptr, 0, &rage::wallbang},
            {"Refine Shot", NeverloseControl::Select, "Latency", 0},
            {"Field of View", NeverloseControl::Slider, "180.0\xC2\xB0", .74f, nullptr, &aim::fov_radius, 15.0f, 180.0f}
        };
        static const Row other[] = {
            {"History", NeverloseControl::Select, "Maximum", 0},
            {"Delay Shot", NeverloseControl::Select, "Damage", 0},
            {"Remove Spread", NeverloseControl::Select, "Full", 0},
            {"Duck Peek Assist", NeverloseControl::Toggle, nullptr, 0},
            {"Quick Peek Assist", NeverloseControl::Toggle, nullptr, 0},
            {"Double Tap", NeverloseControl::Toggle, nullptr, 0}
        };
        static const Row selection[] = {
            {"Prefer", NeverloseControl::Select, "Damage", 0},
            {"Hitboxes", NeverloseControl::MultiSelect, "Head, Chest, Stom", 0},
            {"Hit Chance", NeverloseControl::Slider, "0%", .08f},
            {"Min Damage", NeverloseControl::Slider, "FL", .86f},
            {"Quick Stop", NeverloseControl::Toggle, nullptr, 0},
            {"Quick Scope", NeverloseControl::Toggle, nullptr, 0}
        };
        static const Row aa[] = {
            {"Enabled", NeverloseControl::Toggle, nullptr, 0, &aim::anti_aim},
            {"Suppress Breathing Animations", NeverloseControl::Toggle, nullptr, 0},
            {"Leg Movement", NeverloseControl::Select, "Walking", 0},
            {"Pitch", NeverloseControl::Chevron, nullptr, 0},
            {"Yaw", NeverloseControl::Chevron, nullptr, 0},
            {"Mouse Override", NeverloseControl::Chevron, nullptr, 0}
        };
        const ImVec2 a = b + ImVec2(167, 84), c = b + ImVec2(458, 84), e = b + ImVec2(167, 340), g = b + ImVec2(458, 340);
        Card(d, a, ImVec2(281, 221), "MAIN");
        Card(d, c, ImVec2(277, 221), "OTHER");
        Card(d, e, ImVec2(281, 221), "SELECTION");
        Card(d, g, ImVec2(277, 221), "ANTI-AIM");
        DrawRows(d, a, 281, main, 6, id);
        DrawRows(d, c, 277, other, 6, id);
        DrawRows(d, e, 281, selection, 6, id);
        DrawRows(d, g, 277, aa, 6, id);
    }

    void Legit(ImDrawList* d, ImVec2 b, int& id)
    {
        static const Row top[] = {{"Enabled", NeverloseControl::Toggle, nullptr, 0}};
        static const Row aim[] = {
            {"Enabled", NeverloseControl::Toggle, nullptr, 0},
            {"Activation", NeverloseControl::Select, "Assisted", 0},
            {"Conditions", NeverloseControl::MultiSelect, "Through Walls, Th", 0},
            {"Hitboxes", NeverloseControl::MultiSelect, "Head", 0},
            {"Field of View", NeverloseControl::Slider, "20u", .14f},
            {"Smoothing", NeverloseControl::Slider, "50%", .22f},
            {"Reaction Time", NeverloseControl::Slider, "0ms", .08f},
            {"Min Damage", NeverloseControl::Slider, "HP+1", .68f},
            {"Recoil Control", NeverloseControl::Toggle, nullptr, 0},
            {"Quick Scope", NeverloseControl::Toggle, nullptr, 0},
            {"Quick Stop", NeverloseControl::Toggle, nullptr, 0}
        };
        static const Row trigger[] = {
            {"Enabled", NeverloseControl::Toggle, nullptr, 0},
            {"Conditions", NeverloseControl::MultiSelect, "Through Walls, Th", 0},
            {"Hitboxes", NeverloseControl::MultiSelect, "Head, Chest, Stom", 0},
            {"Hit Chance", NeverloseControl::Slider, "Seed", .92f},
            {"Min Damage", NeverloseControl::Slider, "HP+1", .72f},
            {"Reaction Time", NeverloseControl::Slider, "0ms", .08f},
            {"Burst Time", NeverloseControl::Slider, "50ms", .08f},
            {"Quick Scope", NeverloseControl::Toggle, nullptr, 0}
        };
        static const Row other[] = {
            {"Visualize", NeverloseControl::Toggle, nullptr, 0},
            {"Automatic Weapons", NeverloseControl::Toggle, nullptr, 0},
            {"Standalone Recoil Control", NeverloseControl::Toggle, nullptr, 0},
            {"Randomize", NeverloseControl::Select, "None", 0}
        };
        ImVec2 topc = b + ImVec2(167, 84), aimc = b + ImVec2(167, 160), tr = b + ImVec2(458, 84), ot = b + ImVec2(458, 413);
        Card(d, topc, ImVec2(281, 40), "MAIN");
        Card(d, aimc, ImVec2(281, 401), "AIMBOT");
        Card(d, tr, ImVec2(277, 297), "TRIGGERBOT");
        Card(d, ot, ImVec2(277, 148), "OTHER");
        DrawRows(d, topc, 281, top, 1, id);
        DrawRows(d, aimc, 281, aim, 11, id);
        DrawRows(d, tr, 277, trigger, 8, id);
        DrawRows(d, ot, 277, other, 4, id);
    }

    void Soldier(ImDrawList* d, ImVec2 p)
    {
        const ImU32 blue = C(91, 133, 255, 210), edge = C(211, 220, 255, 235), dark = C(39, 49, 77, 245);
        d->AddCircleFilled(P(p + ImVec2(0, -120)), S(22), dark);
        d->AddCircle(P(p + ImVec2(0, -120)), S(22), edge, 0, S(2));
        d->AddRectFilled(P(p + ImVec2(-28, -96)), P(p + ImVec2(28, -25)), dark, S(12));
        d->AddRect(P(p + ImVec2(-28, -96)), P(p + ImVec2(28, -25)), blue, S(12), 0, S(3));
        d->AddQuadFilled(P(p + ImVec2(-25, -85)), P(p + ImVec2(-48, -30)), P(p + ImVec2(-37, -22)), P(p + ImVec2(-10, -66)), dark);
        d->AddQuadFilled(P(p + ImVec2(25, -85)), P(p + ImVec2(51, -45)), P(p + ImVec2(41, -35)), P(p + ImVec2(10, -66)), dark);
        d->AddRectFilled(P(p + ImVec2(-23, -25)), P(p + ImVec2(-4, 60)), dark, S(8));
        d->AddRectFilled(P(p + ImVec2(4, -25)), P(p + ImVec2(23, 60)), dark, S(8));
        d->AddLine(P(p + ImVec2(-55, -42)), P(p + ImVec2(62, -53)), edge, S(5));
        d->AddRectFilled(P(p + ImVec2(30, -58)), P(p + ImVec2(77, -49)), blue, S(2));
        d->AddCircle(P(p + ImVec2(0, -120)), S(10), blue, 0, S(2));
        d->AddLine(P(p + ImVec2(-20, -115)), P(p + ImVec2(20, -115)), blue, S(2));
        d->AddLine(P(p + ImVec2(-35, 62)), P(p + ImVec2(-4, 62)), edge, S(3));
        d->AddLine(P(p + ImVec2(4, 62)), P(p + ImVec2(35, 62)), edge, S(3));
    }

    void Players(ImDrawList* d, ImVec2 b, int& id)
    {
        static const Row enemy[] = {
            {"Enabled", NeverloseControl::Toggle, nullptr, 0, &visuals::esp_enabled},
            {"Offscreen Arrow", NeverloseControl::Color, nullptr, 0},
            {"Sounds", NeverloseControl::Color, nullptr, 0}
        };
        static const Row model[] = {
            {"Player", NeverloseControl::Select, "Water Flow", 0},
            {"Behind Walls", NeverloseControl::Select, "Glow Outline", 0},
            {"On Shot", NeverloseControl::Select, "Solid", 0},
            {"History", NeverloseControl::Select, "Solid", 0},
            {"Ragdolls", NeverloseControl::Disabled, nullptr, 0},
            {"Soul Particles", NeverloseControl::Color, nullptr, 0},
            {"Glow", NeverloseControl::Color, nullptr, 0}
        };
        ImVec2 a = b + ImVec2(177, 86), m = b + ImVec2(177, 239);
        Card(d, a, ImVec2(300, 116), "ENEMY");
        Card(d, m, ImVec2(300, 264), "ENEMY MODEL");
        DrawRows(d, a, 300, enemy, 3, id);
        DrawRows(d, m, 300, model, 7, id);
        Text(d, b + ImVec2(557, 75), C(111, 167, 255), "Enemies", kTextControl);
        Text(d, b + ImVec2(654, 75), C(180, 184, 194), MENU_ICON_USER, kTextIcon);
        Text(d, b + ImVec2(706, 75), C(180, 184, 194), MENU_ICON_LIST, kTextIcon);
        Soldier(d, b + ImVec2(626, 438));
        Text(d, b + ImVec2(609, 116), C(255, 112, 135), "C4", 8);
        Text(d, b + ImVec2(596, 128), C(253, 213, 90), "Neverlose", 9);
        Text(d, b + ImVec2(612, 141), C(227, 231, 241), "65%", 8);
        d->AddRectFilled(P(b + ImVec2(535, 495)), P(b + ImVec2(715, 499)), C(34, 38, 48), S(2));
        d->AddRectFilled(P(b + ImVec2(535, 495)), P(b + ImVec2(640, 499)), C(75, 126, 255), S(2));
    }

    void World(ImDrawList* d, ImVec2 b, int& id)
    {
        static const Row view_rows[] = {
            {"View Options", NeverloseControl::Chevron, nullptr, 0},
            {"Scope Options", NeverloseControl::Chevron, nullptr, 0},
            {"Viewmodel Options", NeverloseControl::Chevron, nullptr, 0},
            {"Perspective Options", NeverloseControl::Chevron, nullptr, 0},
            {"Unlock Spectating", NeverloseControl::Select, "Perspective, Enem", 0},
            {"Visual Recoil", NeverloseControl::Select, "No Shake, No Rec", 0}
        };
        static const Row hud[] = {
            {"Radar", NeverloseControl::Select, "Reveal Enemies, R", 0},
            {"Scope Overlay", NeverloseControl::Color, nullptr, 0},
            {"Inaccuracy Overlay", NeverloseControl::Color, nullptr, 0},
            {"Death Notices", NeverloseControl::Chevron, nullptr, 0},
            {"Scoreboard", NeverloseControl::Chevron, nullptr, 0},
            {"Crosshairs", NeverloseControl::Chevron, nullptr, 0}
        };
        static const Row esp[] = {
            {"Bomb", NeverloseControl::Chevron, nullptr, 0},
            {"Weapons", NeverloseControl::Chevron, nullptr, 0},
            {"Hostages", NeverloseControl::Chevron, nullptr, 0},
            {"Grenades", NeverloseControl::Chevron, nullptr, 0},
            {"Grenade Trajectory", NeverloseControl::Toggle, nullptr, 0, &visuals::visualize_grenade_trajectory},
            {"Grenade Proximity Warnings", NeverloseControl::Toggle, nullptr, 0}
        };
        static const Row misc[] = {
            {"Windows", NeverloseControl::Chevron, nullptr, 0},
            {"Removals", NeverloseControl::Chevron, nullptr, 0},
            {"Ambience", NeverloseControl::Chevron, nullptr, 0},
            {"Hit Marker", NeverloseControl::Chevron, nullptr, 0},
            {"Bullet Tracers", NeverloseControl::Chevron, nullptr, 0},
            {"Bullet Impacts", NeverloseControl::Color, nullptr, 0}
        };
        ImVec2 a = b + ImVec2(177, 84), c = b + ImVec2(478, 84), e = b + ImVec2(177, 350), g = b + ImVec2(478, 350);
        Card(d, a, ImVec2(291, 221), "VIEW");
        Card(d, c, ImVec2(257, 221), "HUD");
        Card(d, e, ImVec2(291, 221), "WORLD ESP");
        Card(d, g, ImVec2(257, 221), "MISCELLANEOUS");
        DrawRows(d, a, 291, view_rows, 6, id);
        DrawRows(d, c, 257, hud, 6, id);
        DrawRows(d, e, 291, esp, 6, id);
        DrawRows(d, g, 257, misc, 6, id);
    }

    void WeaponCell(ImDrawList* d, ImVec2 p, ImVec2 size, int kind, ImU32 accent)
    {
        d->AddRectFilled(P(p), P(p + size), C(24, 31, 45, 235), S(9));
        d->AddRect(P(p), P(p + size), C(35, 45, 62), S(9));
        d->AddRectFilled(P(p + ImVec2(0, size.y - 3)), P(p + size), accent, 0);
        const ImU32 gun = C(218, 220, 218), shade = C(104, 111, 118);
        const float s = ImMin(size.x / 121.0f, size.y / 91.0f);
        auto q = [&](float x, float y) { return p + ImVec2(x * s, y * s); };
        if (kind % 3 == 0) {
            d->AddRectFilled(P(q(25, 28)), P(q(86, 36)), gun, S(2 * s));
            d->AddRectFilled(P(q(67, 35)), P(q(77, 60)), shade, S(2 * s));
            d->AddRectFilled(P(q(32, 35)), P(q(41, 55)), gun, S(2 * s));
        }
        else if (kind % 3 == 1) {
            d->AddLine(P(q(18, 55)), P(q(94, 27)), gun, S(7 * s));
            d->AddRectFilled(P(q(70, 27)), P(q(90, 35)), shade, S(2 * s));
        }
        else {
            d->AddRectFilled(P(q(16, 32)), P(q(96, 39)), gun, S(2 * s));
            d->AddRectFilled(P(q(30, 39)), P(q(42, 61)), shade, S(2 * s));
            d->AddRectFilled(P(q(67, 39)), P(q(77, 57)), gun, S(2 * s));
        }
    }

    void Inventory(ImDrawList* d, ImVec2 b, int&)
    {
        d->AddRectFilled(P(b + ImVec2(159, 57)), P(b + ImVec2(747, 575)), C(17, 27, 42, 155), 0);
        d->AddRectFilled(P(b + ImVec2(185, 66)), P(b + ImVec2(286, 91)), C(20, 34, 50), S(8));
        TextY(d, b.x + 199, b.y + 66, 25, C(138, 192, 255), "CT Loadout", kTextControl);
        Soldier(d, b + ImVec2(264, 388));
        const ImU32 accents[] = { C(235, 237, 239), C(171, 70, 255), C(75, 116, 255), C(226, 43, 192), C(251, 65, 83), C(134, 72, 255) };
        for (int r = 0; r < 5; ++r)
            for (int c = 0; c < 3; ++c)
                WeaponCell(d, b + ImVec2(390 + c * 116.0f, 88 + r * 96.0f), ImVec2(108, 91), r * 3 + c, accents[(r + c) % 6]);
        Text(d, b + ImVec2(424, 67), C(205, 209, 219), "Pistols", kTextControl);
        Text(d, b + ImVec2(536, 67), C(205, 209, 219), "Mid-Tier", kTextControl);
        Text(d, b + ImVec2(655, 67), C(205, 209, 219), "Rifles", kTextControl);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 3; ++c)
                WeaponCell(d, b + ImVec2(171 + c * 65.0f, 470 + r * 51.0f), ImVec2(55, 47), r * 3 + c, accents[(r + c + 2) % 6]);
    }

    void Misc(ImDrawList* d, ImVec2 b, int& id)
    {
        static const Row movement[] = {
            {"Bunny Hop", NeverloseControl::Toggle, nullptr, 0},
            {"Air Strafe", NeverloseControl::Toggle, nullptr, 0},
            {"Jump Bug", NeverloseControl::Toggle, nullptr, 0},
            {"Standalone Quick Stop", NeverloseControl::Toggle, nullptr, 0},
            {"Strafe Assist", NeverloseControl::Toggle, nullptr, 0},
            {"Edge Jump", NeverloseControl::Toggle, nullptr, 0},
            {"Slow Walk", NeverloseControl::Toggle, nullptr, 0},
            {"Fast Ladder", NeverloseControl::Toggle, nullptr, 0}
        };
        static const Row features[] = {
            {"Quick Switch", NeverloseControl::Toggle, nullptr, 0},
            {"Super Toss", NeverloseControl::Toggle, nullptr, 0},
            {"Knife Bot", NeverloseControl::Disabled, nullptr, 0},
            {"Prevent AFK Kick", NeverloseControl::Toggle, nullptr, 0},
            {"Hit Sound", NeverloseControl::Toggle, nullptr, 0},
            {"Automatic Purchase", NeverloseControl::Toggle, nullptr, 0},
            {"Automatic Grenade Release", NeverloseControl::Toggle, nullptr, 0},
            {"Auto-Accept Matchmaking", NeverloseControl::Toggle, nullptr, 0},
            {"Log Events", NeverloseControl::Select, "Damage Dealt, Da", 0}
        };
        ImVec2 a = b + ImVec2(171, 80), c = b + ImVec2(481, 80);
        Card(d, a, ImVec2(300, 312), "MOVEMENT");
        Card(d, c, ImVec2(254, 349), "FEATURES");
        DrawRows(d, a, 300, movement, 8, id);
        DrawRows(d, c, 254, features, 9, id);
    }

    void AccountPopover(ImDrawList* d, ImVec2 b)
    {
        const float open = Motion(ImGui::GetID("##account_open"), state.account ? 1.0f : 0.0f, 17.0f, 0.0f);
        if (open < .002f) return;
        const ImVec2 p = b + ImVec2(151, 380), size(218, 181);
        const int first = d->VtxBuffer.Size;
        d->AddRectFilled(P(p - ImVec2(4, 2)), P(p + size + ImVec2(4, 7)), C(0, 0, 0, 65), S(18));
        d->AddRectFilled(P(p), P(p + size), C(24, 25, 34, 245), S(16));
        d->AddRect(P(p), P(p + size), C(48, 51, 64), S(16));
        Avatar(d, p + ImVec2(17, 16), 36.0f, 18.0f);
        Text(d, p + ImVec2(64, 15), C(229, 231, 237), "XPTNotFound", kTextBody);
        Text(d, p + ImVec2(64, 34), C(115, 164, 255), "Renew subscription", kTextControl);
        const char* rows[] = { "Language", "Menu Scale", "ESP Scale", "Synchronization" };
        for (int i = 0; i < 4; ++i)
        {
            float y = p.y + 66 + i * 28;
            TextY(d, p.x + 18, y, 28, C(185, 188, 198), rows[i], kTextControl);
            if (i < 3) Chevron(d, ImVec2(p.x + 195, y + 11), C(150, 154, 165));
        }
        Toggle(d, 190, p + ImVec2(169, 150));
        for (int i = first; i < d->VtxBuffer.Size; ++i)
        {
            ImDrawVert& v = d->VtxBuffer[i];
            const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
            v.col = (v.col & 0xffffffu) | ((ImU32)(a * open) << IM_COL32_A_SHIFT);
        }
    }
} // namespace

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
            ImGui::SetCursorScreenPos(p);
            const bool click = ImGui::InvisibleButton("##launch", size);
            const ImGuiID id = ImGui::GetItemID();
            const float hover = Motion(id ^ 0x51a0u, ImGui::IsItemHovered() ? 1.0f : 0.0f, 18.0f);
            ImDrawList* d = ImGui::GetWindowDrawList();

            d->AddRectFilled(p, p + size, Mix(C(20, 22, 31, 225), C(27, 30, 41, 245), hover), 8.0f);
            d->AddRect(p, p + size, Mix(C(40, 44, 56), C(75, 126, 255), hover), 8.0f);
            d->AddRectFilled(p + ImVec2(11.0f, size.y * 0.5f - 4.0f), p + ImVec2(19.0f, size.y * 0.5f + 4.0f),
                             C(75, 126, 255), 2.0f);
            d->AddText(ImGui::GetFont(), 14.0f, p + ImVec2(27.0f, size.y * 0.5f - 7.0f), C(220, 223, 231), "MENU");
            if (click) *open = true;
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    void render(bool* open, const ImVec2& origin, float scale)
    {
        if (open == nullptr || !*open)
        {
            state.open_mix = 0.0f;
            state.account = false;
            state.popup.open = false;
            state.popup_rect_min = ImVec2(0, 0);
            state.popup_rect_max = ImVec2(0, 0);
            return;
        }
        if (scale <= 0.0f) return;

        view.origin = origin;
        view.scale = scale;

        ImGuiIO& io = ImGui::GetIO();
        state.open_mix = ImLerp(state.open_mix, 1.0f, 1.0f - std::exp(-18.0f * io.DeltaTime));
        if (state.open_mix > 0.999f) state.open_mix = 1.0f;

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (state.open_mix < 0.95f) flags |= ImGuiWindowFlags_NoInputs;

        if (ImGui::Begin("Neverlose Menu", nullptr, flags))
        {
            ImDrawList* d = ImGui::GetWindowDrawList();
            const ImVec2 b(0.0f, 0.0f);

            // Blur replacement: dim the scene under the menu.
            d->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, C(9, 11, 18, 110));

            const int shell_first = d->VtxBuffer.Size;
            d->AddRectFilled(P(b), P(b + kShell), C(13, 15, 22, 222), S(14));
            Sidebar(d, b);
            Toolbar(d, b, open);

            const int content_first = d->VtxBuffer.Size;
            int id = 0;
            ImGui::PushID(static_cast<int>(state.page));
            switch (state.page)
            {
            case NeverlosePage::Rage: Rage(d, b, id); break;
            case NeverlosePage::Legit: Legit(d, b, id); break;
            case NeverlosePage::Players: Players(d, b, id); break;
            case NeverlosePage::World: World(d, b, id); break;
            case NeverlosePage::Inventory: Inventory(d, b, id); break;
            case NeverlosePage::Miscellaneous: Misc(d, b, id); break;
            }
            ImGui::PopID();

            state.page_mix = ImLerp(state.page_mix, 1.0f, 1.0f - std::exp(-15.0f * io.DeltaTime));
            const float pe = 1.0f - std::pow(1.0f - state.page_mix, 3.0f);
            for (int i = content_first; i < d->VtxBuffer.Size; ++i)
            {
                ImDrawVert& v = d->VtxBuffer[i];
                v.pos.x += S(9.0f) * (1.0f - pe);
                const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
                v.col = (v.col & 0xffffffu) | ((ImU32)(a * pe) << IM_COL32_A_SHIFT);
            }

            AccountPopover(d, b);
            PopupLayer(d);

            if (state.open_mix < 0.999f)
            {
                const float e = 1.0f - std::pow(1.0f - state.open_mix, 4.0f);
                const float sc = ImLerp(.92f, 1.0f, e);
                const ImVec2 pivot = P(kCanvas * 0.5f);
                for (int i = shell_first; i < d->VtxBuffer.Size; ++i)
                {
                    ImDrawVert& v = d->VtxBuffer[i];
                    v.pos = pivot + (v.pos - pivot) * sc + ImVec2(0.0f, S(16.0f) * (1.0f - e));
                    const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
                    v.col = (v.col & 0xffffffu) | ((ImU32)(a * e) << IM_COL32_A_SHIFT);
                }
            }

            // iOS has no Escape: a tap on the dim area closes the menu.
            if (ImGui::IsMouseReleased(0) && state.open_mix > 0.95f)
            {
                const ImVec2 mp = io.MousePos;
                const ImVec2 mc((mp.x - origin.x) / scale, (mp.y - origin.y) / scale);
                const bool in_shell = mc.x >= 0.0f && mc.y >= 0.0f && mc.x <= kShell.x && mc.y <= kShell.y;
                const bool in_popup = state.popup_rect_max.x > state.popup_rect_min.x &&
                                      mc.x >= state.popup_rect_min.x && mc.y >= state.popup_rect_min.y &&
                                      mc.x <= state.popup_rect_max.x && mc.y <= state.popup_rect_max.y;
                if (!in_shell && !in_popup) *open = false;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) *open = false;
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
} // namespace menu
