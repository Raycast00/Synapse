#include "Memory/KittyInclude.hpp"
#include "imgui.h"
#include "Math/math.h"
#include "Features/Visuals/visuals.h"
#include "il2cpp/IL2CPP_Resolver.hpp"

template <typename T>
inline T Read(uintptr_t addr)
{
    T out{};
    KittyMemory::memRead(addr, &out, sizeof(T));
    return out;
}

template <typename T>
inline bool Write(uintptr_t addr, T value)
{
    return KittyMemory::memWrite(addr, &value, sizeof(T));
}

static void* g_bmClass        = nullptr;
static void* g_singletonClass = nullptr;
static void* g_instanceField  = nullptr;
static bool  g_resolved       = false;
static bool  g_failed         = false;

static double (*g_realtimeFn)() = nullptr;
static bool   g_timeResolved    = false;

void visuals::run()
{
    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    float screenWidth  = ImGui::GetIO().DisplaySize.x;
    float screenHeight = ImGui::GetIO().DisplaySize.y;

    if (visuals::bomb_timer_bar && !g_failed)
    {
        // 1) resolve BombManager once
        if (!g_resolved)
        {
            g_bmClass = IL2CPP::Class::Find("BombManager");
            if (!g_bmClass) { g_failed = true; return; }

            g_singletonClass = IL2CPP::Class::GetParent(g_bmClass);
            if (!g_singletonClass) g_singletonClass = g_bmClass;

            g_instanceField = IL2CPP::Class::GetFieldFromName(g_singletonClass, "_instance");
            if (!g_instanceField)
                g_instanceField = IL2CPP::Class::GetFieldFromName(g_bmClass, "_instance");
            if (!g_instanceField) { g_failed = true; return; }

            g_resolved = true;
        }

        // 2) static _instance -> BombManager*
        void* bmInstance = nullptr;
        IL2CPP::Field::GetStaticValue(g_instanceField, &bmInstance);
        if (!bmInstance) return;

        // 3) BombManager._bomb @ 0x48
        uintptr_t bomb = Read<uintptr_t>((uintptr_t)bmInstance + 0x48);
        if (!bomb) return;

        // 4) Bomb._isActive @ 0x18
        bool isActive = Read<bool>(bomb + 0x18);
        if (!isActive) return;

        // 5) Bomb._explodeAt @ 0x38
        double explodeAt = Read<double>(bomb + 0x38);

        // 6) now
        if (!g_timeResolved)
        {
            g_realtimeFn = (double(*)())IL2CPP::ResolveCall(
                "UnityEngine.Time::get_realtimeSinceStartupAsDouble");
            g_timeResolved = true;
        }
        double now = g_realtimeFn ? g_realtimeFn() : 0.0;

        double timeLeft = explodeAt - now;
        if (timeLeft < 0.0) timeLeft = 0.0;

        const double TOTAL = 40.0;
        float progress = (float)(1.0 - timeLeft / TOTAL);
        if (progress < 0.f) progress = 0.f;
        if (progress > 1.f) progress = 1.f;

        // 7) worldPos — через Unity API из резольвера
        Unity::CComponent* comp = (Unity::CComponent*)bomb;
        Unity::CTransform* tr   = comp->GetTransform();
        if (!tr) return;
        Vector3 worldPos = tr->GetPosition();

        Vector2 screenPos;
        if (!WorldToScreen(worldPos, screenPos)) return;

        // 8) бар
        const float barW = 120.f;
        const float barH = 8.f;
        const float topY = screenPos.y - 20.f;

        draw->AddRectFilled(
            ImVec2(screenPos.x - barW * 0.5f, topY),
            ImVec2(screenPos.x + barW * 0.5f, topY + barH),
            IM_COL32(0, 0, 0, 160), 3.f);

        ImU32 col = progress > 0.66f ? IM_COL32(80, 220, 80, 255)
                  : progress > 0.33f ? IM_COL32(240, 200, 60, 255)
                                     : IM_COL32(230, 70, 70, 255);
        draw->AddRectFilled(
            ImVec2(screenPos.x - barW * 0.5f, topY),
            ImVec2(screenPos.x - barW * 0.5f + barW * progress, topY + barH),
            col, 3.f);

        char buf[32];
        snprintf(buf, sizeof(buf), "C4 %.1fs", timeLeft);
        draw->AddText(ImVec2(screenPos.x - barW * 0.5f, topY - 18.f),
                      IM_COL32(255, 255, 255, 230), buf);
    }
}