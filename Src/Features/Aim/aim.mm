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

    if (aim::drawFOV)
    {
        ImVec2 center(screenWidth / 2.0f, screenHeight / 2.0f);
        draw->AddCircle(center, aim::fov_radius, ImGui::ColorConvertFloat4ToU32({
            aim::fov_color[0], aim::fov_color[1], aim::fov_color[2], aim::fov_color[3]
        }), 64, 1.0f);
    }

    if (aim::anti_aim)
    {
        void* camClass = IL2CPP::Class("ScarletWork.Game", "PlayerFPSCamera");
        void* localField = IL2CPP::Field(camClass, "<Local>k__BackingField");
        uintptr_t camera = IL2CPP::StaticField<uintptr_t>(localField);
        if (!camera) return;

        IL2CPP::Write<float>(camera + 0x50, aim::anti_yaw); 
        IL2CPP::Write<float>(camera + 0x4C, aim::anti_pitch); 
    }
}