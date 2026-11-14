#include "misc.h"
#include "imgui.h"
#include "il2cpp/il2cpp.h"

#include <cmath>
#include <cstdio>

namespace il = IL2CPP;

namespace
{
    struct V3 { float x, y, z; };

    // Не из дампа, чистая косметика камеры — подбирать на вкус.
    constexpr float kDistance = 2.2f;   // насколько назад отодвинуть камеру вдоль взгляда
    constexpr float kHeight   = 0.4f;   // насколько приподнять камеру над линией взгляда
    constexpr bool  kStatus   = true;   // строка диагностики на экране; false = выключить

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

    void Status(const char* t)
    {
        if (!kStatus) return;
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        dl->AddText(ImVec2(11, 146), IM_COL32(0, 0, 0, 255), t);
        dl->AddText(ImVec2(10, 145), IM_COL32(120, 220, 255, 255), t);
    }

    bool  g_haveLastWrite = false;
    V3    g_lastWritten{};
}

void misc::run()
{
    char st[220];

    if (!il::Init()) { Status("MISC: IL2CPP not ready"); return; }
    if (!misc::thirdperson) { g_haveLastWrite = false; return; }
    if (!ResolveApi()) { Status("MISC: resolver failed (classes/methods not found)"); return; }

    uintptr_t pc = LocalPlayerController();
    uintptr_t cam = il::ValidPtr(pc) ? il::Read<uintptr_t>(pc + 0x30) : 0;   // PlayerController._mainCamera

    if (!il::ValidPtr(pc) || !il::ValidPtr(cam)) {
        std::snprintf(st, sizeof st, "MISC tp: pc=%d cam=%d (нет локального игрока/камеры — ты в матче?)",
                      il::ValidPtr(pc), il::ValidPtr(cam));
        Status(st);
        g_haveLastWrite = false;
        return;
    }

    void* camTr = il::Invoke(A.mGetTransform, (void*)cam, nullptr);
    if (!il::ValidPtr((uintptr_t)camTr)) {
        Status("MISC tp: camTr=0 (Camera.get_transform не отработал)");
        g_haveLastWrite = false;
        return;
    }

    V3 pos = il::InvokeBoxed<V3>(A.mGetPos, camTr, nullptr);
    V3 fwd = il::InvokeBoxed<V3>(A.mGetFwd, camTr, nullptr);
    if (!Finite(pos) || !Finite(fwd)) {
        Status("MISC tp: pos/fwd не finite");
        g_haveLastWrite = false;
        return;
    }

    // Если то, что мы читаем СЕЙЧАС, близко к тому, что мы сами записали ПРОШЛЫМ кадром — запись держится.
    // Если же текущая позиция далеко откатилась назад к "голове" — игра сама сбрасывает камеру каждый кадр
    // раньше, чем мы успеваем её сдвинуть, и дело не в резолвере, а в порядке вызовов в main.mm.
    bool holding = false;
    float driftFromLastWrite = -1.f;
    if (g_haveLastWrite) {
        float dx = pos.x - g_lastWritten.x, dy = pos.y - g_lastWritten.y, dz = pos.z - g_lastWritten.z;
        driftFromLastWrite = std::sqrt(dx*dx + dy*dy + dz*dz);
        holding = driftFromLastWrite < 0.05f;
    }

    V3 newPos{
        pos.x - fwd.x * kDistance,
        pos.y - fwd.y * kDistance + kHeight,
        pos.z - fwd.z * kDistance
    };

    void* a[1] = { &newPos };
    il::Invoke(A.mSetPos, camTr, a);

    g_lastWritten = newPos;
    g_haveLastWrite = true;

    std::snprintf(st, sizeof st,
        "MISC tp: pc=1 cam=1 pos=(%.1f,%.1f,%.1f) drift=%.2f holding=%d",
        pos.x, pos.y, pos.z, driftFromLastWrite, (int)holding);
    Status(st);
}