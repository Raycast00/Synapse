#import <Foundation/Foundation.h>
#include "visuals.h"
#include "il2cpp/il2cpp.h"
#include "imgui.h"
#include <vector>
#include <chrono>

struct Vector3f { float x, y, z; };

// ---------- il2cpp lookups ----------
static uintptr_t localPlayer()
{
    static void* klass  = IL2CPP::Class("ScarletWork.Game.Console", "ConsoleTargets");
    static void* method = IL2CPP::Method(klass, "get_Player", 0);
    return (uintptr_t)IL2CPP::Invoke(method, nullptr, nullptr);   // ссылочный тип, не упакован
}

static Vector3f worldToScreen(uintptr_t camera, Vector3f world)
{
    static void* camKlass = IL2CPP::Class("UnityEngine", "Camera", "UnityEngine.CoreModule");
    static void* method   = IL2CPP::Method(camKlass, "WorldToScreenPoint", 1);
    if (!method || !camera) return { -1, -1, -1 };

    void* args[1] = { &world };
    return IL2CPP::InvokeBoxed<Vector3f>(method, (void*)camera, args);   // Vector3 — value-тип, упакован
}

static int playerHealth(uintptr_t pc)
{
    static void* klass  = IL2CPP::Class("", "PlayerController");
    static void* method = IL2CPP::Method(klass, "get_Health", 0);
    if (!method) return -1;

    void* args[1] = {};
    return IL2CPP::InvokeBoxed<int>(method, (void*)pc, args);
}

// ---------- offsets (dump.cs v1.4) ----------
namespace off {
    constexpr uintptr_t pcFpsCamera  = 0x28;   // PlayerController._fpsCamera
    constexpr uintptr_t pcMainCamera = 0x30;   // PlayerController._mainCamera
    constexpr uintptr_t pcNetwork    = 0x88;   // PlayerController._network
    constexpr uintptr_t pcIsDeath    = 0xA8;   // PlayerController._isDeath
    constexpr uintptr_t netPosition  = 0xD4;   // PlayerNetwork.networkPosition (Vector3)
}

// ---------- enemy cache ----------
static std::vector<uintptr_t> g_enemies;
static uintptr_t g_localPC = 0;

static void refreshEnemies()
{
    g_localPC = localPlayer();
    g_enemies.clear();

    static void* pcKlass = IL2CPP::Class("", "PlayerController");
    for (uintptr_t pc : IL2CPP::FindObjectsOfType(pcKlass)) {
        if (pc != g_localPC)
            g_enemies.push_back(pc);
    }
}

void visuals::run()
{
    if (!IL2CPP::Init()) return;
    if (!visuals::esp_enabled) return;

    static bool once = false;
    if (!once) {
        once = true;
        uintptr_t lp = localPlayer();
        NSLog(@"[visuals] local=%p", (void*)lp);
        if (IL2CPP::ValidPtr(lp)) {
            uintptr_t cam = IL2CPP::Read<uintptr_t>(lp + off::pcMainCamera);
            NSLog(@"[visuals] cam=%p", (void*)cam);
        }
    }

    // список врагов обновляем раз в 2 сек, а не каждый кадр — объекты живут дольше кадра
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (now - last > std::chrono::seconds(2)) {
        last = now;
        refreshEnemies();
    }

    if (!IL2CPP::ValidPtr(g_localPC)) return;
    uintptr_t cam = IL2CPP::Read<uintptr_t>(g_localPC + off::pcMainCamera);
    if (!IL2CPP::ValidPtr(cam)) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 screenSize = ImGui::GetIO().DisplaySize;
    float scale = ImGui::GetIO().DisplayFramebufferScale.x;   // Unity отдаёт пиксели, ImGui ждёт поинты
    if (scale <= 0.f) scale = 1.f;

    for (uintptr_t pc : g_enemies) {
        if (!IL2CPP::ValidPtr(pc)) continue;
        if (IL2CPP::Read<bool>(pc + off::pcIsDeath)) continue;

        uintptr_t net = IL2CPP::Read<uintptr_t>(pc + off::pcNetwork);
        if (!IL2CPP::ValidPtr(net)) continue;

        Vector3f feet = IL2CPP::Read<Vector3f>(net + off::netPosition);
        Vector3f head = feet; head.y += 1.8f;   // примерная высота, боксколлайдера под рукой нет

        Vector3f sFeet = worldToScreen(cam, feet);
        Vector3f sHead = worldToScreen(cam, head);
        if (sFeet.z <= 0.f || sHead.z <= 0.f) continue;   // за камерой

        ImVec2 pFeet(sFeet.x / scale, screenSize.y - sFeet.y / scale);
        ImVec2 pHead(sHead.x / scale, screenSize.y - sHead.y / scale);

        float h = pFeet.y - pHead.y;
        if (h < 4.f || h > screenSize.y) continue;   // слишком мелко/мусор — не рисуем
        float w = h * 0.45f;

        ImVec2 boxMin(pHead.x - w * 0.5f, pHead.y);
        ImVec2 boxMax(pHead.x + w * 0.5f, pFeet.y);

        if (visuals::box2D) {
            ImU32 col = ImGui::ColorConvertFloat4ToU32(*(ImVec4*)visuals::box_color);
            if (visuals::boxFill) {
                ImU32 fillCol = ImGui::ColorConvertFloat4ToU32(*(ImVec4*)visuals::box_fill_color);
                dl->AddRectFilled(boxMin, boxMax, fillCol);
            }
            dl->AddRect(boxMin, boxMax, col);
        }

        if (visuals::healthBar) {
            int hp = playerHealth(pc);
            float ratio = hp >= 0 ? (hp / 100.f) : 1.f;
            if (ratio < 0.f) ratio = 0.f;
            if (ratio > 1.f) ratio = 1.f;

            ImVec2 barMin(boxMin.x - 6, boxMin.y);
            ImVec2 barMax(boxMin.x - 3, boxMax.y);
            ImU32 hbCol = ImGui::ColorConvertFloat4ToU32(*(ImVec4*)visuals::healthbar_color);
            dl->AddRectFilled(ImVec2(barMin.x, barMax.y - (barMax.y - barMin.y) * ratio), barMax, hbCol);
            dl->AddRect(barMin, barMax, IM_COL32(0, 0, 0, 255));
        }

        if (visuals::distance) {
            float dist = sqrtf((feet.x * feet.x) + (feet.y * feet.y) + (feet.z * feet.z)); // от камеры, не от игрока — см. заметку ниже
            char buf[32];
            snprintf(buf, sizeof(buf), "%.0fm", dist / 10.f);   // делитель — заглушка, единицы Unity неизвестны
            ImU32 dCol = ImGui::ColorConvertFloat4ToU32(*(ImVec4*)visuals::distance_color);
            dl->AddText(ImVec2(boxMin.x, boxMax.y + 2), dCol, buf);
        }
    }
}