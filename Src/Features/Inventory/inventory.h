#pragma once

namespace inventory
{
    void run();        // пер-кадровая логика фичи, пока пусто
    void draw_tab();    // вызывать внутри ImGui::BeginTabItem("Inventory"); рисует постоянный 3D-вьюпорт
}