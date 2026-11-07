#include "Memory/KittyInclude.hpp"
#include "imgui.h"
#include "Math/math.h"
#include "Features/Visuals/visuals.h"
#include <vector>
#include <string>
#include <cmath>
#include <map>

template<typename T>
static inline T Rd(uintptr_t addr) {
    T v{};
    if (addr) KittyMemory::memRead(addr, &v, sizeof(T));
    return v;
}

static inline uintptr_t RdPtr(uintptr_t addr) {
    uintptr_t v = 0;
    if (addr) KittyMemory::memRead(addr, &v, sizeof(v));
    return v;
}

static inline void RdVec3(uintptr_t addr, float out[3]) {
    if (addr) KittyMemory::memRead(addr, out, 12);
}

static std::string ReadStr(uintptr_t str) {
    if (!str) return "";
    int len = Rd<int>(str + 0x10);
    if (len <= 0 || len > 64) return "";
    std::wstring w(len, L'\0');
    KittyMemory::memRead(str + 0x14, w.data(), len * 2);
    std::string s;
    for (wchar_t c : w) s.push_back((char)c);
    return s;
}

extern uintptr_t g_assembly_csharp_base; 
extern uintptr_t g_unity_core_base; 

// Указатели на функции
int (*get_Health)(void* __this, void* method) = nullptr;
void* (*get_Player)(void* __this, void* method) = nullptr;
bool (*get_IsDeath)(void* __this, void* method) = nullptr;
void* (*get_MainCamera)(void* __this, void* method) = nullptr;
void* (*get_MovementController)(void* __this, void* method) = nullptr;

// Указатели на функции Unity (взяты из твоего дампа)
void (*Camera_get_worldToCameraMatrix)(void* camera, float* outMatrix, void* method) = nullptr;
void (*Camera_get_projectionMatrix)(void* camera, float* outMatrix, void* method) = nullptr;

// Перемножение матриц 4х4 (Column-Major)
void MultiplyMatrices(const float* m1, const float* m2, float* out) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[i + j * 4] = m1[i + 0 * 4] * m2[0 + j * 4] +
                             m1[i + 1 * 4] * m2[1 + j * 4] +
                             m1[i + 2 * 4] * m2[2 + j * 4] +
                             m1[i + 3 * 4] * m2[3 + j * 4];
        }
    }
}

void InitESP() {
    // Assembly-CSharp.dll (из твоего первого дампа)
    get_Health = (int(*)(void*, void*))getAbsoluteAddress(g_assembly_csharp_base, 23413716);
    get_Player = (void*(*)(void*, void*))getAbsoluteAddress(g_assembly_csharp_base, 23413700);
    get_IsDeath = (bool(*)(void*, void*))getAbsoluteAddress(g_assembly_csharp_base, 23413684);
    get_MainCamera = (void*(*)(void*, void*))getAbsoluteAddress(g_assembly_csharp_base, 23413316);
    get_MovementController = (void*(*)(void*, void*))getAbsoluteAddress(g_assembly_csharp_base, 23413348);

    // UnityEngine.CoreModule.dll (ИЗ ТВОЕГО НОВОГО ДАМПА!)
    Camera_get_worldToCameraMatrix = (void(*)(void*, float*, void*))getAbsoluteAddress(g_unity_core_base, 47158916);
    Camera_get_projectionMatrix = (void(*)(void*, float*, void*))getAbsoluteAddress(g_unity_core_base, 47159276);
}

static bool W2S(const float* vp, ImVec2 scr, const float* wp, ImVec2& out) {
    float w = wp[0]*vp[3] + wp[1]*vp[7] + wp[2]*vp[11] + vp[15];
    if (w < 0.1f || !std::isfinite(w)) return false;
    
    float x = (wp[0]*vp[0] + wp[1]*vp[4] + wp[2]*vp[8]  + vp[12]) / w;
    float y = (wp[0]*vp[1] + wp[1]*vp[5] + wp[2]*vp[9]  + vp[13]) / w;
    
    out.x = (x * 0.5f + 0.5f) * scr.x;
    out.y = (1.0f - (y * 0.5f + 0.5f)) * scr.y;
    return true;
}

void Draw(ImDrawList* drawList, float screenW, float screenH) {
    if (!visuals::esp_enabled || !get_Health) return;

    extern uintptr_t g_local_player;
    if (!g_local_player) return;

    // 1. Получаем камеру локального игрока
    void* mainCam = get_MainCamera((void*)g_local_player, nullptr);
    if (!mainCam) return;

    // 2. Получаем и перемножаем матрицы
    float viewProj[16] = {0};
    if (Camera_get_projectionMatrix && Camera_get_worldToCameraMatrix) {
        float proj[16], view[16];
        Camera_get_projectionMatrix(mainCam, proj, nullptr);
        Camera_get_worldToCameraMatrix(mainCam, view, nullptr);
        
        // viewProj = proj * view
        MultiplyMatrices(proj, view, viewProj);
    } else {
        return;
    }

    ImVec2 screenSize{ screenW, screenH };
    extern std::vector<uintptr_t> g_players;

    for (uintptr_t pc : g_players) {
        if (!pc || pc == g_local_player) continue;

        if (get_IsDeath((void*)pc, nullptr)) continue;

        int hp = get_Health((void*)pc, nullptr);

        void* playerObj = get_Player((void*)pc, nullptr);
        std::string nickname = "";
        if (playerObj) {
            uintptr_t nameStr = RdPtr((uintptr_t)playerObj + 0x18); 
            nickname = ReadStr(nameStr);
        }

        // Позиция из PlayerController -> PlayerNetwork
        float feetPos[3] = {0, 0, 0};
        uintptr_t network = RdPtr(pc + 0x88); 
        if (network) {
            RdVec3(network + 0xD4, feetPos); 
        }
        
        if (feetPos[0] == 0 && feetPos[1] == 0 && feetPos[2] == 0) continue;

        float headPos[3] = { feetPos[0], feetPos[1] + 1.8f, feetPos[2] };

        ImVec2 sFeet, sHead;
        if (!W2S(viewProj, screenSize, feetPos, sFeet)) continue;
        if (!W2S(viewProj, screenSize, headPos, sHead)) continue;

        // ===================== РИСОВАНИЕ =====================
        float h = sFeet.y - sHead.y;
        if (h <= 1.f) continue;
        float w = h * 0.5f;
        float cx = (sFeet.x + sHead.x) * 0.5f;
        float left = cx - w * 0.5f, right = cx + w * 0.5f;
        float top = sHead.y, bottom = sFeet.y;

        if (visuals::box2D) {
            ImU32 col = ImGui::GetColorU32(ImVec4(visuals::box_color[0], visuals::box_color[1], visuals::box_color[2], visuals::box_color[3]));
            drawList->AddRect({left, top}, {right, bottom}, col, 0.f, 0, 1.5f);
        }

        if (visuals::healthBar) {
            float f = ImClamp(hp / 100.f, 0.f, 1.f);
            float bw = 2.f, gap = 3.f, bx = left - gap - bw;
            drawList->AddRectFilled({bx, top}, {bx + bw, bottom}, IM_COL32(0,0,0,180));
            float fh = (bottom - top) * f;
            ImU32 col = ImGui::GetColorU32(ImVec4(visuals::healthbar_color[0], visuals::healthbar_color[1], visuals::healthbar_color[2], visuals::healthbar_color[3]));
            drawList->AddRectFilled({bx, bottom - fh}, {bx + bw, bottom}, col);
        }

        if (visuals::names && !nickname.empty()) {
            ImU32 col = ImGui::GetColorU32(ImVec4(visuals::names_color[0], visuals::names_color[1], visuals::names_color[2], visuals::names_color[3]));
            ImVec2 ts = ImGui::CalcTextSize(nickname.c_str());
            drawList->AddText({cx - ts.x*0.5f, top - ts.y - 2.f}, col, nickname.c_str());
        }
    }
}