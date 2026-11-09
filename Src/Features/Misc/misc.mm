#include "misc.h"
#include "imgui.h"
#include "il2cpp/il2cpp.h"

void misc::run()
{
    if (misc::thirdperson)
    {
        void* cameraClass = IL2CPP:Class("", "PlayerFPSCamera");
        void* localField = IL2CPP::Field(camClass, "<Local>k__BackingField");
    }
}