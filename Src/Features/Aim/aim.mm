#include "aim.h"
#include "imgui.h"

void aim::run()
{
    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    float screenWidth = ImGui::GetIO().DisplaySize.x;
    float screenHeight = ImGui::GetIO().DisplaySize.y;

    if (aim::drawFOV)
    {
        ImVec2 center(screenWidth / 2.0f, screenHeight / 2.0f);
        draw->AddCircle(center, aim::fov_radius, ImGui::ColorConvertFloat4ToU32({
            aim::fov_color[0], aim::fov_color[1], aim::fov_color[2], aim::fov_color[3]
        }), 64, 1.0f);
    }
}