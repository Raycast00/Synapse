#include "aim.h"
#include "imgui.h"
#include "il2cpp/il2cpp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace il = IL2CPP;

namespace
{
    struct V3 { float x, y, z; };

    constexpr bool  kStatus = true;          // строка диагностики на экране; false = выключить
    constexpr float kR2D    = 57.2957795f;

    // Источник оффсетов: dump.cs, присланный пользователем (Assembly-CSharp).
    namespace Cfg
    {
        constexpr uintptr_t PC_IsRagdolling = 0x49;  // PlayerController.IsRagdolling
        constexpr uintptr_t PC_MainCamera   = 0x30;  // PlayerController._mainCamera
        constexpr uintptr_t PC_Player       = 0x90;  // PlayerController._player
        constexpr uintptr_t PC_IsDeath      = 0xA8;  // PlayerController._isDeath
        constexpr uintptr_t Player_IsLocal  = 0x1C;  // Photon.Realtime.Player.IsLocal
        constexpr uintptr_t Skel_HeadBone   = 0xC0;  // PlayerSkeleton._headBone (Rigidbody), запасной вариант
        constexpr uintptr_t Skel_ChestBone  = 0xC8;  // PlayerSkeleton._chestBone (Rigidbody)
        constexpr uintptr_t Skel_PelvisBone = 0xD0;  // PlayerSkeleton._pelvisBone (Rigidbody), запасной вариант
        constexpr uintptr_t Cmc_Pitch       = 0x3C;  // CameraMovementController.pitch
        constexpr uintptr_t Cmc_Yaw         = 0x40;  // CameraMovementController.yaw

        constexpr double RefreshInterval = 0.25;     // период пересборки списка целей, сек
        constexpr double CalibWindow     = 0.20;     // окно калибровки, сек
        constexpr float  MinFieldDelta   = 0.30f;    // минимальное изменение поля для калибровочного сэмпла
        constexpr float  UncalibStep     = 0.25f;    // макс. шаг записи в поле (ед. поля) до калибровки
        constexpr int    CalibNeeded     = 5;        // сэмплов для "откалибровано"
    }

    struct Api
    {
        bool ready = false;
        std::string missing;
        void *kPC{}, *kPlayer{}, *kSkel{}, *kCmc{};
        void *mGetTransform{}, *mGetCompInChildren{}, *mGetPos{}, *mGetFwd{}, *mW2S{}, *mCamMain{};
        void *mScreenW{}, *mScreenH{}, *mGetHeadBone{};
        void *mTeam{}, *mIsAlive{}, *mHealth{};
    } A;

    bool ResolveApi()
    {
        if (A.ready) return true;
        A.missing.clear();
        const char* core = "UnityEngine.CoreModule";
        auto need = [&](void* p, const char* n) { if (!p) { A.missing += n; A.missing += ' '; } };

        A.kPC     = il::Class("", "PlayerController", "Assembly-CSharp");
        A.kSkel   = il::Class("ScarletWork.Game", "PlayerSkeleton", "Assembly-CSharp");
        A.kCmc    = il::Class("ScarletWork.Game.Camera", "CameraMovementController", "Assembly-CSharp");
        A.kPlayer = il::Class("Photon.Realtime", "Player", "PhotonRealtime");

        void* kComponent = il::Class("UnityEngine", "Component", core);
        void* kTransform = il::Class("UnityEngine", "Transform", core);
        void* kCamera    = il::Class("UnityEngine", "Camera", core);
        void* kScreen    = il::Class("UnityEngine", "Screen", core);

        A.mGetTransform      = il::Method(kComponent, "get_transform", 0);
        A.mGetCompInChildren = il::Method(kComponent, "GetComponentInChildren", 2);
        A.mGetPos            = il::Method(kTransform, "get_position", 0);
        A.mGetFwd            = il::Method(kTransform, "get_forward", 0);
        A.mW2S               = il::Method(kCamera, "WorldToScreenPoint", 1);
        A.mCamMain           = il::Method(kCamera, "get_main", 0);
        A.mScreenW           = il::Method(kScreen, "get_width", 0);
        A.mScreenH           = il::Method(kScreen, "get_height", 0);
        A.mGetHeadBone       = il::Method(A.kSkel, "GetHeadBone", 0);   // опционально
        A.mTeam              = il::Method(A.kPlayer, "get_Team", 0);    // опционально
        A.mIsAlive           = il::Method(A.kPlayer, "get_IsAlive", 0); // опционально
        A.mHealth            = il::Method(A.kPC, "get_Health", 0);      // опционально

        need(A.kPC, "PlayerController"); need(A.kSkel, "PlayerSkeleton"); need(A.kCmc, "CameraMovementController");
        need(A.mGetTransform, "get_transform"); need(A.mGetCompInChildren, "GetComponentInChildren/2");
        need(A.mGetPos, "get_position"); need(A.mGetFwd, "get_forward"); need(A.mW2S, "WorldToScreenPoint");
        need(A.mScreenW, "get_width"); need(A.mScreenH, "get_height");

        A.ready = A.missing.empty();
        return A.ready;
    }

    std::string Str(void* s)
    {
        uintptr_t p = (uintptr_t)s;
        if (!il::ValidPtr(p)) return {};
        int32_t len = il::Read<int32_t>(p + 0x10);
        if (len <= 0 || len > 64) return {};
        std::string out;
        for (int32_t i = 0; i < len; i++) {
            uint16_t c = il::Read<uint16_t>(p + 0x14 + i * 2);
            out += (c < 0x80) ? (char)c : '?';
        }
        return out;
    }

    bool Finite(const V3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
                                      std::fabs(v.x) < 1e6f && std::fabs(v.y) < 1e6f && std::fabs(v.z) < 1e6f; }

    bool CallInt(void* m, void* inst, int& out)
    {
        void* r = il::Invoke(m, inst, nullptr);
        if (!il::ValidPtr((uintptr_t)r)) return false;
        out = *(int*)((uint8_t*)r + 0x10);
        return true;
    }
    bool CallBool(void* m, void* inst, bool& out)
    {
        void* r = il::Invoke(m, inst, nullptr);
        if (!il::ValidPtr((uintptr_t)r)) return false;
        out = *((uint8_t*)r + 0x10) != 0;
        return true;
    }

    bool PosOfTransform(void* tr, V3& out)
    {
        if (!il::ValidPtr((uintptr_t)tr)) return false;
        out = il::InvokeBoxed<V3>(A.mGetPos, tr, nullptr);
        return Finite(out);
    }
    bool PosOfComponent(void* comp, V3& out)
    {
        void* tr = il::Invoke(A.mGetTransform, comp, nullptr);
        return PosOfTransform(tr, out);
    }

    // ---- Цели ----
    struct Cand { uintptr_t pc = 0, player = 0, skeleton = 0; std::string team; };

    struct World
    {
        std::vector<Cand> cands;
        uintptr_t camera = 0, cmc = 0;
        bool haveLocal = false;
        std::string localTeam;
        double last = -1.0;
        int nPc = 0, nLocal = 0, nNoSkel = 0;
    } W;

    void Refresh()
    {
        W.cands.clear();
        W.camera = 0; W.cmc = 0; W.haveLocal = false; W.localTeam.clear();
        W.nPc = W.nLocal = W.nNoSkel = 0;

        for (uintptr_t pc : il::FindObjectsOfType(A.kPC)) {
            W.nPc++;
            uintptr_t player = il::Read<uintptr_t>(pc + Cfg::PC_Player);
            if (!il::ValidPtr(player)) continue;

            std::string team = Str(il::Invoke(A.mTeam, (void*)player, nullptr));

            if (il::Read<uint8_t>(player + Cfg::Player_IsLocal)) {
                W.nLocal++;
                uintptr_t cam = il::Read<uintptr_t>(pc + Cfg::PC_MainCamera);
                if (il::ValidPtr(cam)) W.camera = cam;
                W.haveLocal = true;
                W.localTeam = team;
                continue;                                   // себя в цели не берём
            }

            bool inc = true;
            void* a[2] = { il::Type(A.kSkel), &inc };
            uintptr_t sk = (uintptr_t)il::Invoke(A.mGetCompInChildren, (void*)pc, a);
            if (!il::ValidPtr(sk)) { W.nNoSkel++; continue; }
            W.cands.push_back({ pc, player, sk, team });
        }

        for (uintptr_t c : il::FindObjectsOfType(A.kCmc))
            if (il::ValidPtr(c)) { W.cmc = c; break; }
    }

    bool IsDead(const Cand& c)
    {
        if (il::Read<uint8_t>(c.pc + Cfg::PC_IsDeath) || il::Read<uint8_t>(c.pc + Cfg::PC_IsRagdolling)) return true;
        bool alive = true;
        if (CallBool(A.mIsAlive, (void*)c.player, alive) && !alive) return true;
        int hp = 0;
        if (CallInt(A.mHealth, (void*)c.pc, hp) && hp <= 0) return true;
        return false;
    }

    double g_nextRoll = 0.0;
    bool   g_rollHead = true;

    bool AimPoint(const Cand& c, V3& out)
    {
        auto head = [&]() -> bool {
            if (A.mGetHeadBone) {
                void* t = il::Invoke(A.mGetHeadBone, (void*)c.skeleton, nullptr);
                if (PosOfTransform(t, out)) return true;
            }
            uintptr_t rb = il::Read<uintptr_t>(c.skeleton + Cfg::Skel_HeadBone);
            return il::ValidPtr(rb) && PosOfComponent((void*)rb, out);
        };
        auto chest = [&]() -> bool {
            uintptr_t rb = il::Read<uintptr_t>(c.skeleton + Cfg::Skel_ChestBone);
            if (il::ValidPtr(rb) && PosOfComponent((void*)rb, out)) return true;
            rb = il::Read<uintptr_t>(c.skeleton + Cfg::Skel_PelvisBone);
            return il::ValidPtr(rb) && PosOfComponent((void*)rb, out);
        };

        int mode = aim::current_aimbot_hitbox;
        bool wantHead = (mode == 0) || (mode == 2 && g_rollHead);
        return wantHead ? (head() || chest()) : (chest() || head());
    }

    bool W2S(uintptr_t cam, const V3& w, float sx, float sy, float screenH, ImVec2& out)
    {
        V3 p = w;
        void* a[1] = { &p };
        V3 s = il::InvokeBoxed<V3>(A.mW2S, (void*)cam, a);
        if (!Finite(s) || s.z <= 0.f) return false;
        out = ImVec2(s.x * sx, (screenH - s.y) * sy);   // Unity: начало снизу-слева, ImGui: сверху-слева
        return true;
    }

    // ---- Углы и калибровка ----
    struct Ang { float yaw, pitch; };   // градусы; yaw = atan2(x, z), pitch > 0 = вверх

    bool FromDir(const V3& d, Ang& out)
    {
        float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (!(len > 1e-4f)) return false;
        out.yaw   = std::atan2(d.x, d.z) * kR2D;
        out.pitch = std::asin(std::clamp(d.y / len, -1.f, 1.f)) * kR2D;
        return true;
    }
    float Wrap(float a) { while (a > 180.f) a -= 360.f; while (a < -180.f) a += 360.f; return a; }

    struct Sample { double t; float fy, fp, ay, ap; };

    struct Calib
    {
        float gY = 1.f, gP = -1.f;       // градусов реального поворота на единицу поля; начальные значения только стартовое допущение
        int   nY = 0, nP = 0, badY = 0, badP = 0;
        bool  have = false;
        float lastY = 0.f, lastP = 0.f, unwY = 0.f, unwP = 0.f;
        std::vector<Sample> hist;
        bool  Ok() const { return nY >= Cfg::CalibNeeded && nP >= Cfg::CalibNeeded; }
        bool  NoResp() const { return badY >= 10 || badP >= 10; }
        void  ResetTrack() { have = false; hist.clear(); }
    } C;

    void CalibUpdate(double now)
    {
        const Sample* ref = nullptr;
        for (const Sample& s : C.hist) if (now - s.t <= Cfg::CalibWindow) { ref = &s; break; }
        if (!ref || now - ref->t < Cfg::CalibWindow * 0.5) return;
        const Sample& cur = C.hist.back();

        auto upd = [&](float dF, float dA, float& g, int& n, int& bad) {
            if (std::fabs(dF) < Cfg::MinFieldDelta) return;
            float s = dA / dF;
            if (!std::isfinite(s)) return;
            if (std::fabs(s) < 0.02f) { bad++; return; }
            bad = 0;
            s = std::clamp(s, -200.f, 200.f);
            g = (n == 0) ? s : g + 0.15f * (s - g);
            n++;
        };
        upd(cur.fy - ref->fy, cur.ay - ref->ay, C.gY, C.nY, C.badY);
        upd(cur.fp - ref->fp, cur.ap - ref->ap, C.gP, C.nP, C.badP);
    }

    float SafeG(float g) { return std::fabs(g) < 0.05f ? (g < 0.f ? -0.05f : 0.05f) : g; }

    void Status(ImDrawList* dl, const char* t)
    {
        if (!kStatus) return;
        dl->AddText(ImVec2(11, 114), IM_COL32(0, 0, 0, 255), t);
        dl->AddText(ImVec2(10, 113), IM_COL32(255, 160, 0, 255), t);
    }

    void AimbotFrame(ImDrawList* dl, ImVec2 center)
    {
        char st[320];
        if (!ResolveApi()) { std::snprintf(st, sizeof st, "AIM: missing %s", A.missing.c_str()); Status(dl, st); return; }

        double now = ImGui::GetTime();
        if (W.last < 0.0 || now - W.last >= Cfg::RefreshInterval) { W.last = now; Refresh(); }
        if (now >= g_nextRoll) { g_nextRoll = now + 1.0; g_rollHead = (std::rand() & 1) != 0; }

        uintptr_t cam = W.camera;
        if (!il::ValidPtr(cam)) {
            void* c = il::Invoke(A.mCamMain, nullptr, nullptr);
            if (il::ValidPtr((uintptr_t)c)) cam = (uintptr_t)c;
        }
        if (!il::ValidPtr(cam) || !il::ValidPtr(W.cmc)) {
            std::snprintf(st, sizeof st, "AIM: pc=%d loc=%d cam=%d cmc=%d (нет локальной камеры или CameraMovementController)",
                          W.nPc, W.nLocal, il::ValidPtr(cam), il::ValidPtr(W.cmc));
            Status(dl, st);
            C.ResetTrack();
            return;
        }

        int sw = il::InvokeBoxed<int>(A.mScreenW, nullptr, nullptr);
        int sh = il::InvokeBoxed<int>(A.mScreenH, nullptr, nullptr);
        if (sw <= 0 || sh <= 0) return;
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        float sx = disp.x / (float)sw, sy = disp.y / (float)sh;

        // Поза камеры.
        void* camTr = il::Invoke(A.mGetTransform, (void*)cam, nullptr);
        if (!il::ValidPtr((uintptr_t)camTr)) return;
        V3 eye = il::InvokeBoxed<V3>(A.mGetPos, camTr, nullptr);
        V3 fwd = il::InvokeBoxed<V3>(A.mGetFwd, camTr, nullptr);
        Ang cur{};
        if (!Finite(eye) || !Finite(fwd) || !FromDir(fwd, cur)) return;

        // Поля углов и сэмпл калибровки.
        float fy = il::Read<float>(W.cmc + Cfg::Cmc_Yaw);
        float fp = il::Read<float>(W.cmc + Cfg::Cmc_Pitch);
        if (!std::isfinite(fy) || !std::isfinite(fp)) return;

        if (!C.have) { C.unwY = cur.yaw; C.unwP = cur.pitch; C.have = true; }
        else         { C.unwY += Wrap(cur.yaw - C.lastY); C.unwP += (cur.pitch - C.lastP); }
        C.lastY = cur.yaw; C.lastP = cur.pitch;

        C.hist.push_back({ now, fy, fp, C.unwY, C.unwP });
        while (!C.hist.empty() && now - C.hist.front().t > 0.6) C.hist.erase(C.hist.begin());
        CalibUpdate(now);

        // Выбор цели: ближайшая к центру в пределах FOV.
        const Cand* best = nullptr; V3 bestPos{}; float bestD = aim::fov_radius; int inFov = 0;
        for (const Cand& c : W.cands) {
            if (W.haveLocal && !W.localTeam.empty() && c.team == W.localTeam) continue;
            if (IsDead(c)) continue;
            V3 p;
            if (!AimPoint(c, p)) continue;
            ImVec2 s;
            if (!W2S(cam, p, sx, sy, (float)sh, s)) continue;
            float d = std::hypot(s.x - center.x, s.y - center.y);
            if (d < aim::fov_radius) inFov++;
            if (d < bestD) { bestD = d; best = &c; bestPos = p; }
        }

        float eY = 0.f, eP = 0.f;
        bool wrote = false;
        if (best && !C.NoResp()) {
            Ang des{};
            V3 dir{ bestPos.x - eye.x, bestPos.y - eye.y, bestPos.z - eye.z };
            if (FromDir(dir, des)) {
                eY = Wrap(des.yaw - cur.yaw);
                eP = des.pitch - cur.pitch;

                float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.001f, 0.1f);
                float s = aim::aimbot_smoothing;
                float alpha = (s <= 1.f) ? 1.f : 1.f - std::pow(1.f - 1.f / s, dt * 60.f);

                float dFy = (eY * alpha) / SafeG(C.gY);
                float dFp = (eP * alpha) / SafeG(C.gP);
                if (!C.Ok()) {
                    dFy = std::clamp(dFy, -Cfg::UncalibStep, Cfg::UncalibStep);
                    dFp = std::clamp(dFp, -Cfg::UncalibStep, Cfg::UncalibStep);
                }
                if (std::isfinite(dFy) && std::isfinite(dFp) && (std::fabs(eY) > 0.05f || std::fabs(eP) > 0.05f)) {
                    il::Write<float>(W.cmc + Cfg::Cmc_Yaw,   fy + dFy);
                    il::Write<float>(W.cmc + Cfg::Cmc_Pitch, fp + dFp);
                    wrote = true;
                }
            }
        }
        // aim::silent здесь не обрабатывается: для него нужен хук на выстрел, это отдельный шаг.

        std::snprintf(st, sizeof st,
            "AIM pc=%d loc=%d cand=%d fov=%d tgt=%d | gY=%.2f(%d) gP=%.2f(%d) %s | eY=%.1f eP=%.1f fy=%.2f fp=%.2f %s",
            W.nPc, W.nLocal, (int)W.cands.size(), inFov, best ? 1 : 0,
            C.gY, C.nY, C.gP, C.nP, C.Ok() ? "calib" : "UNCALIB",
            eY, eP, fy, fp, C.NoResp() ? "NO RESPONSE" : (wrote ? "writing" : "idle"));
        Status(dl, st);
    }
}

void aim::run()
{
    if (!il::Init()) return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    float screenWidth  = ImGui::GetIO().DisplaySize.x;
    float screenHeight = ImGui::GetIO().DisplaySize.y;
    ImVec2 center(screenWidth / 2.0f, screenHeight / 2.0f);

    if (aim::aimbot) AimbotFrame(draw, center);
    else             C.ResetTrack();

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
}