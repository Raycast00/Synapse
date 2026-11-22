#pragma once
#include "imgui.h"

namespace inventory
{
    void run();        // пер-кадровая логика, вызывать один раз в начале кадра (как сейчас)
    void draw_tab();    // вызывать внутри ImGui::BeginTabItem("Inventory")
    void end_frame();   // вызывать один раз после EndTabBar()/End() меню, до ImGui::Render()

    // Добавляет карточку в список. Рисуются в порядке добавления, максимум 3 в ряд,
    // дальше перенос на новую строку; сетка карточек скроллится отдельно от окна вьюера.
    // name:      маленькая метка в верхнем левом углу карточки
    // skin_name: крупное название снизу карточки
    // rarity:    подпись редкости под названием
    // color:     цвет полоски редкости снизу карточки, например IM_COL32(255, 196, 64, 255)
    // object:    текстура предпросмотра 3D-объекта; (ImTextureID)0, если рендерить пока нечего — область останется пустой
    void AddContainer(const char* name, const char* skin_name, const char* rarity, ImU32 color, ImTextureID object = (ImTextureID)0);
    void ClearContainers();
}