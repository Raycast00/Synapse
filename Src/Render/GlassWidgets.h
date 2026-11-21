#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include <unordered_map>

namespace Glass {

// show_dots = true добавляет кнопку с тремя точками справа от лейбла.
// Пока ничего не открывает (под будущий попап с доп-настройками),
// но уже кликабельна и анимирует hover/press, чтобы не доделывать дважды.
inline bool Toggle(const char* label, bool* v, bool show_dots = false)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);

    const float toggle_h = ImGui::GetFrameHeight();
    const float toggle_w = toggle_h * 1.8f;
    const float radius   = toggle_h * 0.5f;

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect toggle_bb(pos, ImVec2(pos.x + toggle_w, pos.y + toggle_h));

    float total_w = toggle_w;
    if (label_size.x > 0.0f) total_w += style.ItemInnerSpacing.x + label_size.x;

    // место под кнопку с точками считается ДО ItemSize, чтобы войти в total_bb
    const float dots_w = toggle_h;                 // квадратная кнопка высотой с тумблер
    const float dots_gap = style.ItemInnerSpacing.x;
    ImRect dots_bb;
    if (show_dots) {
        dots_bb = ImRect(ImVec2(pos.x + total_w + dots_gap, pos.y),
                          ImVec2(pos.x + total_w + dots_gap + dots_w, pos.y + toggle_h));
        total_w += dots_gap + dots_w;
    }

    const ImRect total_bb(pos, ImVec2(pos.x + total_w, pos.y + ImMax(label_size.y, toggle_h) + style.FramePadding.y * 2.0f - toggle_h + toggle_h));
    ImGui::ItemSize(ImRect(pos, ImVec2(pos.x + total_w, pos.y + label_size.y + style.FramePadding.y * 2.0f)), style.FramePadding.y);

    bool toggle_pressed = false;
    if (ImGui::ItemAdd(toggle_bb, id)) {
        bool hovered, held;
        toggle_pressed = ImGui::ButtonBehavior(toggle_bb, id, &hovered, &held);
        if (toggle_pressed) *v = !(*v);

        // анимация положения ручки (0 = выкл, 1 = вкл), хранится по ImGui ID
        static std::unordered_map<ImGuiID, float> s_anim;
        float& t = s_anim[id];
        const float target = *v ? 1.0f : 0.0f;
        if (g.IO.DeltaTime > 0.0f)
            t += (target - t) * ImMin(g.IO.DeltaTime * 14.0f, 1.0f);
        else
            t = target;
        if (ImFabs(t - target) < 0.001f) t = target;

        const ImVec4 track_off(0.16f, 0.16f, 0.16f, 0.92f);
        const ImVec4 track_on (0.82f, 0.82f, 0.82f, 0.95f);
        const ImVec4 knob_off (0.55f, 0.55f, 0.55f, 1.0f);
        const ImVec4 knob_on  (0.14f, 0.14f, 0.14f, 1.0f);
        const ImVec4 border_c (1.0f, 1.0f, 1.0f, 0.28f);

        auto lerp4 = [](const ImVec4& a, const ImVec4& b, float x) {
            return ImVec4(a.x + (b.x - a.x) * x, a.y + (b.y - a.y) * x,
                           a.z + (b.z - a.z) * x, a.w + (b.w - a.w) * x);
        };
        ImVec4 track = lerp4(track_off, track_on, t);
        ImVec4 knob  = lerp4(knob_off, knob_on, t);
        if (hovered) track.w = ImMin(track.w + 0.05f, 1.0f);

        ImDrawList* dl = window->DrawList;
        dl->AddRectFilled(toggle_bb.Min, toggle_bb.Max, ImGui::ColorConvertFloat4ToU32(track), radius);
        dl->AddRect(toggle_bb.Min, toggle_bb.Max, ImGui::ColorConvertFloat4ToU32(border_c), radius, 0, 1.0f);

        const float knob_r  = radius - 2.5f;
        const float travel  = toggle_w - toggle_h;
        const float knob_cx = toggle_bb.Min.x + radius + travel * t;
        const float knob_cy = toggle_bb.Min.y + radius;
        dl->AddCircleFilled(ImVec2(knob_cx, knob_cy), knob_r, ImGui::ColorConvertFloat4ToU32(knob), 20);

        if (label_size.x > 0.0f)
            ImGui::RenderText(ImVec2(toggle_bb.Max.x + style.ItemInnerSpacing.x, toggle_bb.Min.y + style.FramePadding.y), label);
    }

    if (show_dots) {
        const ImGuiID dots_id = window->GetID((void*)((char*)nullptr + id + 1)); // стабильный отдельный id от toggle_id
        if (ImGui::ItemAdd(dots_bb, dots_id)) {
            bool d_hover, d_held;
            // кнопка кликабельна и анимируется, но ничего не делает — место под будущий попап
            ImGui::ButtonBehavior(dots_bb, dots_id, &d_hover, &d_held);

            ImU32 dots_bg = ImGui::ColorConvertFloat4ToU32(
                d_held  ? ImVec4(1.f, 1.f, 1.f, 0.22f) :
                d_hover ? ImVec4(1.f, 1.f, 1.f, 0.14f) :
                          ImVec4(1.f, 1.f, 1.f, 0.07f));
            ImDrawList* dl = window->DrawList;
            dl->AddRectFilled(dots_bb.Min, dots_bb.Max, dots_bg, radius * 0.6f);
            dl->AddRect(dots_bb.Min, dots_bb.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(1.f,1.f,1.f,0.20f)), radius * 0.6f, 0, 1.0f);

            const ImVec2 c = dots_bb.GetCenter();
            const float r = 1.6f;
            const float sp = 5.5f;
            ImU32 dot_col = ImGui::ColorConvertFloat4ToU32(ImVec4(1.f, 1.f, 1.f, 0.85f));
            dl->AddCircleFilled(ImVec2(c.x - sp, c.y), r, dot_col, 12);
            dl->AddCircleFilled(ImVec2(c.x,      c.y), r, dot_col, 12);
            dl->AddCircleFilled(ImVec2(c.x + sp, c.y), r, dot_col, 12);
        }
    }

    return toggle_pressed;
}

} // namespace Glass