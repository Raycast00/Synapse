#include "visuals.h"
#include "il2cpp/il2cpp.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace il = IL2CPP;

namespace
{
    struct V3     { float x, y, z; };
    struct Bounds { V3 center; V3 extents; };

    // Источник: dump.cs, присланный пользователем (Assembly-CSharp, PhotonRealtime).
    // Это оффсеты полей экземпляра от начала объекта (заголовок Il2CppObject = 0x10 уже учтён в дампе).
    namespace Cfg
    {
        constexpr uintptr_t PC_MainCamera  = 0x30;  // PlayerController._mainCamera
        constexpr uintptr_t PC_Player      = 0x90;  // PlayerController._player
        constexpr uintptr_t PC_IsDeath     = 0xA8;  // PlayerController._isDeath
        constexpr uintptr_t Player_IsLocal = 0x1C;  // Photon.Realtime.Player.IsLocal
        constexpr uintptr_t Player_Nick    = 0x20;  // Photon.Realtime.Player.nickName
        constexpr uintptr_t Skel_Body      = 0x60;  // PlayerSkeleton._body (SkinnedMeshRenderer)
        constexpr uintptr_t Bomb_IsActive  = 0x18;  // Bomb._isActive

        constexpr double RefreshInterval = 0.20;    // период пересборки списка игроков, сек
        constexpr float  BoxWidthRatio   = 0.50f;   // ширина бокса = высота * ratio (подбирается глазами)
        constexpr float  MaxHealth       = 100.f;   // НЕ проверено по дампу: макс. HP для полоски
    }

    // ---- API Unity (всё из UnityEngine.CoreModule по dump.cs) ----
    struct Api
    {
        bool ready = false;
        void *kPlayerController{}, *kPlayer{}, *kSkeleton{}, *kBomb{};
        void *mGetTransform{}, *mGetPosition{}, *mW2S{}, *mCamMain{};
        void *mGetBounds{}, *mGetCompInChildren{}, *mObjGetName{};
        void *mScreenW{}, *mScreenH{};
        void *mPcHealth{}, *mPlayerTeam{}, *mSkelWeapon{}, *mBombTimeLeft{};
    } A;

    bool ResolveApi()
    {
        if (A.ready) return true;
        const char* core = "UnityEngine.CoreModule";

        A.kPlayerController = il::Class("", "PlayerController", "Assembly-CSharp");
        A.kSkeleton         = il::Class("", "PlayerSkeleton", "Assembly-CSharp");
        A.kBomb             = il::Class("ScarletWork.Game", "Bomb", "Assembly-CSharp");
        A.kPlayer           = il::Class("Photon.Realtime", "Player", "PhotonRealtime");

        void* kComponent = il::Class("UnityEngine", "Component", core);
        void* kTransform = il::Class("UnityEngine", "Transform", core);
        void* kCamera    = il::Class("UnityEngine", "Camera", core);
        void* kRenderer  = il::Class("UnityEngine", "Renderer", core);
        void* kScreen    = il::Class("UnityEngine", "Screen", core);
        void* kObject    = il::Class("UnityEngine", "Object", core);

        A.mGetTransform      = il::Method(kComponent, "get_transform", 0);
        A.mGetCompInChildren = il::Method(kComponent, "GetComponentInChildren", 2); // (Type, bool)
        A.mGetPosition       = il::Method(kTransform, "get_position", 0);
        A.mW2S               = il::Method(kCamera, "WorldToScreenPoint", 1);
        A.mCamMain           = il::Method(kCamera, "get_main", 0);
        A.mGetBounds         = il::Method(kRenderer, "get_bounds", 0);
        A.mScreenW           = il::Method(kScreen, "get_width", 0);
        A.mScreenH           = il::Method(kScreen, "get_height", 0);
        A.mObjGetName        = il::Method(kObject, "get_name", 0);
        A.mPcHealth          = il::Method(A.kPlayerController, "get_Health", 0);
        A.mPlayerTeam        = il::Method(A.kPlayer, "get_Team", 0);
        A.mSkelWeapon        = il::Method(A.kSkeleton, "get_Weapon", 0);
        A.mBombTimeLeft      = il::Method(A.kBomb, "get_TimeLeft", 0);

        A.ready = A.kPlayerController && A.kSkeleton && A.kBomb && A.kPlayer &&
                  A.mGetTransform && A.mGetCompInChildren && A.mGetPosition && A.mW2S &&
                  A.mCamMain && A.mGetBounds && A.mScreenW && A.mScreenH && A.mObjGetName &&
                  A.mPcHealth && A.mPlayerTeam && A.mSkelWeapon && A.mBombTimeLeft;
        return A.ready;
    }

    // Il2CppString: int32 length @0x10, UTF-16 chars @0x14 (стандартный layout IL2CPP, не из дампа).
    std::string Str(void* s)
    {
        uintptr_t p = (uintptr_t)s;
        if (!il::ValidPtr(p)) return {};
        int32_t len = il::Read<int32_t>(p + 0x10);
        if (len <= 0 || len > 64) return {};
        std::string out;
        for (int32_t i = 0; i < len; i++) {
            uint16_t c = il::Read<uint16_t>(p + 0x14 + i * 2);
            if (c < 0x80)       out += (char)c;
            else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
            else                { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
        }
        return out;
    }

    bool Finite(const V3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
                                      std::fabs(v.x) < 1e6f && std::fabs(v.y) < 1e6f && std::fabs(v.z) < 1e6f; }

    V3 WorldPos(void* component)
    {
        void* tr = il::Invoke(A.mGetTransform, component, nullptr);
        if (!il::ValidPtr((uintptr_t)tr)) return {0, 0, 0};
        return il::InvokeBoxed<V3>(A.mGetPosition, tr, nullptr);
    }

    // ---- Кэш сущностей ----
    struct Ent
    {
        uintptr_t pc = 0, skeleton = 0, body = 0;
        bool        dead = false;
        std::string name, team, weapon;
    };

    struct Frame
    {
        std::vector<Ent> ents;
        uintptr_t camera = 0;
        V3        localPos{};
        bool      haveLocal = false;
        std::string localTeam;
        uintptr_t bomb = 0;
        double    lastRefresh = -1.0;
    } F;

    void Refresh()
    {
        F.ents.clear();
        F.camera = 0; F.haveLocal = false; F.localTeam.clear(); F.bomb = 0;

        for (uintptr_t pc : il::FindObjectsOfType(A.kPlayerController)) {
            uintptr_t player = il::Read<uintptr_t>(pc + Cfg::PC_Player);
            if (!il::ValidPtr(player)) continue;

            std::string team = Str(il::Invoke(A.mPlayerTeam, (void*)player, nullptr));

            if (il::Read<uint8_t>(player + Cfg::Player_IsLocal)) {
                uintptr_t cam = il::Read<uintptr_t>(pc + Cfg::PC_MainCamera);
                if (il::ValidPtr(cam)) F.camera = cam;
                F.localPos  = WorldPos((void*)pc);
                F.localTeam = team;
                F.haveLocal = true;
                continue;
            }

            Ent e;
            e.pc   = pc;
            e.team = team;
            e.name = Str((void*)il::Read<uintptr_t>(player + Cfg::Player_Nick));

            bool inc = true;
            void* a[2] = { il::Type(A.kSkeleton), &inc };
            e.skeleton = (uintptr_t)il::Invoke(A.mGetCompInChildren, (void*)pc, a);
            if (!il::ValidPtr(e.skeleton)) continue;
            e.body = il::Read<uintptr_t>(e.skeleton + Cfg::Skel_Body);
            if (!il::ValidPtr(e.body)) continue;

            // Имя GameObject оружия. Что именно там лежит (id префаба или читаемое имя) — проверить в игре.
            void* wgo = il::Invoke(A.mSkelWeapon, (void*)e.skeleton, nullptr);
            if (il::ValidPtr((uintptr_t)wgo))
                e.weapon = Str(il::Invoke(A.mObjGetName, wgo, nullptr));

            F.ents.push_back(std::move(e));
        }

        if (!F.camera) {
            void* c = il::Invoke(A.mCamMain, nullptr, nullptr);   // запасной вариант: Camera.main
            if (il::ValidPtr((uintptr_t)c)) F.camera = (uintptr_t)c;
        }

        for (uintptr_t b : il::FindObjectsOfType(A.kBomb))
            if (il::Read<uint8_t>(b + Cfg::Bomb_IsActive)) { F.bomb = b; break; }
    }

    // Возвращает экранные координаты в точках ImGui (начало слева-сверху). false, если точка за камерой.
    bool W2S(const V3& w, float sx, float sy, float screenH, ImVec2& out)
    {
        V3 p = w;
        void* a[1] = { &p };
        V3 s = il::InvokeBoxed<V3>(A.mW2S, (void*)F.camera, a);
        if (!Finite(s) || s.z <= 0.f) return false;
        out = ImVec2(s.x * sx, (screenH - s.y) * sy);
        return true;
    }

    ImU32 Col(const float c[4]) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3])); }

    void Text(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* t, bool centered)
    {
        ImVec2 sz = ImGui::CalcTextSize(t);
        if (centered) pos.x -= sz.x * 0.5f;
        dl->AddText(ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 200), t);
        dl->AddText(pos, col, t);
    }

    void CornerBox(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col)
    {
        float lw = (b.x - a.x) * 0.25f, lh = (b.y - a.y) * 0.25f;
        dl->AddLine(a, ImVec2(a.x + lw, a.y), col);  dl->AddLine(a, ImVec2(a.x, a.y + lh), col);
        dl->AddLine(ImVec2(b.x, a.y), ImVec2(b.x - lw, a.y), col);  dl->AddLine(ImVec2(b.x, a.y), ImVec2(b.x, a.y + lh), col);
        dl->AddLine(ImVec2(a.x, b.y), ImVec2(a.x + lw, b.y), col);  dl->AddLine(ImVec2(a.x, b.y), ImVec2(a.x, b.y - lh), col);
        dl->AddLine(b, ImVec2(b.x - lw, b.y), col);  dl->AddLine(b, ImVec2(b.x, b.y - lh), col);
    }

    float g_bombMax = 0.f;
}

void visuals::run()
{
    if (!il::Init()) return;                 // сам ждёт готовности UnityFramework
    if (!esp_enabled) return;
    if (!ResolveApi()) return;

    double now = ImGui::GetTime();
    if (F.lastRefresh < 0.0 || now - F.lastRefresh >= Cfg::RefreshInterval) {
        F.lastRefresh = now;
        Refresh();
    }
    if (!il::ValidPtr(F.camera)) return;

    int sw = il::InvokeBoxed<int>(A.mScreenW, nullptr, nullptr);
    int sh = il::InvokeBoxed<int>(A.mScreenH, nullptr, nullptr);
    if (sw <= 0 || sh <= 0) return;

    ImVec2 disp = ImGui::GetIO().DisplaySize;
    float sx = disp.x / (float)sw, sy = disp.y / (float)sh;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    for (const Ent& e : F.ents) {
        if (F.haveLocal && !F.localTeam.empty() && e.team == F.localTeam) continue;  // свои
        if (il::Read<uint8_t>(e.pc + Cfg::PC_IsDeath)) continue;

        Bounds bb = il::InvokeBoxed<Bounds>(A.mGetBounds, (void*)e.body, nullptr);
        if (!Finite(bb.center) || !Finite(bb.extents)) continue;

        V3 feet{ bb.center.x, bb.center.y - bb.extents.y, bb.center.z };
        V3 head{ bb.center.x, bb.center.y + bb.extents.y, bb.center.z };
        ImVec2 sf, sh2;
        if (!W2S(feet, sx, sy, (float)sh, sf) || !W2S(head, sx, sy, (float)sh, sh2)) continue;

        float h = sf.y - sh2.y;
        if (h < 4.f || h > disp.y * 3.f) continue;
        float w = h * Cfg::BoxWidthRatio;
        ImVec2 a(sf.x - w * 0.5f, sh2.y), b(sf.x + w * 0.5f, sf.y);

        if (boxFill) dl->AddRectFilled(a, b, Col(box_fill_color));
        if (box2D) {
            if (current_box_type == 0) {
                dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), IM_COL32(0, 0, 0, 200));
                dl->AddRect(a, b, Col(box_color));
            } else CornerBox(dl, a, b, Col(box_color));
        }

        int hp = il::InvokeBoxed<int>(A.mPcHealth, (void*)e.pc, nullptr);
        if (healthBar || healthBarText) {
            float frac = std::clamp((float)hp / Cfg::MaxHealth, 0.f, 1.f);
            if (healthBar) {
                dl->AddRectFilled(ImVec2(a.x - 6, a.y - 1), ImVec2(a.x - 3, b.y + 1), IM_COL32(0, 0, 0, 180));
                dl->AddRectFilled(ImVec2(a.x - 5, b.y - h * frac), ImVec2(a.x - 4, b.y), Col(healthbar_color));
            }
            if (healthBarText) {
                char t[16]; std::snprintf(t, sizeof t, "%d", hp);
                Text(dl, ImVec2(a.x - 6 - ImGui::CalcTextSize(t).x, b.y - h * frac), IM_COL32(255, 255, 255, 255), t, false);
            }
        }

        float topY = a.y - ImGui::GetTextLineHeight() - 2.f;
        if (names && !e.name.empty()) { Text(dl, ImVec2(sf.x, topY), Col(names_color), e.name.c_str(), true); }

        float botY = b.y + 2.f;
        if (weaponText && !e.weapon.empty()) {
            Text(dl, ImVec2(sf.x, botY), Col(weapon_text_color), e.weapon.c_str(), true);
            botY += ImGui::GetTextLineHeight();
        }
        if (distance && F.haveLocal) {
            V3 p = WorldPos((void*)e.pc);
            float d = std::sqrt((p.x - F.localPos.x) * (p.x - F.localPos.x) +
                                (p.y - F.localPos.y) * (p.y - F.localPos.y) +
                                (p.z - F.localPos.z) * (p.z - F.localPos.z));
            char t[24]; std::snprintf(t, sizeof t, "%.0f u", d);   // единица "u": масштаб мира не подтверждён
            Text(dl, ImVec2(sf.x, botY), Col(distance_color), t, true);
        }
        if (tracer) dl->AddLine(ImVec2(disp.x * 0.5f, disp.y), sf, Col(tracer_color));
    }

    // Таймер бомбы. Полное время фьюза из дампа не читаю: максимум берётся по первому увиденному TimeLeft.
    if (bomb_timer_bar && il::ValidPtr(F.bomb) && il::Read<uint8_t>(F.bomb + Cfg::Bomb_IsActive)) {
        float left = il::InvokeBoxed<float>(A.mBombTimeLeft, (void*)F.bomb, nullptr);
        if (std::isfinite(left) && left > 0.f) {
            g_bombMax = std::max(g_bombMax, left);
            float frac = std::clamp(left / g_bombMax, 0.f, 1.f);
            float bw = disp.x * 0.3f, x0 = (disp.x - bw) * 0.5f, y0 = 24.f;
            dl->AddRectFilled(ImVec2(x0 - 1, y0 - 1), ImVec2(x0 + bw + 1, y0 + 7), IM_COL32(0, 0, 0, 180));
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + bw * frac, y0 + 6), Col(bomb_timer_color));
            char t[16]; std::snprintf(t, sizeof t, "%.1f", left);
            Text(dl, ImVec2(disp.x * 0.5f, y0 + 8), Col(bomb_timer_color), t, true);
        }
    } else g_bombMax = 0.f;
}