// Menu source watermark: @use_sberbank
#include "Menu/launcher.h"

#include "Menu/draw.h"
#include "Menu/fonts.h"
#include "Menu/style.h"
#include "Menu/widgets.h"

namespace nl {

LauncherRect launcher_button()
{
    LauncherRect out;
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mn(20.0f, 58.0f);
    const ImVec2 mx(mn.x + 86.0f, mn.y + 34.0f);

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(2);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiID id = ImGui::GetID("##open_menu");
    const ui::ItemState st = ui::item_behavior(id, mn, mx);
    const float hover = ui::animate(id + 1, st.hovered ? 1.0f : 0.0f, 18.0f);

    // The pill keeps its size when the menu scales: draw it at the reference scale.
    const float saved_scale = style::ui_scale;
    style::ui_scale = 1.0f;

    const float rounding = 9.0f;
    dl->AddRectFilled(mn, mx, IM_COL32(13, 14, 19, 216), rounding);
    draw::border(dl, mn, mx, draw::lerp_color(style::col::main_border, style::col::accent, hover * 0.7f), rounding,
                 1.0f);

    const float cy = (mn.y + mx.y) * 0.5f;
    draw::icon(dl, fonts().icons, 12.0f, ImVec2(mn.x + 19.0f, cy), style::col::accent, icon::gear);

    ImFont* font = fonts().medium;
    const ImU32 text_col = draw::lerp_color(style::col::preset_text, IM_COL32_WHITE, hover);
    const float cap = draw::cap_height(font, 13.0f);
    draw::text(dl, font, 13.0f, ImVec2(mn.x + 31.0f, cy - cap * 0.5f), text_col, "Menu");

    style::ui_scale = saved_scale;

    out.x = mn.x;
    out.y = mn.y;
    out.w = mx.x - mn.x;
    out.h = mx.y - mn.y;
    out.pressed = st.pressed;

    ImGui::End();
    return out;
}

} // namespace nl
