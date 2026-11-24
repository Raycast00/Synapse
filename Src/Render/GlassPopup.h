#pragma once
#include "GlassWidgets.h"
#include <string>
#include <cmath>
#include <cstring>

namespace Glass {

// Размеры в единицах размера шрифта, чтобы попап масштабировался вместе с меню.
struct PopupConfig {
    // окно попапа
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
    float popup_open_scale = 0.90f;    // с какого масштаба попап «выпрыгивает» (как list_open_scale у списка)
    int   shadow_steps  = 10;
    float shadow_alpha  = 0.40f;
    ImVec4 tint   = ImVec4(0.20f, 0.20f, 0.21f, 0.45f);   // цвет стекла поверх блюра
    ImVec4 border = ImVec4(1.f, 1.f, 1.f, 0.20f);

    // dropdown: кнопка в строке
    float dd_w          = 7.5f;    // ширина кнопки
    float dd_h          = 1.8f;
    float dd_round      = 0.5f;
    float dd_pad_x      = 0.7f;
    float dd_arrow_zone = 2.0f;    // место справа под стрелку
    float dd_fade_len   = 1.6f;    // длина растворения текста
    // dropdown: список
    float list_pad_x      = 0.8f;
    float list_pad_y      = 0.5f;
    float list_item_h     = 2.1f;
    float list_shift      = 1.4f;  // на сколько текст уезжает вправо под стрелку
    float list_round      = 1.0f;
    float list_open_scale = 0.90f; // с какого масштаба список «выскакивает»
    float list_anim       = 0.18f; // секунд
    ImVec4 list_tint = ImVec4(0.30f, 0.30f, 0.31f, 0.93f);

    float slider_w          = 7.5f;    // ширина дорожки
    float slider_h          = 0.5f;    // толщина дорожки
    float slider_speed      = 22.f;    // скорость плавного движения заливки (1/сек)
    float bubble_pad_x      = 0.65f;
    float bubble_pad_y      = 0.3f;
    float bubble_gap        = 0.35f;
    float bubble_round      = 0.5f;
    float bubble_open_scale = 0.85f;   // с какого масштаба окошко «выпрыгивает»
    float bubble_speed      = 14.f;    // скорость появления и исчезновения окошка (1/сек)
    bool  bubble_above      = true;    // окошко над слайдером (как на макете); false = под ним
    ImVec4 slider_track = ImVec4(0.f, 0.f, 0.f, 0.28f);
    ImVec4 slider_fill  = ImVec4(0.74f, 0.74f, 0.74f, 1.f);
    ImVec4 bubble_fill  = ImVec4(0.17f, 0.17f, 0.18f, 0.96f);
};
inline PopupConfig& PopupCfg() { static PopupConfig c; return c; }

// То, что нужно main.mm, чтобы нарисовать слой попапа и блюр под ним.
struct PopupVisual {
    bool   visible = false;
    ImVec2 pos  = ImVec2(0, 0);
    ImVec2 size = ImVec2(0, 0);
    float  rounding = 0.f;
    float  alpha = 0.f;                 // уже со сглаживанием
    ImDrawList* draw_list  = nullptr;   // окно попапа
    ImDrawList* draw_list2 = nullptr;   // окно списка dropdown, если он виден
};

namespace detail {

struct SectionState { ImVec2 min = ImVec2(0, 0); int count = 0; };
inline SectionState& Section() { static SectionState s; return s; }
inline ImVec2 PopupXform(const ImVec2& p)
{
    const PopupState& P = Popup();
    return ImVec2(P.pivot.x + (p.x - P.pivot.x) * P.scale, P.pivot.y + (P.pivot.y == P.pivot.y ? (p.y - P.pivot.y) * P.scale : 0.f));
}

struct DropdownState {
    ImGuiID id = 0;                     // id кнопки, которой принадлежит список
    bool    open  = false;              // целевое состояние
    float   alpha = 0.f;                // 0..1
    bool    blocking = false;           // снимок open на начало кадра: пока true, остальные виджеты попапа не реагируют
    int     opened_frame = -1;
    int     submitted_frame = -1;
    ImVec2  rect_min = ImVec2(0, 0);    // фактический (масштабированный) прямоугольник списка с прошлого кадра
    ImVec2  rect_max = ImVec2(0, 0);
    ImDrawList* draw_list = nullptr;
    int     draw_list_frame = -1;
};
inline DropdownState& Dropdown() { static DropdownState s; return s; }

inline void DropdownReset()
{
    DropdownState& D = Dropdown();
    D.open = false; D.alpha = 0.f; D.id = 0; D.blocking = false;
}

struct SliderAnim { float disp = 0.f; float bubble = 0.f; };   // отображаемая доля заливки и видимость окошка
inline SliderAnim& SliderState(ImGuiID id, float init_frac)
{
    static std::unordered_map<ImGuiID, SliderAnim> s;
    auto it = s.find(id);
    if (it == s.end()) { SliderAnim a; a.disp = init_frac; it = s.emplace(id, a).first; }
    return it->second;
}

// Округление до точности формата: "%.0f" -> целые, "%.1f" -> десятые. Формат без точности не округляем.
inline float RoundToFormat(float v, const char* fmt)
{
    if (!fmt) return v;
    const char* dot = strchr(fmt, '.');
    if (!dot || dot[1] < '0' || dot[1] > '9') return v;
    float scale = 1.f;
    for (int i = 0; i < dot[1] - '0'; ++i) scale *= 10.f;
    return roundf(v * scale) / scale;
}

// Заливка = пересечение таблетки-дорожки с полосой x <= fill_x. Строится путём без клипа,
// поэтому корректно масштабируется вместе с попапом и точно повторяет скругления дорожки.
inline void FillPillUpTo(ImDrawList* dl, const ImRect& tr, float fill_x, ImU32 col)
{
    const float r  = tr.GetHeight() * 0.5f;
    const float cy = tr.Min.y + r;
    const ImVec2 cl(tr.Min.x + r, cy), cr(tr.Max.x - r, cy);
    fill_x = ImClamp(fill_x, tr.Min.x, tr.Max.x);
    if (fill_x <= tr.Min.x + 0.5f) return;

    if (fill_x < cl.x) {                                   // только кусочек левой «шапки»
        const float phi = acosf(ImClamp((fill_x - cl.x) / r, -1.f, 1.f));
        dl->PathArcTo(cl, r, phi, IM_PI * 2.f - phi);
    } else if (fill_x <= cr.x) {                           // левая шапка и прямой участок
        dl->PathArcTo(cl, r, IM_PI * 0.5f, IM_PI * 1.5f);
        dl->PathLineTo(ImVec2(fill_x, cy - r));
        dl->PathLineTo(ImVec2(fill_x, cy + r));
    } else {                                               // дошли до правой «шапки»
        const float a = acosf(ImClamp((fill_x - cr.x) / r, -1.f, 1.f));
        dl->PathArcTo(cl, r, IM_PI * 0.5f, IM_PI * 1.5f);
        dl->PathArcTo(cr, r, IM_PI * 1.5f, IM_PI * 2.f - a);
        dl->PathArcTo(cr, r, a, IM_PI * 0.5f);
    }
    dl->PathFillConvex(col);
}

// Список dropdown: отдельное окно без фона, все рисуется вручную и масштабируется вокруг pivot.
inline bool DropdownList(ImGuiID id, const ImRect& box, bool* selected, const char* const* items, int count, bool multi)
{
    DropdownState& D = Dropdown();
    PopupState& P = Popup();
    const PopupConfig& C = PopupCfg();
    ImGuiIO& io = ImGui::GetIO();
    const int frame = ImGui::GetFrameCount();

    if (!P.open) D.open = false;       // попап закрывается, список вместе с ним

    // тап мимо списка (по отпусканию) закрывает только список; кадр открытия пропускаем
    if (D.open && D.opened_frame != frame && D.rect_max.x > D.rect_min.x && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const ImRect r(D.rect_min, D.rect_max);
        if (!r.Contains(io.MousePos)) D.open = false;
    }
    D.alpha = ImClamp(D.alpha + (D.open ? 1.f : -1.f) * io.DeltaTime / C.list_anim, 0.f, 1.f);
    if (!D.open && D.alpha <= 0.f) { D.id = 0; return false; }
    D.submitted_frame = frame;

    const float u = ImGui::GetFontSize();
    const float t = Eased(D.alpha);
    const float s = C.list_open_scale + (1.f - C.list_open_scale) * t;

    // раскладка без масштаба
    const float pad_x  = C.list_pad_x * u;
    const float pad_y  = C.list_pad_y * u;
    const float item_h = C.list_item_h * u;
    const float shift  = C.list_shift * u;
    float max_tw = 0.f;
    for (int i = 0; i < count; ++i) max_tw = ImMax(max_tw, ImGui::CalcTextSize(items[i]).x);
    const float W = ImMax(box.GetWidth() + 0.6f * u, pad_x * 2.f + max_tw + shift);
    const float H = pad_y * 2.f + item_h * (float)count;

    // список накрывает кнопку: первая строка встаёт на уровень кнопки, правый край чуть правее кнопки
    ImVec2 p0(box.Max.x + 0.3f * u - W, box.GetCenter().y - pad_y - item_h * 0.5f);
    const float margin = 0.5f * u;
    p0.x = ImClamp(p0.x, margin, ImMax(margin, io.DisplaySize.x - margin - W));
    p0.y = ImClamp(p0.y, margin, ImMax(margin, io.DisplaySize.y - margin - H));
    const ImVec2 p1(p0.x + W, p0.y + H);

    const ImVec2 pivot(box.Max.x, box.GetCenter().y);
    auto T = [&](const ImVec2& p) { return ImVec2(pivot.x + (p.x - pivot.x) * s, pivot.y + (p.y - pivot.y) * s); };
    const ImVec2 q0 = T(p0), q1 = T(p1);
    D.rect_min = q0;
    D.rect_max = q1;

    ImGui::SetNextWindowPos(q0, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(q1.x - q0.x, q1.y - q0.y), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * t);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoFocusOnAppearing;
    if (!D.open) flags |= ImGuiWindowFlags_NoInputs;     // пока исчезает, тапы не принимаем

    ImGui::Begin("##glass_dropdown", nullptr, flags);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());   // над окном попапа

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rd = C.list_round * u * s;

    // тень и фон списка
    dl->PushClipRectFullScreen();
    for (int i = 1; i <= 8; ++i) {
        const float k = 1.f - (float)i / 9.f;
        dl->AddRect(ImVec2(q0.x - i, q0.y - i), ImVec2(q1.x + i, q1.y + i),
                    ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.35f * k * k)), rd + (float)i, 0, 1.5f);
    }
    dl->AddRectFilled(q0, q1, ImGui::GetColorU32(C.list_tint), rd);
    dl->AddRect(q0, q1, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.20f)), rd, 0, 1.f);
    dl->PopClipRect();

    // пункты
    bool changed = false;
    ImFont* font = ImGui::GetFont();
    const float fsz = ImGui::GetFontSize() * s;
    for (int i = 0; i < count; ++i) {
        const ImVec2 r0(p0.x, p0.y + pad_y + item_h * (float)i);
        const ImVec2 r1(p1.x, r0.y + item_h);
        const ImRect rb(T(r0), T(r1));
        const ImGuiID iid = ImHashData(&i, sizeof(i), id);

        bool hov = false, held = false;
        if (ImGui::ItemAdd(rb, iid) && D.open) {
            if (ImGui::ButtonBehavior(rb, iid, &hov, &held)) {
                if (multi) selected[i] = !selected[i];
                else       for (int k = 0; k < count; ++k) selected[k] = (k == i);
                changed = true;
            }
        }
        if (hov || held) {
            const float inset = 0.35f * u * s;
            dl->AddRectFilled(ImVec2(q0.x + inset, rb.Min.y), ImVec2(q1.x - inset, rb.Max.y),
                              ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, held ? 0.10f : 0.06f)), 0.5f * u * s);
        }

        const float sh = SwitchAnim(iid, selected[i]);          // 0..1: сдвиг текста и появление стрелки
        const float cy = (r0.y + r1.y) * 0.5f;
        const ImVec2 tp = T(ImVec2(p0.x + pad_x + sh * shift, cy - ImGui::GetTextLineHeight() * 0.5f));
        dl->AddText(font, fsz, tp, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.95f)), items[i]);

        if (sh > 0.01f) {
            const float ax = p0.x + pad_x + 0.28f * u;
            const float aw = 0.2f * u, ah = 0.3f * u;
            const ImVec2 pts[3] = { T(ImVec2(ax - aw, cy - ah)), T(ImVec2(ax + aw, cy)), T(ImVec2(ax - aw, cy + ah)) };
            dl->AddPolyline(pts, 3, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.95f * sh)),
                            ImDrawFlags_None, ImMax(1.5f, u * 0.12f) * s);
        }
    }

    D.draw_list = dl;
    D.draw_list_frame = frame;
    ImGui::End();
    ImGui::PopStyleVar(5);
    return changed;
}

} // namespace detail

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
    detail::DropdownState& D = detail::Dropdown();
    const PopupConfig& C = PopupCfg();
    ImGuiIO& io = ImGui::GetIO();

    if (P.last_toggle_id == 0 || P.id != P.last_toggle_id) return false;   // не наш попап
    if (!P.open && P.alpha <= 0.f) { P.id = 0; return false; }

    const int frame = ImGui::GetFrameCount();
    P.alpha = ImClamp(P.alpha + (P.open ? 1.f : -1.f) * io.DeltaTime / C.fade_time, 0.f, 1.f);
    if (!P.open && P.alpha <= 0.f) { P.id = 0; return false; }

    // пока открыт список dropdown, остальные виджеты попапа тапы не принимают
    D.blocking = D.open;

    // тап мимо попапа (по отпусканию) закрывает; кадр открытия пропускаем, иначе тот же тап закроет попап;
    // пока открыт список dropdown, тап закрывает сначала его; пока тянут слайдер, попап не закрываем
    if (P.open && !D.open && P.size.x > 0.f && P.opened_frame != frame &&
        frame - P.interacting_frame > 1 && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
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

    // «выпрыгивание»: масштаб вокруг точки на границе попапа, ближайшей к кнопке с точками
    const float ease = detail::Eased(P.alpha);
    P.scale = C.popup_open_scale + (1.f - C.popup_open_scale) * ease;
    const bool flipped = (pos.y < P.anchor.y - 0.5f);
    P.pivot = ImVec2(ImClamp(P.dots_rect.GetCenter().x, pos.x, pos.x + W),
                     flipped ? pos.y + P.size.y : pos.y);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ease);
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

    // 0 = фон попапа, 1 = фон секций, 2 = содержимое, 3 = надстройки (окошко значения слайдера)
    ImDrawList* dl = ImGui::GetWindowDrawList();
    P.vtx_begin = dl->VtxBuffer.Size;      // всё, что нарисуем дальше, потом масштабируется
    dl->ChannelsSplit(4);
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
    S.min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(1.f, C.section_pad_y * u));
    return true;
}

// Строка в секции: название слева, переключатель справа. Нажатие работает по всей строке.
// Линии между строками ставятся вручную через Glass::PopupSeparator().
inline bool PopupToggle(const char* label, bool* v)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const PopupConfig& C = PopupCfg();
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
        bool hovered = false, held = false;
        if (!detail::Dropdown().blocking) {          // пока открыт список dropdown, строки не реагируют
            pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
            if (pressed) *v = !*v;
        }

        ImDrawList* dl = window->DrawList;
        const char* end = ImGui::FindRenderedTextEnd(label);
        dl->AddText(ImVec2(bb.Min.x + px, bb.Min.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                    ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.92f)), label, end);

        const ImRect sw(ImVec2(bb.Max.x - px - sw_w, bb.Min.y + (row_h - sw_h) * 0.5f),
                        ImVec2(bb.Max.x - px,        bb.Min.y + (row_h + sw_h) * 0.5f));
        detail::DrawSwitch(dl, sw, detail::SwitchAnim(id, *v), hovered);
    }
    return pressed;
}

// Тонкая линия на всю ширину секции. Занимает по высоте ровно толщину линии (1 px),
// так что секция по-прежнему подгоняется под содержимое автоматически.
inline void PopupSeparator()
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    const PopupConfig& C = PopupCfg();
    const float u       = ImGui::GetFontSize();
    const float inner_w = (C.width - 2.f * C.pad) * u;
    const float inset   = C.section_pad_x * u * 0.35f;
    const ImVec2 pos    = window->DC.CursorPos;

    ImGui::Dummy(ImVec2(inner_w, 1.f));
    window->DrawList->AddLine(ImVec2(pos.x + inset,           pos.y + 0.5f),
                              ImVec2(pos.x + inner_w - inset, pos.y + 0.5f),
                              ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.14f)), 1.f);
}

// Строка с выпадающим списком: название слева, кнопка справа.
// selected[i] = выбран ли пункт i. multi = true: можно выбрать несколько (тап по выбранному снимает выбор),
// multi = false: выбор одного. Возвращает true в кадре, когда выбор изменился.
// label должен быть уникальным среди строк попапа (при совпадении текста добавляй "##что-то").
inline bool PopupDropdown(const char* label, bool* selected, const char* const* items, int count, bool multi = true)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems || !selected || !items || count <= 0) return false;

    const PopupConfig& C = PopupCfg();
    detail::DropdownState& D = detail::Dropdown();
    const int frame = ImGui::GetFrameCount();
    const float u       = ImGui::GetFontSize();
    const float inner_w = (C.width - 2.f * C.pad) * u;
    const float row_h   = C.row_h * u;
    const float px      = C.section_pad_x * u;
    const ImGuiID id    = window->GetID(label);

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect row(pos, ImVec2(pos.x + inner_w, pos.y + row_h));
    ImGui::ItemSize(row);

    const float box_w = C.dd_w * u, box_h = C.dd_h * u;
    const ImRect box(ImVec2(row.Max.x - px - box_w, row.Min.y + (row_h - box_h) * 0.5f),
                     ImVec2(row.Max.x - px,         row.Min.y + (row_h + box_h) * 0.5f));

    ImDrawList* dl = window->DrawList;

    // название слева
    const char* lend = ImGui::FindRenderedTextEnd(label);
    if (lend > label)
        dl->AddText(ImVec2(row.Min.x + px, row.Min.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                    ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.92f)), label, lend);

    // когда открыт список, кнопка плавно гаснет под ним
    const float box_a = 1.f - ((D.id == id) ? detail::Eased(D.alpha) : 0.f);

    bool changed = false;
    bool hovered = false, held = false;
    if (ImGui::ItemAdd(box, id)) {
        if (!D.blocking) {
            if (ImGui::ButtonBehavior(box, id, &hovered, &held)) {
                D.open = true; D.id = id; D.opened_frame = frame;
            }
        }

        const float rr = C.dd_round * u;
        dl->AddRectFilled(box.Min, box.Max,
                          ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, (held ? 0.12f : hovered ? 0.09f : 0.06f) * box_a)), rr);
        dl->AddRect(box.Min, box.Max, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.28f * box_a)), rr, 0, 1.f);

        // текст: «Select», одно имя или имена через запятую
        std::string txt;
        for (int i = 0; i < count; ++i)
            if (selected[i]) { if (!txt.empty()) txt += ", "; txt += items[i]; }
        const bool none = txt.empty();
        if (none) txt = "Select";

        const float tx0 = box.Min.x + C.dd_pad_x * u;
        const float tx1 = box.Max.x - C.dd_arrow_zone * u;
        const float ty  = box.GetCenter().y - ImGui::GetTextLineHeight() * 0.5f;
        const float tw  = ImGui::CalcTextSize(txt.c_str()).x;

        const int v0 = dl->VtxBuffer.Size;
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx0, ty),
                    ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, (none ? 0.85f : 0.95f) * box_a)), txt.c_str());
        if (tw > tx1 - tx0) {
            // длинный текст растворяется к концу: альфа вершин убывает до нуля у границы
            const float fade_len = ImMax(1.f, ImMin(C.dd_fade_len * u, tx1 - tx0));
            for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
                ImDrawVert& vtx = dl->VtxBuffer[i];
                const float k = ImSaturate((tx1 - vtx.pos.x) / fade_len);
                if (k >= 1.f) continue;
                const ImU32 a = (vtx.col >> IM_COL32_A_SHIFT) & 0xFF;
                vtx.col = (vtx.col & ~IM_COL32_A_MASK) | ((ImU32)((float)a * k) << IM_COL32_A_SHIFT);
            }
        }

        // стрелка вниз
        const float ax = box.Max.x - C.dd_arrow_zone * u * 0.5f, ay = box.GetCenter().y;
        const float aw = 0.34f * u, ah = 0.18f * u;
        const ImVec2 pts[3] = { ImVec2(ax - aw, ay - ah), ImVec2(ax, ay + ah), ImVec2(ax + aw, ay - ah) };
        dl->AddPolyline(pts, 3, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.92f * box_a)),
                        ImDrawFlags_None, ImMax(1.5f, u * 0.12f));
    }

    if (D.id == id && (D.open || D.alpha > 0.f)) {
        // кнопка с учётом анимации попапа, чтобы список и попап закрывались согласованно
        const ImRect box_t(detail::PopupXform(box.Min), detail::PopupXform(box.Max));
        if (detail::DropdownList(id, box_t, selected, items, count, multi)) changed = true;
    }

    return changed;
}

inline bool PopupSlider(const char* label, float* v, float v_min, float v_max, const char* format = "%.0f")
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems || !v || !(v_max > v_min)) return false;

    const PopupConfig& C = PopupCfg();
    detail::PopupState& P = detail::Popup();
    ImGuiIO& io = ImGui::GetIO();
    const int frame = ImGui::GetFrameCount();
    const float u       = ImGui::GetFontSize();
    const float inner_w = (C.width - 2.f * C.pad) * u;
    const float row_h   = C.row_h * u;
    const float px      = C.section_pad_x * u;
    const ImGuiID id    = window->GetID(label);

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect row(pos, ImVec2(pos.x + inner_w, pos.y + row_h));
    ImGui::ItemSize(row);

    const float tw = C.slider_w * u, th = C.slider_h * u;
    const ImRect track(ImVec2(row.Max.x - px - tw, row.Min.y + (row_h - th) * 0.5f),
                       ImVec2(row.Max.x - px,      row.Min.y + (row_h + th) * 0.5f));
    const float pad_hit = 0.3f * u;       // область нажатия шире и выше самой тонкой дорожки
    const ImRect hit(ImVec2(track.Min.x - pad_hit, row.Min.y), ImVec2(track.Max.x + pad_hit, row.Max.y));

    ImDrawList* dl = window->DrawList;

    const char* lend = ImGui::FindRenderedTextEnd(label);
    if (lend > label)
        dl->AddText(ImVec2(row.Min.x + px, row.Min.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                    ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.92f)), label, lend);

    bool changed = false;
    if (ImGui::ItemAdd(hit, id)) {
        bool hovered = false, held = false;
        if (!detail::Dropdown().blocking) {          // пока открыт список dropdown, слайдер не реагирует
            ImGui::ButtonBehavior(hit, id, &hovered, &held);
            if (held) {
                P.interacting_frame = frame;
                const float t = ImSaturate((io.MousePos.x - track.Min.x) / track.GetWidth());
                float nv = detail::RoundToFormat(v_min + t * (v_max - v_min), format);
                nv = ImClamp(nv, v_min, v_max);
                if (nv != *v) { *v = nv; changed = true; }
            }
        }

        // плавное движение заливки и появление окошка
        const float target = ImSaturate((*v - v_min) / (v_max - v_min));
        detail::SliderAnim& A = detail::SliderState(id, target);
        A.disp += (target - A.disp) * ImMin(io.DeltaTime * C.slider_speed, 1.f);
        if (ImFabs(A.disp - target) < 0.0005f) A.disp = target;
        A.bubble += ((held ? 1.f : 0.f) - A.bubble) * ImMin(io.DeltaTime * C.bubble_speed, 1.f);
        if (!held && A.bubble < 0.003f) A.bubble = 0.f;

        // дорожка и заливка
        dl->AddRectFilled(track.Min, track.Max, ImGui::GetColorU32(C.slider_track), th * 0.5f);
        const float fill_x = track.Min.x + A.disp * track.GetWidth();
        detail::FillPillUpTo(dl, track, fill_x, ImGui::GetColorU32(C.slider_fill));

        // окошко со значением: над краем заливки, проявляется и «выпрыгивает»
        if (A.bubble > 0.001f) {
            const float bt = detail::Eased(A.bubble);
            const float bs = C.bubble_open_scale + (1.f - C.bubble_open_scale) * bt;

            char buf[48];
            ImFormatString(buf, sizeof(buf), format, *v);
            const ImVec2 ts = ImGui::CalcTextSize(buf);
            const float bw = ts.x + 2.f * C.bubble_pad_x * u;
            const float bh = ts.y + 2.f * C.bubble_pad_y * u;
            const float gap = C.bubble_gap * u;
            const float cx = ImClamp(fill_x, row.Min.x + bw * 0.5f, row.Max.x - bw * 0.5f);
            const float top = C.bubble_above ? track.Min.y - gap - bh : track.Max.y + gap;
            const ImVec2 pivot(cx, C.bubble_above ? top + bh : top);     // растёт от края, ближайшего к дорожке
            auto S = [&](float x, float y) { return ImVec2(pivot.x + (x - pivot.x) * bs, pivot.y + (y - pivot.y) * bs); };

            dl->ChannelsSetCurrent(3);              // поверх всего содержимого попапа
            dl->PushClipRectFullScreen();           // окошко может выходить за край попапа
            const float rr = C.bubble_round * u * bs;
            const ImVec4 bf = C.bubble_fill;
            dl->AddRectFilled(S(cx - bw * 0.5f, top), S(cx + bw * 0.5f, top + bh),
                              ImGui::GetColorU32(ImVec4(bf.x, bf.y, bf.z, bf.w * bt)), rr);
            dl->AddRect(S(cx - bw * 0.5f, top), S(cx + bw * 0.5f, top + bh),
                        ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.14f * bt)), rr, 0, 1.f);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * bs,
                        S(cx - ts.x * 0.5f, top + (bh - ts.y) * 0.5f),
                        ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.95f * bt)), buf);
            dl->PopClipRect();
            dl->ChannelsSetCurrent(2);
        }
    }
    return changed;
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

        P.pos  = p0;                       // прямоугольник без масштаба: по нему считаются тапы и позиция
        P.size = ImVec2(W, H);
        P.draw_list = dl;
        P.draw_list_frame = ImGui::GetFrameCount();
    }

    // «выпрыгивание»: масштабируем все вершины попапа (фон, тень, текст, переключатели) вокруг pivot
    if (P.scale < 0.9999f) {
        for (int i = P.vtx_begin; i < dl->VtxBuffer.Size; ++i) {
            ImDrawVert& v = dl->VtxBuffer[i];
            v.pos.x = P.pivot.x + (v.pos.x - P.pivot.x) * P.scale;
            v.pos.y = P.pivot.y + (v.pos.y - P.pivot.y) * P.scale;
        }
    }

    dl->ChannelsMerge();
    ImGui::End();
    ImGui::PopStyleVar(5);
}

inline PopupVisual PopupEndFrame()
{
    detail::PopupState& P = detail::Popup();
    detail::DropdownState& D = detail::Dropdown();
    const PopupConfig& C = PopupCfg();
    PopupVisual out;
    const int frame = ImGui::GetFrameCount();

    if (P.submitted_frame != frame) {
        // тумблер-владелец не нарисован (меню закрыли или сменили вкладку): закрываем сразу
        P.open = false; P.alpha = 0.f; P.id = 0;
        detail::DropdownReset();
        return out;
    }
    if (D.submitted_frame != frame && (D.open || D.alpha > 0.f))
        detail::DropdownReset();

    // блюр под попапом должен повторять его анимацию размера
    const float s = P.scale;
    out.visible    = P.alpha > 0.f && P.size.x > 0.f && P.draw_list_frame == frame;
    out.pos        = ImVec2(P.pivot.x + (P.pos.x - P.pivot.x) * s, P.pivot.y + (P.pos.y - P.pivot.y) * s);
    out.size       = ImVec2(P.size.x * s, P.size.y * s);
    out.rounding   = C.rounding * ImGui::GetFontSize() * s;
    out.alpha      = detail::Eased(P.alpha);
    out.draw_list  = P.draw_list;
    out.draw_list2 = (D.draw_list_frame == frame && D.alpha > 0.f) ? D.draw_list : nullptr;
    return out;
}

} // namespace Glass