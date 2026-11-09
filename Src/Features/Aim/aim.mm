#include "aim.h"
#include "Features/Rage/rage.h"
#include "imgui.h"
#include "il2cpp/il2cpp.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace il = IL2CPP;

namespace {
    struct V3 { float x, y, z; };

    // Источник: dump.cs. Повторяет резолвер visuals.mm, здесь своя копия, т.к. файлы не делят состояние.
    struct Api {
        bool ready = false;
        void *kPlayerController{}, *kSkeleton{}, *kCamMove{};
        void *mGetTransform{}, *mGetPosition{}, *mGetCompInChildren{}, *mGetHeadBone{};
        void *mW2S{}, *mCamMain{};
    } A;

    bool Resolve()
    {
        if (A.ready) return true;
        const char* core = "UnityEngine.CoreModule";
        A.kPlayerController = il::Class("", "PlayerController", "Assembly-CSharp");
        A.kSkeleton          = il::Class("ScarletWork.Game", "PlayerSkeleton", "Assembly-CSharp");
        A.kCamMove            = il::Class("ScarletWork.Game.Camera", "CameraMovementController", "Assembly-CSharp");
        void* kComponent = il::Class("UnityEngine", "Component", core);
        void* kTransform = il::Class("UnityEngine", "Transform", core);
        void* kCamera    = il::Class("UnityEngine", "Camera", core);

        A.mGetTransform      = il::Method(kComponent, "get_transform", 0);
        A.mGetCompInChildren = il::Method(kComponent, "GetComponentInChildren", 2);
        A.mGetPosition       = il::Method(kTransform, "get_position", 0);
        A.mW2S               = il::Method(kCamera, "WorldToScreenPoint", 1);
        A.mCamMain           = il::Method(kCamera, "get_main", 0);
        A.mGetHeadBone       = il::Method(A.kSkeleton, "GetHeadBone", 0);

        A.ready = A.kPlayerController && A.kSkeleton && A.kCamMove && A.mGetTransform &&
                  A.mGetCompInChildren && A.mGetPosition && A.mW2S && A.mCamMain;
        return A.ready;
    }

    bool Finite(const V3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

    V3 WorldPos(void* component)
    {
        void* tr = il::Invoke(A.mGetTransform, component, nullptr);
        if (!il::ValidPtr((uintptr_t)tr)) return {0,0,0};
        return il::InvokeBoxed<V3>(A.mGetPosition, tr, nullptr);
    }

    uintptr_t LocalCamera()
    {
        static void* klass = il::Class("", "PlayerFPSCamera");
        static void* field = il::Field(klass, "<Local>k__BackingField");
        uintptr_t fpsCam = il::StaticField<uintptr_t>(field);
        if (!il::ValidPtr(fpsCam)) return 0;
        // PlayerFPSCamera._camera (0x108), dump.cs. Если занято — fallback на Camera.main.
        uintptr_t cam = il::Read<uintptr_t>(fpsCam + 0x108);
        if (il::ValidPtr(cam)) return cam;
        void* c = il::Invoke(A.mCamMain, nullptr, nullptr);
        return il::ValidPtr((uintptr_t)c) ? (uintptr_t)c : 0;
    }

    bool W2S(uintptr_t cam, const V3& w, ImVec2& out)
    {
        V3 p = w;
        void* a[1] = { &p };
        V3 s = il::InvokeBoxed<V3>(A.mW2S, (void*)cam, a);
        if (!Finite(s) || s.z <= 0.f) return false;
        out = ImVec2(s.x, ImGui::GetIO().DisplaySize.y - s.y);   // инверсия Y, как в visuals.mm
        return true;
    }

    // Та же команда — свой; без неё пропускаем проверку, чтобы не тащить сюда весь Photon-резолвер.
    struct Target { uintptr_t pc = 0, skeleton = 0; };

    std::vector<Target> Enumerate()
    {
        std::vector<Target> out;
        for (uintptr_t pc : il::FindObjectsOfType(A.kPlayerController)) {
            if (il::Read<uint8_t>(pc + 0xA8)) continue;   // PlayerController._isDeath
            bool inc = true;
            void* a[2] = { il::Type(A.kSkeleton), &inc };
            uintptr_t skel = (uintptr_t)il::Invoke(A.mGetCompInChildren, (void*)pc, a);
            if (!il::ValidPtr(skel)) continue;
            out.push_back({ pc, skel });
        }
        return out;
    }

    V3 BoneTarget(uintptr_t skeleton)
    {
        if (A.mGetHeadBone && (aim::current_aimbot_hitbox == 0 ||
            (aim::current_aimbot_hitbox == 2 && (rand() & 1)))) {
            uintptr_t hb = (uintptr_t)il::Invoke(A.mGetHeadBone, (void*)skeleton, nullptr);
            if (il::ValidPtr(hb)) return WorldPos((void*)hb);
        }
        // "Chest" и запасной вариант для "Random": центр скелета.
        uintptr_t body = il::Read<uintptr_t>(skeleton + 0x60);   // PlayerSkeleton._body
        if (il::ValidPtr(body)) return WorldPos((void*)body);
        return WorldPos((void*)skeleton);
    }
}

void aim::run()
{
    if (!IL2CPP::Init()) return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    float screenWidth  = ImGui::GetIO().DisplaySize.x;
    float screenHeight = ImGui::GetIO().DisplaySize.y;
    ImVec2 center(screenWidth / 2.0f, screenHeight / 2.0f);

    rage::ApplyRecoilControl(aim::RCS);

    if (aim::aimbot && Resolve()) {
        uintptr_t cam = LocalCamera();
        if (il::ValidPtr(cam)) {
            Target best{}; float bestD = aim::fov_radius;
            for (const Target& t : Enumerate()) {
                V3 w = BoneTarget(t.skeleton);
                ImVec2 s;
                if (!W2S(cam, w, s)) continue;
                float d = std::hypot(s.x - center.x, s.y - center.y);
                if (d < bestD) { bestD = d; best = t; }
            }
            if (best.pc) {
                V3 w = BoneTarget(best.skeleton);
                static void* kCam = il::Class("", "PlayerFPSCamera");
                static void* fLocal = il::Field(kCam, "<Local>k__BackingField");
                uintptr_t fpsCam = il::StaticField<uintptr_t>(fLocal);
                uintptr_t cmcList0 = 0;
                {
                    std::vector<uintptr_t> cmcs = il::FindObjectsOfType(A.kCamMove);
                    if (!cmcs.empty()) cmcList0 = cmcs[0];
                }
                if (il::ValidPtr(cmcList0) && il::ValidPtr(fpsCam)) {
                    V3 eye = WorldPos((void*)fpsCam);   // приближение: позиция камеры-объекта как глаза
                    float dx = w.x - eye.x, dy = w.y - eye.y, dz = w.z - eye.z;
                    float yawTarget   = std::atan2(dx, dz);
                    float pitchTarget = -std::atan2(dy, std::sqrt(dx * dx + dz * dz));

                    float yawCur   = il::Read<float>(cmcList0 + 0x40);
                    float pitchCur = il::Read<float>(cmcList0 + 0x3C);
                    float t = std::clamp(aim::aimbot_smoothing > 0.f ? 1.f / aim::aimbot_smoothing : 1.f, 0.02f, 1.f);
                    il::Write<float>(cmcList0 + 0x40, yawCur   + (yawTarget   - yawCur)   * t);
                    il::Write<float>(cmcList0 + 0x3C, pitchCur + (pitchTarget - pitchCur) * t);
                }
                // aim::silent: без хука FireRay/Shoot не сделать честно, не реализовано.
            }
        }
    }

    if (aim::draw_fov) {
        draw->AddCircle(center, aim::fov_radius, ImGui::ColorConvertFloat4ToU32({
            aim::fov_color[0], aim::fov_color[1], aim::fov_color[2], aim::fov_color[3]
        }), 64, 1.0f);
    }

    if (aim::anti_aim) {
        static void* cmcClass = IL2CPP::Class("ScarletWork.Game.Camera", "CameraMovementController");
        std::vector<uintptr_t> controllers = IL2CPP::FindObjectsOfType(cmcClass);
        if (!controllers.empty()) {
            uintptr_t cmc = controllers[0];
            IL2CPP::Write<float>(cmc + 0x40, aim::anti_yaw * (float)(M_PI / 180.0));
        }
    }

    // aim::aim_through_walls, aim::automatic_fire: не реализовано в этом заходе.
}