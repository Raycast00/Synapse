#import <Foundation/Foundation.h>
#include "visuals.h"
#include "il2cpp/il2cpp.h"
#include "imgui.h"
#include <vector>
#include <string>
#include <cmath>
#include <chrono>

struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };

// ---------- Il2CppString -> std::string ----------
static std::string readStr(void* str) {
    if (!str) return "";
    int32_t len = *(int32_t*)((uint8_t*)str + 0x10);
    if (len <= 0 || len > 512) return "";
    char16_t* chars = (char16_t*)((uint8_t*)str + 0x14);
    std::string out; out.reserve(len);
    for (int32_t i = 0; i < len; ++i) {
        char16_t c = chars[i];
        if (c < 0x80) out.push_back((char)c);
        else if (c < 0x800) {
            out.push_back((char)(0xC0 | (c >> 6)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else {
            out.push_back((char)(0xE0 | (c >> 12)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// ---------- API cache ----------
struct Api {
    void *pcKlass=nullptr, *camKlass=nullptr, *pvKlass=nullptr, *playerKlass=nullptr;
    void *getHealthM=nullptr, *getIsMineM=nullptr, *getNickM=nullptr, *w2sM=nullptr;
    bool init() {
        if (pcKlass) return true;
        pcKlass     = IL2CPP::Class("", "PlayerController");                       // Assembly-CSharp
        camKlass    = IL2CPP::Class("UnityEngine", "Camera", "UnityEngine.CoreModule");
        pvKlass     = IL2CPP::Class("Photon.Pun", "PhotonView", "PhotonUnityNetworking");
        playerKlass = IL2CPP::Class("Photon.Realtime", "Player", "PhotonRealtime");
        if (!pcKlass || !camKlass || !pvKlass || !playerKlass) return false;
        getHealthM  = IL2CPP::Method(pcKlass, "get_Health", 0);
        getIsMineM  = IL2CPP::Method(pvKlass, "get_IsMine", 0);
        getNickM    = IL2CPP::Method(playerKlass, "get_NickName", 0);
        w2sM        = IL2CPP::Method(camKlass, "WorldToScreenPoint", 1);
        return true;
    }
};
static Api api;

// ---------- helpers ----------
static bool isMine(uintptr_t pc) {
    void* view = IL2CPP::Read<void*>(pc + 0x80);
    if (!view || !api.getIsMineM) return false;
    void* ret = IL2CPP::Invoke(api.getIsMineM, view, nullptr);
    return ret && *(bool*)((uint8_t*)ret + 0x10);
}

static Vec3 netPos(uintptr_t pc) {
    void* net = IL2CPP::Read<void*>(pc + 0x88);
    if (!net) return {0,0,0};
    return IL2CPP::Read<Vec3>((uintptr_t)net + 0xD4);
}

static int health(uintptr_t pc) {
    if (!api.getHealthM) return 0;
    void* ret = IL2CPP::Invoke(api.getHealthM, (void*)pc, nullptr);
    return ret ? *(int*)((uint8_t*)ret + 0x10) : 0;
}

static std::string nick(uintptr_t pc) {
    void* player = IL2CPP::Read<void*>(pc + 0x90);
    if (!player || !api.getNickM) return "";
    void* ret = IL2CPP::Invoke(api.getNickM, player, nullptr);
    return readStr(ret);
}

static bool w2s(uintptr_t cam, Vec3 world, Vec2& out, float screenH) {
    if (!cam || !api.w2sM) return false;
    void* args[1] = { &world };
    void* ret = IL2CPP::Invoke(api.w2sM, (void*)cam, args);
    if (!ret) return false;
    Vec3 sp = *(Vec3*)((uint8_t*)ret + 0x10);
    if (sp.z < 0.1f) return false;
    out.x = sp.x;
    out.y = screenH - sp.y; // Unity bottom-left -> ImGui top-left
    return true;
}

// ---------- box ----------
static void drawBox(ImDrawList* dl, float x, float y, float w, float h, ImU32 col) {
    if (visuals::current_box_type == 0) {
        dl->AddRect(ImVec2(x,y), ImVec2(x+w,y+h), col, 0, 0, 1.0f);
    } else {
        float cs = fminf(w, h) * 0.2f;
        dl->AddLine(ImVec2(x,   y),   ImVec2(x+cs, y),   col, 1.0f);
        dl->AddLine(ImVec2(x,   y),   ImVec2(x,   y+cs), col, 1.0f);
        dl->AddLine(ImVec2(x+w, y),   ImVec2(x+w-cs, y), col, 1.0f);
        dl->AddLine(ImVec2(x+w, y),   ImVec2(x+w, y+cs), col, 1.0f);
        dl->AddLine(ImVec2(x,   y+h), ImVec2(x+cs, y+h), col, 1.0f);
        dl->AddLine(ImVec2(x,   y+h), ImVec2(x,   y+h-cs),col, 1.0f);
        dl->AddLine(ImVec2(x+w, y+h), ImVec2(x+w-cs,y+h),col, 1.0f);
        dl->AddLine(ImVec2(x+w, y+h), ImVec2(x+w, y+h-cs),col,1.0f);
    }
}

static inline ImU32 col(const float c[4]) {
    return IM_COL32((int)(c[0]*255),(int)(c[1]*255),(int)(c[2]*255),(int)(c[3]*255));
}

// ---------- main ----------
void visuals::run()
{
    if (!IL2CPP::Init()) return;
    if (!visuals::esp_enabled) return;
    if (!api.init()) return;

    // 1) Кэш списка игроков (0.5s)
    static std::vector<uintptr_t> players;
    static auto lastRefresh = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<float>(now - lastRefresh).count() > 0.5f) {
        players = IL2CPP::FindObjectsOfType(api.pcKlass);
        lastRefresh = now;
    }
    if (players.empty()) return;

    // 2) Локальный игрок и его main-camera
    uintptr_t localPC = 0, localCam = 0;
    for (auto pc : players) {
        if (!pc) continue;
        if (isMine(pc)) { localPC = pc; break; }
    }
    if (!localPC) return;
    localCam = IL2CPP::Read<uintptr_t>(localPC + 0x30); // _mainCamera
    if (!localCam) return;

    Vec3 localFeet = netPos(localPC);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const float sH = io.DisplaySize.y;
    const float sW = io.DisplaySize.x;

    // 3) Рисуем остальных
    for (auto pc : players) {
        if (!pc || pc == localPC) continue;
        if (IL2CPP::Read<bool>(pc + 0xA8)) continue; // _isDeath

        Vec3 feet = netPos(pc);
        if (feet.x == 0 && feet.y == 0 && feet.z == 0) continue;
        Vec3 head = { feet.x, feet.y + 1.6f, feet.z }; // PlayerHull.StandView = 1.6

        Vec2 hs, fs;
        if (!w2s(localCam, head, hs, sH)) continue;
        if (!w2s(localCam, feet, fs, sH)) continue;

        float h = fs.y - hs.y;
        if (h < 2.0f || h > sH * 2.0f) continue;
        float w = h * 0.45f;
        float x = hs.x - w * 0.5f;
        float y = hs.y;

        // Box
        if (visuals::box2D) {
            if (visuals::boxFill) {
                dl->AddRectFilled(ImVec2(x, y), ImVec2(x+w, y+h),
                                  col(visuals::box_fill_color));
            }
            drawBox(dl, x, y, w, h, col(visuals::box_color));
        }

        // Health bar
        if (visuals::healthBar) {
            int hp = health(pc);
            float frac = hp / 100.0f;
            if (frac < 0) frac = 0;
            if (frac > 1) frac = 1;
            float bx = x - 5, bh = h * frac;
            dl->AddRectFilled(ImVec2(bx, y + (h - bh)), ImVec2(bx+3, y+h),
                              col(visuals::healthbar_color));
            dl->AddRect(ImVec2(bx, y), ImVec2(bx+3, y+h), IM_COL32(0,0,0,180));

            if (visuals::healthBarText) {
                char buf[16]; snprintf(buf, sizeof(buf), "%d", hp);
                dl->AddText(ImVec2(bx - 22, y + h*0.5f - 6), IM_COL32_WHITE, buf);
            }
        }

        // Name
        if (visuals::names) {
            std::string n = nick(pc);
            if (!n.empty()) {
                ImVec2 sz = ImGui::CalcTextSize(n.c_str());
                dl->AddText(ImVec2(x + w*0.5f - sz.x*0.5f, y - sz.y - 2),
                            col(visuals::names_color), n.c_str());
            }
        }

        // Distance
        if (visuals::distance) {
            float dx = feet.x - localFeet.x;
            float dy = feet.y - localFeet.y;
            float dz = feet.z - localFeet.z;
            char buf[24];
            snprintf(buf, sizeof(buf), "%.0fm", sqrtf(dx*dx + dy*dy + dz*dz));
            ImVec2 sz = ImGui::CalcTextSize(buf);
            dl->AddText(ImVec2(x + w*0.5f - sz.x*0.5f, y + h + 2),
                        col(visuals::distance_color), buf);
        }

        // Tracer (от низа экрана к голове)
        if (visuals::tracer) {
            dl->AddLine(ImVec2(sW*0.5f, sH), ImVec2(hs.x, hs.y),
                        col(visuals::tracer_color), 1.0f);
        }
    }
}