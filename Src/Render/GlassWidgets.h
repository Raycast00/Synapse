#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include <unordered_map>

namespace Glass {
namespace detail {

struct PopupState {
    bool    open  = false;                 // целевое состояние
    float   alpha = 0.f;                   // 0..1, меняется плавно
    ImGuiID id    = 0;                     // id кнопки-точек тумблера-владельца
    char    title[64] = {0};
    ImVec2  anchor = ImVec2(0, 0);         // куда ставить левый верх попапа
    ImRect  dots_rect;
    ImVec2  pos  = ImVec2(0, 0);           // фактический прямоугольник с прошлого кадра
    ImVec2  size = ImVec2(0, 0);
    int     submitted_frame = -1;
    int     opened_frame    = -1;
    ImGuiID last_toggle_id  = 0;           // последний Toggle с точками (для BeginTogglePopup)
    ImDrawList* draw_list = nullptr;
    int     draw_list_frame = -1;
    float   scale = 1.f;                 // масштаб попапа в этом кадре (анимация «выпрыгивания»)
    ImVec2  pivot = ImVec2(0, 0);        // точка, от которой масштабируем
    int     vtx_begin = 0;               // с какой вершины draw list попапа начинается его содержимое

    int     interacting_frame = -10;     // последний кадр, когда тянули слайдер (чтобы тап-отпускание вне попапа его не закрыл)
};
inline PopupState& Popup() { static PopupState s; return s; }

inline float Eased(float a) { a = ImClamp(a, 0.f, 1.f); return a * a * (3.f - 2.f * a); }

inline ImVec4 Lerp4(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

// 0 = выкл, 1 = вкл; анимация хранится по ImGui ID, на первом показе сразу в целевом состоянии
inline float SwitchAnim(ImGuiID id, bool on)
{
    static std::unordered_map<ImGuiID, float> s_anim;
    const float target = on ? 1.f : 0.f;
    auto it = s_anim.find(id);
    if (it == s_anim.end()) it = s_anim.emplace(id, target).first;
    float& t = it->second;
    const float dt = ImGui::GetIO().DeltaTime;
    if (dt > 0.f) t += (target - t) * ImMin(dt * 14.f, 1.f);
    else          t = target;
    if (ImFabs(t - target) < 0.001f) t = target;
    return t;
}

// Сам переключатель: тёмный трек и серая ручка выключено, светлый трек и тёмная ручка включено
inline void DrawSwitch(ImDrawList* dl, const ImRect& bb, float t, bool hovered)
{
    const float h = bb.GetHeight();
    const float r = h * 0.5f;
    ImVec4 track = Lerp4(ImVec4(0.16f, 0.16f, 0.16f, 0.92f), ImVec4(0.82f, 0.82f, 0.82f, 0.95f), t);
    const ImVec4 knob = Lerp4(ImVec4(0.55f, 0.55f, 0.55f, 1.f), ImVec4(0.14f, 0.14f, 0.14f, 1.f), t);
    if (hovered) track.w = ImMin(track.w + 0.05f, 1.f);

    dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(track), r);
    dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.28f)), r, 0, 1.f);

    const float knob_r  = r - ImMax(2.f, h * 0.12f);
    const float travel  = bb.GetWidth() - h;
    dl->AddCircleFilled(ImVec2(bb.Min.x + r + travel * t, bb.Min.y + r), knob_r, ImGui::GetColorU32(knob), 24);
}

inline ImVec2 PopupAnchorFor(const ImRect& dots)
{
    // чуть ниже и правее точек
    return ImVec2(dots.Min.x + dots.GetWidth() * 0.45f, dots.Max.y + ImGui::GetFontSize() * 0.35f);
}

inline void RequestPopup(ImGuiID id, const char* label, const ImRect& dots_bb)
{
    PopupState& P = Popup();
    if (P.open && P.id == id) { P.open = false; return; }   // повторный тап по точкам закрывает
    P.open = true;
    P.id   = id;
    const char* end = ImGui::FindRenderedTextEnd(label);
    ImStrncpy(P.title, label, ImMin((size_t)(end - label) + 1, sizeof(P.title)));
    P.dots_rect = dots_bb;
    P.anchor = PopupAnchorFor(dots_bb);
    P.opened_frame = ImGui::GetFrameCount();
}

} // namespace detail

// Тумблер. show_dots = true добавляет кнопку из трёх точек, она открывает попап
// (см. Glass::BeginTogglePopup в GlassPopup.h). Нажатие на текст тоже переключает.
inline bool Toggle(const char* label, bool* v, bool show_dots = false)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);

    const float h = ImGui::GetFrameHeight();
    const float w = h * 1.8f;
    const ImVec2 pos = window->DC.CursorPos;

    const float main_w = w + (label_size.x > 0.f ? style.ItemInnerSpacing.x + label_size.x : 0.f);
    const ImRect sw_bb(pos, ImVec2(pos.x + w, pos.y + h));
    const ImRect main_bb(pos, ImVec2(pos.x + main_w, pos.y + h));

    ImRect dots_bb;
    float total_w = main_w;
    if (show_dots) {
        const float gap = style.ItemInnerSpacing.x;
        dots_bb = ImRect(ImVec2(pos.x + main_w + gap, pos.y), ImVec2(pos.x + main_w + gap + h, pos.y + h));
        total_w = main_w + gap + h;
    }
    ImGui::ItemSize(ImVec2(total_w, h), style.FramePadding.y);

    bool pressed = false;
    if (ImGui::ItemAdd(main_bb, id)) {
        bool hovered, held;
        pressed = ImGui::ButtonBehavior(main_bb, id, &hovered, &held);
        if (pressed) *v = !*v;

        detail::DrawSwitch(window->DrawList, sw_bb, detail::SwitchAnim(id, *v), hovered);
        if (label_size.x > 0.f)
            ImGui::RenderText(ImVec2(sw_bb.Max.x + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y), label);
    }

    if (show_dots) {
        detail::PopupState& P = detail::Popup();
        const ImGuiID dots_id = ImHashStr("##dots", 0, id);
        P.last_toggle_id = dots_id;
        if (P.id == dots_id) {                      // попап следует за тумблером, если меню двигают
            P.dots_rect = dots_bb;
            P.anchor = detail::PopupAnchorFor(dots_bb);
        }
        if (ImGui::ItemAdd(dots_bb, dots_id)) {
            bool d_hov, d_held;
            const bool dots_pressed = ImGui::ButtonBehavior(dots_bb, dots_id, &d_hov, &d_held);

            ImDrawList* dl = window->DrawList;
            const float rr = h * 0.3f;
            const ImVec4 bg = d_held ? ImVec4(1.f, 1.f, 1.f, 0.22f)
                            : d_hov  ? ImVec4(1.f, 1.f, 1.f, 0.14f)
                                     : ImVec4(1.f, 1.f, 1.f, 0.07f);
            dl->AddRectFilled(dots_bb.Min, dots_bb.Max, ImGui::GetColorU32(bg), rr);
            dl->AddRect(dots_bb.Min, dots_bb.Max, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.22f)), rr, 0, 1.f);

            const ImVec2 c = dots_bb.GetCenter();
            const float dr = ImMax(1.5f, h * 0.075f);
            const float sp = h * 0.26f;
            for (int i = -1; i <= 1; ++i)
                dl->AddCircleFilled(ImVec2(c.x + sp * (float)i, c.y), dr, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.85f)), 12);

            if (dots_pressed) detail::RequestPopup(dots_id, label, dots_bb);
        }
    }
    return pressed;
}

} // namespace Glass