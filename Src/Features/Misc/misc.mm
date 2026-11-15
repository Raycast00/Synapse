#include "misc.h"
#include "imgui.h"
#include "il2cpp/il2cpp.h"

#include <vector>

void misc::run()
{
    if (!IL2CPP::Init()) return;
    if (!misc::silent_test) return;

    static void* cmcClass = nullptr;
    if (!cmcClass) cmcClass = IL2CPP::Class("ScarletWork.Game.Camera", "CameraMovementController", "Assembly-CSharp");
    if (!cmcClass) return;

    auto controllers = IL2CPP::FindObjectsOfType(cmcClass);
    if (controllers.empty()) return;
    uintptr_t cmc = controllers[0];

    // Тест 1: pitch (0x3C) / yaw (0x40) — private
    IL2CPP::Write<float>(cmc + 0x3C, misc::test_pitch);
    IL2CPP::Write<float>(cmc + 0x40, misc::test_yaw);

    // Тест 2: cameraPitch (0x50) — public, возможно "тихий"
    // IL2CPP::Write<float>(cmc + 0x50, misc::test_pitch);
}