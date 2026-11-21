#pragma once
#import <CoreGraphics/CoreGraphics.h>

namespace inventory
{
    void run();   // пер-кадровая логика фичи (как у visuals/aim/rage), пока пусто

    // UI вьюера
    void draw_tab();     // вызывать внутри ImGui::BeginTabItem("Inventory")
    void draw_viewer();  // вызывать раз в кадр снаружи меню, рисует окно вьюера, если оно открыто

    CGRect viewer_hit_rect(); // прямоугольник открытого окна для hitTest, CGRectZero если закрыто
}