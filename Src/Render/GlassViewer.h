#pragma once
#include "imgui.h"

namespace Glass {

// Пустая панель-вьюер, открывается на половину ширины и высоты окна меню,
// темнее основного стекла (почти чёрная), без цветного блика.
// Вызывать внутри таб-айтема, в котором должен появиться вьюер (сейчас Inventory).
// backdrop_slot: индекс слота в GlassBackdrop (сейчас 2, свободный после кнопки=0 и меню=1).
// Возвращает true, пока окно вьюера открыто (на будущее, если понадобится проверка снаружи).
inline bool ViewerPanel(const char* id, bool* open, int backdrop_slot)
{
    if (!*open) {
        GlassBackdrop_Set(backdrop_slot, CGRectZero, 0, false);
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 half(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    ImGui::SetNextWindowSize(half, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 old_bg = style.Colors[ImGuiCol_WindowBg];
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.03f, 0.03f, 0.55f); // темнее основного меню

    ImGui::Begin(id, open, ImGuiWindowFlags_NoCollapse);

    style.Colors[ImGuiCol_WindowBg] = old_bg; // возвращаем стиль сразу после Begin, чтобы не задеть дочерние окна/попапы

    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    float rd = style.WindowRounding;
    GlassBackdrop_Set(backdrop_slot, CGRectMake(wp.x, wp.y, ws.x, ws.y), rd, true);

    // содержимое пока не добавляем, сюда позже встанет сетка скинов/список
    ImGui::TextDisabled("Empty for now");

    ImGui::End();
    return true;
}

} // namespace Glass