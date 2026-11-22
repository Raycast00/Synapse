#pragma once
#include "GlassWidgets.h"

namespace Glass {

// Размеры в единицах размера шрифта, чтобы попап масштабировался вместе с меню.
struct PopupConfig {
    float width         = 16.0f;
    float pad           = 0.8f;
    float rounding      = 1.1f;
    float title_scale   = 1.25f;
    float title_gap     = 0.5f;
    float section_gap   = 0.55f;
    float section_pad_x = 0.7f;
    float section_pad_y = 0.2f;
    float section_round = 0.8f;
    float row_h         = 2.3f;
    float switch_h      = 1.35f;
    float fade_time     = 0.16f;   // секунд на полное появление/исчезновение
    int   shadow_steps  = 10;
    float shadow_alpha  = 0.40f;
    ImVec4 tint   = ImVec4(0.20f, 0.20f, 0.21f, 0.45f);   // цвет стекла поверх блюра
    ImVec4 border = ImVec4(1.f, 1.f, 1.f, 0.20f);
};
inline PopupConfig& PopupCfg() { static PopupConfig c; return c; }

// То, что нужно main.mm, чтобы нарисовать слой попапа и блюр под ним.
struct PopupVisual {
    bool   visible = false;
    ImVec2 pos  = ImVec2(0, 0);
    ImVec2 size = ImVec2(0, 0);
    float  rounding = 0.f;
    float  alpha = 0.f;                 // уже со сглаживанием
    ImDrawList* draw_list = nullptr;    // draw list окна попапа
};

namespace detail {
struct SectionState { ImVec2 min = ImVec2(0, 0); int rows = 0; int count = 0; };
inline SectionState& Section() { static SectionState s; return s; }
}

// true, пока попап открыт: тогда оверлей ловит тапы по всему экрану, чтобы тап мимо закрывал попап
inline bool PopupCapturesInput()
{
    const detail::PopupState& P = detail::Popup();
    return P.open && P.alpha > 0.f;
}

// Вызывать сразу после Toggle(..., true), который открывает попап (и до следующего тумблера с точками).
// Возвращает true, пока попап виден; тогда добавляй секции и обязательно вызови EndTogglePopup().
inline bool BeginTogglePopup()
{
    detail::PopupState& P = detail::Popup();
    const PopupConfig& C = PopupCfg();
    ImGuiIO& io = ImGui::GetIO();

    if (P.last_toggle_id == 0 || P.id != P.last_toggle_id) return false;   // не наш попап
    if (!P.open && P.alpha <= 0.f) { P.id = 0; return false; }

    const int frame = ImGui::GetFrameCount();
    P.alpha = ImClamp(P.alpha + (P.open ? 1.f : -1.f) * io.DeltaTime / C.fade_time, 0.f, 1.f);
    if (!P.open && P.alpha <= 0.f) { P.id = 0; return false; }

    // тап мимо попапа (по отпусканию) закрывает; кадр открытия пропускаем, иначе тот же тап закроет попап
    if (P.open && P.size.x > 0.f && P.opened_frame != frame && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const ImRect r(P.pos, ImVec2(P.pos.x + P.size.x, P.pos.y + P.size.y));
        if (!r.Contains(io.MousePos)) P.open = false;
    }
    P.submitted_frame = frame;

    const float u   = ImGui::GetFontSize();
    const float pad = C.pad * u;
    const float W   = C.width * u;

    // позиция: под точками, с поправкой на края экрана (по размеру с прошлого кадра)
    ImVec2 pos = P.anchor;
    if (P.size.y > 0.f) {
        const float margin = 0.5f * u;
        if (pos.x + W > io.DisplaySize.x - margin) pos.x = io.DisplaySize.x - margin - W;
        if (pos.x < margin) pos.x = margin;
        if (pos.y + P.size.y > io.DisplaySize.y - margin) {
            const float above = P.dots_rect.Min.y - 0.35f * u - P.size.y;
            pos.y = (above >= margin) ? above : ImMax(margin, io.DisplaySize.y - margin - P.size.y);
        }
    }
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, detail::Eased(P.alpha));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!P.open) flags |= ImGuiWindowFlags_NoInputs;      // во время исчезновения тапы не принимаем

    ImGui::Begin("##glass_popup", nullptr, flags);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());   // всегда над окном меню

    // 0 = фон попапа, 1 = фон секций, 2 = содержимое
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(3);
    dl->ChannelsSetCurrent(2);

    detail::Section() = detail::SectionState();

    const float title_size = u * C.title_scale;
    dl->AddText(ImGui::GetFont(), title_size, ImGui::GetCursorScreenPos(),
                ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.92f)), P.title);
    ImGui::Dummy(ImVec2(W - 2.f * pad, title_size + C.title_gap * u));
    return true;
}

inline bool BeginPopupSection()
{
    const PopupConfig& C = PopupCfg();
    detail::SectionState& S = detail::Section();
    const float u = ImGui::GetFontSize();
    if (S.count > 0) ImGui::Dummy(ImVec2(1.f, C.section_gap * u));
    S.min  = ImGui::GetCursorScreenPos();
    S.rows = 0;
    ImGui::Dummy(ImVec2(1.f, C.section_pad_y * u));
    return true;
}

// Строка в секции: название слева, переключатель справа, между строками тонкая линия.
// Нажатие работает по всей строке.
inline bool PopupToggle(const char* label, bool* v)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const PopupConfig& C = PopupCfg();
    detail::SectionState& S = detail::Section();
    const float u       = ImGui::GetFontSize();
    const float inner_w = (C.width - 2.f * C.pad) * u;
    const float row_h   = C.row_h * u;
    const float sw_h    = C.switch_h * u;
    const float sw_w    = sw_h * 1.9f;
    const float px      = C.section_pad_x * u;

    const ImGuiID id = window->GetID(label);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + inner_w, pos.y + row_h));
    ImGui::ItemSize(bb);

    bool pressed = false;
    if (ImGui::ItemAdd(bb, id)) {
        bool hovered, held;
        pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
        if (pressed) *v = !*v;

        ImDrawList* dl = window->DrawList;
        if (S.rows > 0) {
            const float inset = px * 0.35f;
            dl->AddLine(ImVec2(bb.Min.x + inset, bb.Min.y), ImVec2(bb.Max.x - inset, bb.Min.y),
                        ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.14f)), 1.f);
        }
        const char* end = ImGui::FindRenderedTextEnd(label);
        dl->AddText(ImVec2(bb.Min.x + px, bb.Min.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                    ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.92f)), label, end);

        const ImRect sw(ImVec2(bb.Max.x - px - sw_w, bb.Min.y + (row_h - sw_h) * 0.5f),
                        ImVec2(bb.Max.x - px,        bb.Min.y + (row_h + sw_h) * 0.5f));
        detail::DrawSwitch(dl, sw, detail::SwitchAnim(id, *v), hovered);
    }
    S.rows++;
    return pressed;
}

inline void EndPopupSection()
{
    const PopupConfig& C = PopupCfg();
    detail::SectionState& S = detail::Section();
    const float u = ImGui::GetFontSize();
    ImGui::Dummy(ImVec2(1.f, C.section_pad_y * u));

    const float inner_w = (C.width - 2.f * C.pad) * u;
    const ImVec2 p0 = S.min;
    const ImVec2 p1 = ImVec2(p0.x + inner_w, ImGui::GetCursorScreenPos().y);
    const float rd = C.section_round * u;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSetCurrent(1);
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.05f)), rd);
    dl->AddRect(p0, p1, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.16f)), rd, 0, 1.f);
    dl->ChannelsSetCurrent(2);
    S.count++;
}

inline void EndTogglePopup()
{
    detail::PopupState& P = detail::Popup();
    const PopupConfig& C = PopupCfg();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;

    if (!window->SkipItems) {
        const float u   = ImGui::GetFontSize();
        const float pad = C.pad * u;
        const ImVec2 p0 = window->Pos;
        const float  W  = C.width * u;
        const float  H  = (ImGui::GetCursorScreenPos().y - p0.y) + pad;   // высота по факту содержимого
        const ImVec2 p1 = ImVec2(p0.x + W, p0.y + H);
        const float  rd = C.rounding * u;

        dl->ChannelsSetCurrent(0);
        dl->PushClipRectFullScreen();

        // тень вокруг: кольца за пределами попапа, внутри попапа ничего не затемняется
        for (int i = 1; i <= C.shadow_steps; ++i) {
            const float k = 1.f - (float)i / (float)(C.shadow_steps + 1);
            dl->AddRect(ImVec2(p0.x - i, p0.y - i), ImVec2(p1.x + i, p1.y + i),
                        ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, C.shadow_alpha * k * k)), rd + (float)i, 0, 1.5f);
        }
        dl->AddRectFilled(p0, p1, ImGui::GetColorU32(C.tint), rd);

        // блик сверху: слои с клипом дают плавный градиент, который повторяет скругление
        const float band = ImMax(4.f, H * 0.07f);
        for (int i = 0; i < 6; ++i) {
            dl->PushClipRect(p0, ImVec2(p1.x, p0.y + band * (float)(i + 1)), true);
            dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.012f)), rd);
            dl->PopClipRect();
        }
        dl->AddRect(p0, p1, ImGui::GetColorU32(C.border), rd, 0, 1.f);
        dl->PopClipRect();

        P.pos  = p0;
        P.size = ImVec2(W, H);
        P.draw_list = dl;
        P.draw_list_frame = ImGui::GetFrameCount();
    }

    dl->ChannelsMerge();
    ImGui::End();
    ImGui::PopStyleVar(5);
}

// Один раз за кадр, после меню и до ImGui::Render().
inline PopupVisual PopupEndFrame()
{
    detail::PopupState& P = detail::Popup();
    const PopupConfig& C = PopupCfg();
    PopupVisual out;
    const int frame = ImGui::GetFrameCount();

    if (P.submitted_frame != frame) {
        // тумблер-владелец не нарисован (меню закрыли или сменили вкладку): закрываем сразу
        P.open = false; P.alpha = 0.f; P.id = 0;
        return out;
    }
    out.visible   = P.alpha > 0.f && P.size.x > 0.f && P.draw_list_frame == frame;
    out.pos       = P.pos;
    out.size      = P.size;
    out.rounding  = C.rounding * ImGui::GetFontSize();
    out.alpha     = detail::Eased(P.alpha);
    out.draw_list = P.draw_list;
    return out;
}

} // namespace Glass