#include "aim.h"
#include "imgui.h"
#include "il2cpp/il2cpp.h"

void aim::run()
{
    if (!IL2CPP::Ready()) return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    float screenWidth = ImGui::GetIO().DisplaySize.x;
    float screenHeight = ImGui::GetIO().DisplaySize.y;

    if (aim::aimbot)
    {
        // aimbot logic

        if (aim::silent)
        {
            // silent aim logic
        }
    }

    if (aim::draw_fov)
    {
        ImVec2 center(screenWidth / 2.0f, screenHeight / 2.0f);
        draw->AddCircle(center, aim::fov_radius, ImGui::ColorConvertFloat4ToU32({
            aim::fov_color[0], aim::fov_color[1], aim::fov_color[2], aim::fov_color[3]
        }), 64, 1.0f);
    }

    if (aim::anti_aim)
    {
        static void* cmcClass = IL2CPP::Class("ScarletWork.Game.Camera", "CameraMovementController");
        std::vector<uintptr_t> controllers = IL2CPP::FindObjectsOfType(cmcClass); 

        if (!controllers.empty())
        {
            uintptr_t cmc = controllers[0];
            IL2CPP::Write<float>(cmc + 0x40, aim::anti_yaw * (M_PI / 180.0f));
        }
    }
}