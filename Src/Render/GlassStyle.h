#pragma once
#include "imgui.h"

#pragma once
#include "imgui.h"

inline void ApplyGlassStyle()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();

    // скругления как в первом main.mm; остальные остаются стандартными (после ScaleAllSizes)
    s.WindowRounding = 5.5f;
    s.FrameRounding  = 3.0f;
    s.WindowBorderSize = 1.f; s.FrameBorderSize = 0.f; s.TabBarBorderSize = 0.f;

    auto W = [](float a) { return ImVec4(1.f, 1.f, 1.f, a); };
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]   = ImVec4(0.30f, 0.30f, 0.30f, 0.40f);   // серый тинт поверх блюра
    c[ImGuiCol_ChildBg]    = W(0.04f);
    c[ImGuiCol_PopupBg]    = ImVec4(0.18f, 0.18f, 0.18f, 0.95f);   // попапы без блюра, поэтому почти непрозрачные
    c[ImGuiCol_Border]     = W(0.22f);
    c[ImGuiCol_Text]       = W(0.95f);
    c[ImGuiCol_TextDisabled] = W(0.45f);

    c[ImGuiCol_FrameBg] = W(0.10f); c[ImGuiCol_FrameBgHovered] = W(0.16f); c[ImGuiCol_FrameBgActive] = W(0.22f);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_TitleBgCollapsed] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_MenuBarBg] = c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = W(0.25f); c[ImGuiCol_ScrollbarGrabHovered] = W(0.35f); c[ImGuiCol_ScrollbarGrabActive] = W(0.45f);
    c[ImGuiCol_CheckMark] = W(0.95f);
    c[ImGuiCol_SliderGrab] = W(0.55f); c[ImGuiCol_SliderGrabActive] = W(0.80f);
    c[ImGuiCol_Button] = W(0.12f); c[ImGuiCol_ButtonHovered] = W(0.20f); c[ImGuiCol_ButtonActive] = W(0.28f);
    c[ImGuiCol_Header] = W(0.12f); c[ImGuiCol_HeaderHovered] = W(0.20f); c[ImGuiCol_HeaderActive] = W(0.28f);
    c[ImGuiCol_Separator] = W(0.18f); c[ImGuiCol_SeparatorHovered] = W(0.40f); c[ImGuiCol_SeparatorActive] = W(0.60f);
    c[ImGuiCol_ResizeGrip] = W(0.12f); c[ImGuiCol_ResizeGripHovered] = W(0.25f); c[ImGuiCol_ResizeGripActive] = W(0.40f);
    c[ImGuiCol_Tab] = W(0.08f); c[ImGuiCol_TabHovered] = W(0.20f); c[ImGuiCol_TabSelected] = W(0.22f);
    c[ImGuiCol_TabDimmed] = W(0.05f); c[ImGuiCol_TabDimmedSelected] = W(0.12f);
    c[ImGuiCol_TabSelectedOverline] = c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);
}