#pragma once
#include "imgui.h"

namespace Glass {

// Вызывать сразу после ImGui::Begin(). Рисует блик по верхней части окна с учётом скругления.
inline void Gloss(ImVec2 p0, ImVec2 p1, float rounding)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p0, p1, false);                       // иначе блик обрежется по внутренней области окна
    for (int i = 0; i < 8; ++i) {
        dl->PushClipRect(p0, ImVec2(p1.x, p0.y + (i + 1) * 9.f), true);
        dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 4), rounding);
        dl->PopClipRect();
    }
    dl->PopClipRect();
}

} // namespace Glass