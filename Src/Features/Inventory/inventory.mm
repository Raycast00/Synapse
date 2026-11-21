#include "inventory.h"
#include "imgui.h"
#include "il2cpp/il2cpp.h"

#include "../../Render/GlassBackdrop.h"

#include <vector>

static bool  g_viewer_open = false;
static CGRect g_viewer_rect = CGRectZero;

// 0 = кнопка меню, 1 = само меню (см. main.mm), 2 = окно вьюера — слот закреплён за инвентарём
static const int kViewerBackdropSlot = 2;

void inventory::run()
{
    // lol
}

void inventory::draw_tab()
{
    if (ImGui::Button(g_viewer_open ? "Close Viewer" : "Open Viewer"))
        g_viewer_open = !g_viewer_open;
}

void inventory::draw_viewer()
{
    if (!g_viewer_open) {
        GlassBackdrop_Set(kViewerBackdropSlot, CGRectZero, 0, false);
        g_viewer_rect = CGRectZero;
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 half(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    ImGui::SetNextWindowSize(half, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 old_bg = style.Colors[ImGuiCol_WindowBg];
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.03f, 0.03f, 0.55f); // темнее основного меню, почти чёрный

    ImGui::Begin("##inventory_viewer", &g_viewer_open, ImGuiWindowFlags_NoCollapse);

    style.Colors[ImGuiCol_WindowBg] = old_bg; // возвращаем стиль сразу после Begin

    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    float rd = style.WindowRounding;
    GlassBackdrop_Set(kViewerBackdropSlot, CGRectMake(wp.x, wp.y, ws.x, ws.y), rd, true);
    g_viewer_rect = CGRectInset(CGRectMake(wp.x, wp.y, ws.x, ws.y), -8, -8);

    // содержимое пока не добавляем, сюда позже встанет сетка скинов/список
    ImGui::TextDisabled("Empty for now");

    ImGui::End();

    if (!g_viewer_open) {
        // окно закрыли его собственным крестиком в этом же кадре
        GlassBackdrop_Set(kViewerBackdropSlot, CGRectZero, 0, false);
        g_viewer_rect = CGRectZero;
    }
}

CGRect inventory::viewer_hit_rect()
{
    return g_viewer_rect;
}