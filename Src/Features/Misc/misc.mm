#include "misc.h"
#include "imgui.h"
#include "il2cpp/il2cpp.h"

namespace il = IL2CPP;

namespace
{
    struct V3 { float x, y, z; };

    // Не из дампа, чистая косметика камеры — подбирать на вкус.
    constexpr float kDistance = 2.2f;   // насколько назад отодвинуть камеру вдоль взгляда
    constexpr float kHeight   = 0.4f;   // насколько приподнять камеру над линией взгляда

    struct Api
    {
        bool ready = false;
        void *kPC{}, *kPlayer{};
        void *mGetTransform{}, *mGetPos{}, *mSetPos{}, *mGetFwd{};
    } A;

    bool ResolveApi()
    {
        if (A.ready) return true;
        const char* core = "UnityEngine.CoreModule";
        A.kPC     = il::Class("", "PlayerController", "Assembly-CSharp");
        A.kPlayer = il::Class("Photon.Realtime", "Player", "PhotonRealtime");

        void* kComponent = il::Class("UnityEngine", "Component", core);
        void* kTransform = il::Class("UnityEngine", "Transform", core);

        A.mGetTransform = il::Method(kComponent, "get_transform", 0);
        A.mGetPos       = il::Method(kTransform, "get_position", 0);
        A.mSetPos       = il::Method(kTransform, "set_position", 1);
        A.mGetFwd       = il::Method(kTransform, "get_forward", 0);

        A.ready = A.kPC && A.kPlayer && A.mGetTransform && A.mGetPos && A.mSetPos && A.mGetFwd;
        return A.ready;
    }

    // Та же цепочка, что в rage.mm: PlayerController._player (0x90) -> Photon Player.IsLocal (0x1C).
    uintptr_t LocalPlayerController()
    {
        static uintptr_t cached = 0;
        static double nextRefresh = 0.0;
        double now = ImGui::GetTime();
        if (!il::ValidPtr(cached) || now >= nextRefresh) {
            nextRefresh = now + 0.25;
            cached = 0;
            for (uintptr_t pc : il::FindObjectsOfType(A.kPC)) {
                uintptr_t pl = il::Read<uintptr_t>(pc + 0x90);
                if (il::ValidPtr(pl) && il::Read<uint8_t>(pl + 0x1C)) { cached = pc; break; }
            }
        }
        return cached;
    }

    bool Finite(const V3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}

void misc::run()
{
    if (!il::Init()) return;
    if (!misc::thirdperson) return;
    if (!ResolveApi()) return;

    uintptr_t pc = LocalPlayerController();
    if (!il::ValidPtr(pc)) return;

    uintptr_t cam = il::Read<uintptr_t>(pc + 0x30);   // PlayerController._mainCamera
    if (!il::ValidPtr(cam)) return;

    void* camTr = il::Invoke(A.mGetTransform, (void*)cam, nullptr);
    if (!il::ValidPtr((uintptr_t)camTr)) return;

    V3 pos = il::InvokeBoxed<V3>(A.mGetPos, camTr, nullptr);
    V3 fwd = il::InvokeBoxed<V3>(A.mGetFwd, camTr, nullptr);
    if (!Finite(pos) || !Finite(fwd)) return;

    V3 newPos{
        pos.x - fwd.x * kDistance,
        pos.y - fwd.y * kDistance + kHeight,
        pos.z - fwd.z * kDistance
    };

    void* a[1] = { &newPos };
    il::Invoke(A.mSetPos, camTr, a);
}