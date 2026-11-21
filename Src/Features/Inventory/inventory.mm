#include "inventory.h"
#include "imgui.h"
#include "il2cpp/il2cpp.h"

#include <vector>

void inventory::run()
{
    // lol
}

void inventory::draw_tab()
{
    ImGuiStyle& style = ImGui::GetStyle();

    // половина доступной ширины вкладки, высота на всё свободное место
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 size(avail.x * 0.4f, avail.y);
    if (size.x < 1.f || size.y < 1.f) return; // окно ещё не раскрыто/свёрнуто, не рисуем нулевой регион

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + size.x, p0.y + size.y);
    float rounding = style.ChildRounding > 0.f ? style.ChildRounding : 8.f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(18, 18, 20, 235), rounding);              // тёмный, почти чёрный фон
    dl->AddRectFilledMultiColor(p0, ImVec2(p1.x, p0.y + size.y * 0.5f),          // лёгкий градиент сверху вниз для глубины
        IM_COL32(255, 255, 255, 10), IM_COL32(255, 255, 255, 10),
        IM_COL32(255, 255, 255, 0),  IM_COL32(255, 255, 255, 0));
    dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 35), rounding, 0, 1.f);          // тонкая светлая рамка

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0)); // фон уже нарисован вручную выше
    ImGui::BeginChild("##inventory_viewport", size, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // сюда позже встанет рендер 3D-объекта (например, через текстуру в ImGui::Image)
    ImGui::SetCursorPos(ImVec2(size.x * 0.5f - 40.f, size.y * 0.5f - 8.f));
    ImGui::TextDisabled("Empty for now");

    ImGui::EndChild();
    ImGui::PopStyleColor();
}