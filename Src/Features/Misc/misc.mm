#include "misc.h"
#include "imgui.h"
#include "il2cpp/il2cpp.h"

#include <vector>

void misc::run()
{
    if (!IL2CPP::Init()) return;
    if (!misc::thirdperson) return;

    static void* cmcClass = nullptr;
    static void* skelClass = nullptr;
    static void* pcClass = nullptr;
    if (!cmcClass) cmcClass = IL2CPP::Class("ScarletWork.Game.Camera", "CameraMovementController", "Assembly-CSharp");
    if (!skelClass) skelClass = IL2CPP::Class("ScarletWork.Game", "PlayerSkeleton", "Assembly-CSharp");
    if (!pcClass) pcClass = IL2CPP::Class("", "PlayerController", "Assembly-CSharp");
    if (!cmcClass) return;

    // CameraMovementController
    auto controllers = IL2CPP::FindObjectsOfType(cmcClass);
    if (controllers.empty()) return;
    uintptr_t cmc = controllers[0];

    // mode (0x18): 0 = FirstPerson, 1 = ThirdPerson
    IL2CPP::Write<int>(cmc + 0x18, 1);
    // maxCameraDistance (0x34) — дистанция камеры
    IL2CPP::Write<float>(cmc + 0x34, misc::thirdperson_distance);

    // Показать модель игрока (иначе third-person "из глаз пустоты")
    if (!skelClass || !pcClass) return;
    auto pcs = IL2CPP::FindObjectsOfType(pcClass);
    for (uintptr_t pc : pcs) {
        uintptr_t player = IL2CPP::Read<uintptr_t>(pc + 0x90);  // PlayerController._player
        if (!IL2CPP::ValidPtr(player)) continue;
        if (!IL2CPP::Read<uint8_t>(player + 0x1C)) continue;    // не локальный

        bool inc = true;
        void* a[2] = { IL2CPP::Type(skelClass), &inc };
        static void* mGetComp = nullptr;
        if (!mGetComp) {
            void* kComponent = IL2CPP::Class("UnityEngine", "Component", "UnityEngine.CoreModule");
            mGetComp = IL2CPP::Method(kComponent, "GetComponentInChildren", 2);
        }
        if (!mGetComp) return;
        uintptr_t skel = (uintptr_t)IL2CPP::Invoke(mGetComp, (void*)pc, a);
        if (!IL2CPP::ValidPtr(skel)) continue;

        static void* mSetVisible = nullptr;
        if (!mSetVisible) mSetVisible = IL2CPP::Method(skelClass, "SetRenderersVisible", 1);
        if (mSetVisible) {
            bool visible = true;
            void* args[1] = { &visible };
            IL2CPP::Invoke(mSetVisible, (void*)skel, args);
        }
        break;  // только для локального
    }
}