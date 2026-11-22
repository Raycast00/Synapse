#include "inventory.h"
#include "imgui.h"
#include "il2cpp/il2cpp.h"

#include "Render/GlassBackdrop.h"
#include "Render/GlassPanel.h"

#include <vector>
#include <string>

namespace {

struct Container {
    std::string name;
    std::string skin_name;
    std::string rarity;
    ImU32        color;
    ImTextureID  object;
};

std::vector<Container> g_containers;
bool g_drawn_this_frame = false;

// слот 0 = кнопка меню, слот 1 = само меню (см. main.mm), слот 2 = окно вьюера — закреплён за инвентарём
const int kViewerBackdropSlot = 2;

void draw_card(const Container& c, ImVec2 size)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + size.x, p0.y + size.y);
    const float rounding = 10.f;
    const float pad = 10.f;

    dl->AddRectFilled(p0, p1, IM_COL32(20, 20, 22, 235), rounding);
    dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 28), rounding, 0, 1.f);

    // служебное имя, верхний левый угол
    dl->AddText(ImVec2(p0.x + pad, p0.y + pad), IM_COL32(170, 170, 170, 170), c.name.c_str());

    // нижний блок снизу вверх: полоска редкости -> rarity -> skin_name
    const float bar_h = 3.f;
    float y = p1.y - pad;
    ImVec2 bar_p0(p0.x + pad, y - bar_h);
    ImVec2 bar_p1(p1.x - pad, y);

    // полоска-«свечение»: цвет редкости в центре, прозрачно по краям
    ImVec2 bar_mid(bar_p0.x + (bar_p1.x - bar_p0.x) * 0.5f, bar_p0.y);
    ImU32  transparent = c.color & 0x00FFFFFF; // обнуляем альфу (старший байт), сам цвет оставляем
    dl->AddRectFilledMultiColor(bar_p0, ImVec2(bar_mid.x, bar_p1.y), transparent, c.color, c.color, transparent);
    dl->AddRectFilledMultiColor(ImVec2(bar_mid.x, bar_p0.y), bar_p1, c.color, transparent, transparent, c.color);
    y = bar_p0.y - 4.f;

    ImVec2 rarity_sz = ImGui::CalcTextSize(c.rarity.c_str());
    y -= rarity_sz.y;
    dl->AddText(ImVec2(p0.x + pad, y), IM_COL32(190, 190, 190, 200), c.rarity.c_str());
    y -= 3.f;

    ImFont*     font = ImGui::GetFont();
    const float skin_font_size = ImGui::GetFontSize() * 1.3f;
    ImVec2      skin_sz = font->CalcTextSizeA(skin_font_size, FLT_MAX, 0.f, c.skin_name.c_str());
    y -= skin_sz.y;
    ImVec2 skin_pos(p0.x + pad, y);
    // имитация полужирного: тот же текст с небольшим сдвигом поверх самого себя
    dl->AddText(font, skin_font_size, ImVec2(skin_pos.x + 0.6f, skin_pos.y), IM_COL32_WHITE, c.skin_name.c_str());
    dl->AddText(font, skin_font_size, skin_pos, IM_COL32_WHITE, c.skin_name.c_str());

    // область предпросмотра объекта — всё, что осталось между именем сверху и названием снизу
    float obj_top = p0.y + pad + ImGui::GetFontSize() + pad;
    float obj_bottom = y - 6.f;
    if (c.object != (ImTextureID)0 && obj_bottom > obj_top) {
        dl->AddImageRounded(c.object, ImVec2(p0.x + pad, obj_top), ImVec2(p1.x - pad, obj_bottom),
                             ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.f);
    }
}

} // namespace

void inventory::run()
{
    g_drawn_this_frame = false; // сбрасывается раз в кадр, до рисования вкладок
}

void inventory::draw_tab()
{
    g_drawn_this_frame = true;
    ImGuiStyle& style = ImGui::GetStyle();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 viewer_size(avail.x * 0.4f, avail.y);
    if (viewer_size.x < 1.f || viewer_size.y < 1.f) return;

    // ---- окно вьюера (слева), со стеклом ----
    ImVec2 vp0 = ImGui::GetCursorScreenPos();
    ImVec2 vp1 = ImVec2(vp0.x + viewer_size.x, vp0.y + viewer_size.y);
    float  rounding = style.ChildRounding > 0.f ? style.ChildRounding : 8.f;

    GlassBackdrop_Set(kViewerBackdropSlot, CGRectMake(vp0.x, vp0.y, viewer_size.x, viewer_size.y), rounding, true);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // полупрозрачная тёмная заливка, чтобы сквозь неё было видно системный блюр
    dl->AddRectFilled(vp0, vp1, IM_COL32(14, 14, 16, 150), rounding);
    dl->AddRect(vp0, vp1, IM_COL32(255, 255, 255, 40), rounding, 0, 1.f);
    Glass::Gloss(vp0, vp1, rounding);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##inventory_viewport", viewer_size, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPos(ImVec2(viewer_size.x * 0.5f - 40.f, viewer_size.y * 0.5f - 8.f));
    ImGui::TextDisabled("Empty for now");
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine();

    // ---- сетка контейнеров (справа), скроллится отдельно от вьюера ----
    ImVec2 grid_size(ImGui::GetContentRegionAvail().x, avail.y);
    ImGui::BeginChild("##inventory_grid", grid_size, false, ImGuiWindowFlags_None);

    if (g_containers.empty()) {
        ImGui::TextDisabled("No containers");
    } else {
        const int   columns = 3;
        const float spacing = style.ItemSpacing.x;
        const float grid_avail = ImGui::GetContentRegionAvail().x;
        const float card_w = (grid_avail - spacing * (columns - 1)) / columns;
        const float card_h = card_w * 1.1f;

        for (size_t i = 0; i < g_containers.size(); ++i) {
            ImGui::PushID((int)i);
            draw_card(g_containers[i], ImVec2(card_w, card_h));
            ImGui::Dummy(ImVec2(card_w, card_h)); // продвигаем курсор на место, занятое карточкой
            ImGui::PopID();

            if ((i + 1) % columns != 0 && i + 1 < g_containers.size())
                ImGui::SameLine();
        }
    }

    ImGui::EndChild();
}

void inventory::end_frame()
{
    if (!g_drawn_this_frame)
        GlassBackdrop_Set(kViewerBackdropSlot, CGRectZero, 0, false);
}

void inventory::AddContainer(const char* name, const char* skin_name, const char* rarity, ImU32 color, ImTextureID object)
{
    Container c;
    c.name      = name ? name : "";
    c.skin_name = skin_name ? skin_name : "";
    c.rarity    = rarity ? rarity : "";
    c.color     = color;
    c.object    = object;
    g_containers.push_back(std::move(c));
}

void inventory::ClearContainers()
{
    g_containers.clear();
}