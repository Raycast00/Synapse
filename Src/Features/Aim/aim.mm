#include "aim.h"
#include "imgui.h"
#include "il2cpp/il2cpp.h"

#include "extrapolation/extrapolation.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace il = IL2CPP;

namespace
{
    struct V3 { float x, y, z; };

    constexpr bool  kStatus = true;
    constexpr float kR2D    = 57.2957795f;

    namespace Cfg
    {
        constexpr uintptr_t PC_IsRagdolling = 0x49;
        constexpr uintptr_t PC_MainCamera   = 0x30;
        constexpr uintptr_t PC_PlayerLock   = 0x68;
        constexpr uintptr_t PC_Player       = 0x90;
        constexpr uintptr_t PC_IsDeath      = 0xA8;
        constexpr uintptr_t PC_HiddenMask   = 0xC4;
        constexpr int HiddenByRound = 1, HiddenBySpectate = 2, HiddenByOcclusion = 4;
        constexpr int HiddenAlways  = HiddenByRound | HiddenBySpectate;

        constexpr uintptr_t PL_PlayerBody   = 0x20;
        constexpr uintptr_t PL_RotationX    = 0x40;

        constexpr uintptr_t Player_IsLocal  = 0x1C;
        constexpr uintptr_t Skel_Body       = 0x60;
        constexpr uintptr_t Skel_HeadBone   = 0xC0;
        constexpr uintptr_t Skel_ChestBone  = 0xC8;
        constexpr uintptr_t Skel_PelvisBone = 0xD0;

        constexpr double RefreshInterval = 0.25;
        constexpr float  KeepRadiusMul     = 1.5f;
        constexpr double LoseGraceSec      = 0.15;
        constexpr float  MaxDegPerSec      = 220.f;
        constexpr float  DefaultPitchLimit = 80.f;
        constexpr float  OverrideYawDegPerFrame = 12.f;
        constexpr double OverrideHoldSec        = 0.25;

        // Silent aim: PlayerController._armsController и ArmsController._cameraTransform, из dump.cs
        constexpr uintptr_t PC_ArmsController    = 0x60;
        constexpr uintptr_t Arms_CameraTransform = 0x178;
        // ArmsController._timeFired — подтверждено эмпирически (rage.mm): растёт только во время стрельбы.
        constexpr uintptr_t Arms_TimeFired       = 0x1BC;

        // PlayerNetwork — бэктрек/экстраполяция (dump.cs)
        constexpr uintptr_t Net_NetworkPosition      = 0xD4;  // networkPosition — текущая интерполированная позиция
        constexpr float     MaxExtrapolationDuration = 0.25f; // PlayerNetwork.MaxExtrapolationDuration, реальная константа
    }

    struct Api
    {
        bool ready = false;
        std::string missing;
        void *kPC{}, *kPlayer{}, *kSkel{}, *kPlayerLock{};
        void *mGetTransform{}, *mGoGetTransform{}, *mGetCompInChildren{}, *mGetPos{}, *mGetFwd{}, *mRotate4{};
        void *mW2S{}, *mCamMain{}, *mScreenW{}, *mScreenH{}, *mGetHeadBone{};
        void *mTeam{}, *mIsAlive{}, *mHealth{}, *mLookXLimit{};
        void *kSMR{}, *mGetBones{};
        void *kArms{}, *mShoot{}, *mCanShoot{};
        // Настоящие трейсы игры для проверки стен сайлента (dump.cs: SourceTrace, ScarletWork.Hit.BulletTrace)
        void *kSourceTrace{}, *fMask{};
        void *kBulletTrace{}, *mClearNearby{}, *mHardBlocked{};
        // PlayerNetwork — бэктрек/экстраполяция
        void *kNet{}, *mTryGetPos{}, *mRenderTime{}, *mSnapAge{}, *mRemoteVel{};
        void *kConVars{}, *fConFriction{}, *fConStopSpeed{}, *fConGravity{};
    } A;

    bool ResolveApi()
    {
        if (A.ready) return true;
        A.missing.clear();
        const char* core = "UnityEngine.CoreModule";
        auto need = [&](void* p, const char* n) { if (!p) { A.missing += n; A.missing += ' '; } };

        A.kPC         = il::Class("", "PlayerController", "Assembly-CSharp");
        A.kSkel       = il::Class("ScarletWork.Game", "PlayerSkeleton", "Assembly-CSharp");
        A.kPlayerLock = il::Class("ScarletWork.Game", "PlayerLock", "Assembly-CSharp");
        A.kPlayer     = il::Class("Photon.Realtime", "Player", "PhotonRealtime");
        A.kSMR        = il::Class("UnityEngine", "SkinnedMeshRenderer", core);
        A.mGetBones   = il::Method(A.kSMR, "get_bones", 0);

        A.kArms     = il::Class("", "ArmsController", "Assembly-CSharp");
        A.mShoot    = il::Method(A.kArms, "Shoot", 0);
        A.mCanShoot = il::Method(A.kArms, "get_CanShoot", 0);

        void* kComponent  = il::Class("UnityEngine", "Component", core);
        void* kGameObject = il::Class("UnityEngine", "GameObject", core);
        void* kTransform  = il::Class("UnityEngine", "Transform", core);
        void* kCamera     = il::Class("UnityEngine", "Camera", core);
        void* kScreen     = il::Class("UnityEngine", "Screen", core);

        A.mGetTransform      = il::Method(kComponent, "get_transform", 0);
        A.mGoGetTransform    = il::Method(kGameObject, "get_transform", 0);
        A.mGetCompInChildren = il::Method(kComponent, "GetComponentInChildren", 2);
        A.mGetPos            = il::Method(kTransform, "get_position", 0);
        A.mGetFwd            = il::Method(kTransform, "get_forward", 0);
        A.mRotate4           = il::Method(kTransform, "Rotate", 4);
        A.mW2S               = il::Method(kCamera, "WorldToScreenPoint", 1);
        A.mCamMain           = il::Method(kCamera, "get_main", 0);
        A.mScreenW           = il::Method(kScreen, "get_width", 0);
        A.mScreenH           = il::Method(kScreen, "get_height", 0);
        A.mGetHeadBone       = il::Method(A.kSkel, "GetHeadBone", 0);
        A.mTeam              = il::Method(A.kPlayer, "get_Team", 0);
        A.mIsAlive           = il::Method(A.kPlayer, "get_IsAlive", 0);
        A.mHealth            = il::Method(A.kPC, "get_Health", 0);
        A.mLookXLimit        = il::Method(A.kPlayerLock, "get_LookXLimit", 0);

        need(A.kPC, "PlayerController"); need(A.kSkel, "PlayerSkeleton"); need(A.kPlayerLock, "PlayerLock");
        need(A.mGetTransform, "Component.get_transform"); need(A.mGoGetTransform, "GameObject.get_transform");
        need(A.mGetCompInChildren, "GetComponentInChildren/2"); need(A.mGetPos, "get_position");
        need(A.mGetFwd, "get_forward"); need(A.mRotate4, "Transform.Rotate/4"); need(A.mW2S, "WorldToScreenPoint");
        need(A.mScreenW, "get_width"); need(A.mScreenH, "get_height");
        need(A.kArms, "ArmsController"); need(A.mShoot, "ArmsController.Shoot"); need(A.mCanShoot, "ArmsController.get_CanShoot");

        // Не в need() — опционально, используется только сайлентом; остальной аимбот должен жить без них.
        A.kSourceTrace = il::Class("", "SourceTrace", "Assembly-CSharp");
        A.fMask        = A.kSourceTrace ? il::Field(A.kSourceTrace, "Mask") : nullptr;
        A.kBulletTrace = il::Class("ScarletWork.Hit", "BulletTrace", "Assembly-CSharp");
        A.mClearNearby = il::Method(A.kBulletTrace, "ClearNearby", 2);
        A.mHardBlocked = il::Method(A.kBulletTrace, "HardBlocked", 3);

        A.kNet        = il::Class("", "PlayerNetwork", "Assembly-CSharp");
        A.mTryGetPos  = il::Method(A.kNet, "TryGetPosition", 2);
        A.mRenderTime = il::Method(A.kNet, "get_RenderTime", 0);
        A.mSnapAge    = il::Method(A.kNet, "get_SnapshotAge", 0);
        A.mRemoteVel  = il::Method(A.kNet, "get_RemoteVelocity", 0);

        A.kConVars      = il::Class("", "MovementConVars", "Assembly-CSharp");
        A.fConFriction  = A.kConVars ? il::Field(A.kConVars, "Friction")  : nullptr;
        A.fConStopSpeed = A.kConVars ? il::Field(A.kConVars, "StopSpeed") : nullptr;
        A.fConGravity   = A.kConVars ? il::Field(A.kConVars, "Gravity")   : nullptr;

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
    bool CallFloat(void* m, void* inst, float& out)
    {
        void* r = il::Invoke(m, inst, nullptr);
        if (!il::ValidPtr((uintptr_t)r)) return false;
        out = *(float*)((uint8_t*)r + 0x10);
        return true;
    }
    bool CallDouble(void* m, void* inst, double& out)
    {
        void* r = il::Invoke(m, inst, nullptr);
        if (!il::ValidPtr((uintptr_t)r)) return false;
        out = *(double*)((uint8_t*)r + 0x10);
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

    struct Cand
    {
        uintptr_t pc = 0, player = 0, skeleton = 0, headBone = 0;
        std::string team;
    };

    struct World
    {
        std::vector<Cand> cands;
        uintptr_t camera = 0, lock = 0, body = 0;
        bool haveLocal = false;
        std::string localTeam;
        double last = -1.0;
        int nPc = 0, nLocal = 0, nNoSkel = 0;

        uintptr_t lockedPc = 0;
        double    lostSince = -1.0;
        uintptr_t prevLock = 0;

        double overrideUntil = -1.0;
        float  lastCmdY = 0.f;

        uintptr_t localPc = 0; // PlayerController локального игрока — нужен для доступа к ArmsController
    } W;

    uintptr_t PickHeadBone(uintptr_t body)
    {
        if (!A.mGetBones || !il::ValidPtr(body)) return 0;
        uintptr_t arr = (uintptr_t)il::Invoke(A.mGetBones, (void*)body, nullptr);
        if (!il::ValidPtr(arr)) return 0;
        uint64_t len = il::Read<uint64_t>(arr + 0x18);
        if (len == 0 || len > 512) return 0;

        uintptr_t best = 0; float bestY = -1e9f;
        for (uint64_t i = 0; i < len; i++) {
            uintptr_t t = il::Read<uintptr_t>(arr + 0x20 + i * 8);
            if (!il::ValidPtr(t)) continue;
            V3 p;
            if (!PosOfTransform((void*)t, p)) continue;
            if (p.y > bestY) { bestY = p.y; best = t; }
        }
        return best;
    }

    void Refresh()
    {
        W.cands.clear();
        W.camera = W.lock = W.body = 0; W.haveLocal = false; W.localTeam.clear();
        W.localPc = 0;
        W.nPc = W.nLocal = W.nNoSkel = 0;

        for (uintptr_t pc : il::FindObjectsOfType(A.kPC)) {
            W.nPc++;
            uintptr_t player = il::Read<uintptr_t>(pc + Cfg::PC_Player);
            if (!il::ValidPtr(player)) continue;

            std::string team = Str(il::Invoke(A.mTeam, (void*)player, nullptr));

            if (il::Read<uint8_t>(player + Cfg::Player_IsLocal)) {
                W.nLocal++;
                W.localPc = pc;
                uintptr_t cam = il::Read<uintptr_t>(pc + Cfg::PC_MainCamera);
                if (il::ValidPtr(cam)) W.camera = cam;
                uintptr_t lk = il::Read<uintptr_t>(pc + Cfg::PC_PlayerLock);
                if (il::ValidPtr(lk)) {
                    W.lock = lk;
                    uintptr_t body = il::Read<uintptr_t>(lk + Cfg::PL_PlayerBody);
                    if (il::ValidPtr(body)) W.body = body;
                }
                W.haveLocal = true;
                W.localTeam = team;
                continue;
            }

            bool inc = true;
            void* a[2] = { il::Type(A.kSkel), &inc };
            uintptr_t sk = (uintptr_t)il::Invoke(A.mGetCompInChildren, (void*)pc, a);
            if (!il::ValidPtr(sk)) { W.nNoSkel++; continue; }
            uintptr_t body = il::Read<uintptr_t>(sk + Cfg::Skel_Body);
            uintptr_t hb = PickHeadBone(body);
            Cand c;
            c.pc = pc;
            c.player = player;
            c.skeleton = sk;
            c.headBone = hb;
            c.team = team;
            W.cands.push_back(std::move(c));
        }
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

    bool IsAlwaysHidden(const Cand& c)
    {
        int m = il::Read<int>(c.pc + Cfg::PC_HiddenMask);
        return (m & Cfg::HiddenAlways) != 0;
    }

    double g_nextRoll = 0.0;
    bool   g_rollHead = true;

    enum class HeadSrc { None, Method, Heuristic, RawField, Chest };

    bool AimPoint(const Cand& c, V3& out, HeadSrc& src)
    {
        src = HeadSrc::None;
        auto head = [&]() -> bool {
            // Настоящий метод игры — приоритетный источник, он должен отслеживать голову в любой позе.
            if (A.mGetHeadBone) {
                void* t = il::Invoke(A.mGetHeadBone, (void*)c.skeleton, nullptr);
                if (PosOfTransform(t, out)) { src = HeadSrc::Method; return true; }
            }
            // Эвристика "самая высокая кость скелета" — только запасной вариант, нестабилен при приседе.
            if (il::ValidPtr(c.headBone) && PosOfTransform((void*)c.headBone, out)) { src = HeadSrc::Heuristic; return true; }
            uintptr_t rb = il::Read<uintptr_t>(c.skeleton + Cfg::Skel_HeadBone);
            if (il::ValidPtr(rb) && PosOfComponent((void*)rb, out)) { src = HeadSrc::RawField; return true; }
            return false;
        };
        auto chest = [&]() -> bool {
            uintptr_t rb = il::Read<uintptr_t>(c.skeleton + Cfg::Skel_ChestBone);
            if (il::ValidPtr(rb) && PosOfComponent((void*)rb, out)) { src = HeadSrc::Chest; return true; }
            rb = il::Read<uintptr_t>(c.skeleton + Cfg::Skel_PelvisBone);
            if (il::ValidPtr(rb) && PosOfComponent((void*)rb, out)) { src = HeadSrc::Chest; return true; }
            return false;
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
        out = ImVec2(s.x * sx, (screenH - s.y) * sy);
        return true;
    }

    struct Ang { float yaw, pitch; };

    bool FromDir(const V3& d, Ang& out)
    {
        float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (!(len > 1e-4f)) return false;
        out.yaw   = std::atan2(d.x, d.z) * kR2D;
        out.pitch = std::asin(std::clamp(d.y / len, -1.f, 1.f)) * kR2D;
        return true;
    }
    float Wrap(float a) { while (a > 180.f) a -= 360.f; while (a < -180.f) a += 360.f; return a; }

    struct Diag
    {
        float lastY = 0.f, lastP = 0.f;
        bool  have = false;
        float corrY = 0.f, corrP = 0.f;
        void  Reset() { have = false; }
    } D;

    void Status(ImDrawList* dl, ImVec2 pos, const char* t)
    {
        if (!kStatus) return;
        dl->AddText(ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 255), t);
        dl->AddText(pos, IM_COL32(255, 160, 0, 255), t);
    }

    enum class LOS { Clear, ThinWall, Blocked };

    // Проверка для сайлента: HardBlocked (ScarletWork.Hit.BulletTrace) — единственный судья
    // "стрелять вообще можно или нет" (непробиваемое — всегда LOS::Blocked, вне зависимости от
    // тогглов). Когда не хард-блок, дополнительно смотрим PC_HiddenMask/HiddenByOcclusion у
    // КОНКРЕТНОГО кандидата (candPc) — та же механика, что уже использует видимый аим под
    // aim_through_walls, но здесь это не гейт поиска цели, а отдельное деление: бит снят → Clear
    // (честно видно), бит установлен → ThinWall (не видно, но долетит — вуллбанг). ClearNearby
    // по-прежнему только для диагностики, в решении не участвует.
    LOS CheckWallbang(const V3& eye, const V3& target, uintptr_t candPc = 0,
                       bool* rawClear = nullptr, bool* rawHard = nullptr, bool* rawOcc = nullptr, int* maskOut = nullptr)
    {
        V3 from = eye, to = target;

        bool clear = false;
        if (A.mClearNearby) {
            void* a2[2] = { &from, &to };
            void* r = il::Invoke(A.mClearNearby, nullptr, a2);
            if (il::ValidPtr((uintptr_t)r)) clear = *((uint8_t*)r + 0x10) != 0;
        }
        if (rawClear) *rawClear = clear;

        int mask = A.fMask ? il::StaticField<int>(A.fMask) : -1;
        if (maskOut) *maskOut = mask;

        bool hard = false;
        if (A.mHardBlocked) {
            void* a3[3] = { &from, &to, &mask };
            void* r = il::Invoke(A.mHardBlocked, nullptr, a3);
            hard = il::ValidPtr((uintptr_t)r) ? (*((uint8_t*)r + 0x10) != 0) : true;
        }
        if (rawHard) *rawHard = hard;
        if (hard) return LOS::Blocked;

        bool occluded = false;
        if (candPc) {
            int m = il::Read<int>(candPc + Cfg::PC_HiddenMask);
            occluded = (m & Cfg::HiddenByOcclusion) != 0;
        }
        if (rawOcc) *rawOcc = occluded;
        return occluded ? LOS::ThinWall : LOS::Clear;
    }

    // Бэктрек/экстраполяция для уже выбранной цели сайлента. Сдвигаем не кость (историю скелета не
    // храним), а дельту корневой позиции networkPosition — применяем её к точке хитбокса, так
    // сохраняется поза. TryGetPosition/RenderTime/SnapshotAge/RemoteVelocity — реальные методы
    // PlayerNetwork (dump.cs), MaxExtrapolationDuration (0.25с) — реальная константа игры, не наша.
    bool AdjustAimPoint(uintptr_t candPc, V3& point, char* diagOut, size_t diagCap)
    {
        if (!A.kNet || !A.mTryGetPos || !A.mRenderTime) return false;

        bool inc = true;
        void* a[2] = { il::Type(A.kNet), &inc };
        uintptr_t net = (uintptr_t)il::Invoke(A.mGetCompInChildren, (void*)candPc, a);
        if (!il::ValidPtr(net)) return false;

        int backtrackIdx = -1;
        for (int i = 0; i < 4; i++)
            if (aim::silent_additional::silent_backtrack[i]) { backtrackIdx = i; break; }
        bool useExtrapolation = (backtrackIdx < 0) && (aim::silent_additional::silent_extrapolation > 0.01f);
        if (backtrackIdx < 0 && !useExtrapolation) return false;

        V3 curNetPos = il::Read<V3>(net + Cfg::Net_NetworkPosition);

        if (backtrackIdx >= 0) {
            double renderTime = 0.0;
            if (!CallDouble(A.mRenderTime, (void*)net, renderTime)) return false;

            float snapAge = 0.f;
            if (A.mSnapAge) CallFloat(A.mSnapAge, (void*)net, snapAge);
            static constexpr float kEffortMul[4] = { 1.0f, 1.5f, 2.0f, 3.0f };
            float rewind = std::clamp(snapAge * kEffortMul[backtrackIdx], 0.f, Cfg::MaxExtrapolationDuration);

            double targetTime = renderTime - (double)rewind;
            V3 outPos{};
            void* args[2] = { &targetTime, &outPos };
            void* r = il::Invoke(A.mTryGetPos, (void*)net, args);
            bool got = il::ValidPtr((uintptr_t)r) ? (*((uint8_t*)r + 0x10) != 0) : false;
            if (!got || !Finite(outPos)) return false;

            point.x += outPos.x - curNetPos.x;
            point.y += outPos.y - curNetPos.y;
            point.z += outPos.z - curNetPos.z;
            if (diagOut) std::snprintf(diagOut, diagCap, "BT %s age=%.2f rew=%.2f",
                                        aim::silent_additional::silent_backtrack_effort[backtrackIdx], snapAge, rewind);
            return true;
        }

                if (A.mRemoteVel) {
            V3 vel = il::InvokeBoxed<V3>(A.mRemoteVel, (void*)net, nullptr);
            if (!Finite(vel)) return false;

            float pct = aim::silent_additional::silent_extrapolation / 100.f;
            int32_t ticks = (int32_t)std::lround(pct * (Cfg::MaxExtrapolationDuration / extrapolation::MOVE_TICK));
            if (ticks <= 0) return false;

            extrapolation::MovementTuning tuning;
            tuning.friction  = A.fConFriction  ? il::StaticField<float>(A.fConFriction)  : 0.f;
            tuning.stopSpeed = A.fConStopSpeed ? il::StaticField<float>(A.fConStopSpeed) : 0.f;
            tuning.gravity   = A.fConGravity   ? il::StaticField<float>(A.fConGravity)   : 0.f;

            extrapolation::PlayerState st;
            st.position = { curNetPos.x, curNetPos.y, curNetPos.z };
            st.velocity = { vel.x, vel.y, vel.z };

            static extrapolation::Extrapolator extrap;
            auto res = extrap.extrapolate(st, ticks, tuning);
            if (!res.valid) return false;

            point.x += res.final_state.position.x - curNetPos.x;
            point.y += res.final_state.position.y - curNetPos.y;
            point.z += res.final_state.position.z - curNetPos.z;
            if (diagOut) std::snprintf(diagOut, diagCap, "EXP t=%d fr=%.2f gr=%.0f", res.ticks_extrapolated, tuning.friction, tuning.gravity);
            return true;
        }
        return false;
    }

    static float s_lastHeadY = 0.f;
    static bool  s_haveLastHeadY = false;

    // Сайлент: пишем угол, стреляем прямым вызовом Shoot(), восстанавливаем — синхронно, без хуков.
    // Yaw крутим только на bodyTr (камера в иерархии унаследует), pitch — только на camTr локально,
    // чтобы не задваивать поворот. armsCam и TimeFired — чистая диагностика в статус-строке.
    // Выбор цели + бэктрек/экстраполяция делаются снаружи, в AimbotFrame. candPc нужен для
    // повторной проверки стены/видимости (HardBlocked + HiddenByOcclusion) прямо перед выстрелом.
    bool SilentFire(void* camTr, void* bodyTr, const V3& eye, const Ang& cur,
                     uintptr_t candPc, const V3& targetPos, char* statusOut, size_t statusCap)
    {
        if (!A.kArms || !A.mShoot || !A.mCanShoot) {
            std::snprintf(statusOut, statusCap, "no Shoot API"); return false;
        }
        if (!il::ValidPtr(W.localPc)) { std::snprintf(statusOut, statusCap, "no local pc"); return false; }

        uintptr_t arms = il::Read<uintptr_t>(W.localPc + Cfg::PC_ArmsController);
        if (!il::ValidPtr(arms)) { std::snprintf(statusOut, statusCap, "no arms"); return false; }

        bool canShoot = false;
        if (!CallBool(A.mCanShoot, (void*)arms, canShoot) || !canShoot) {
            std::snprintf(statusOut, statusCap, "CanShoot=false"); return false;
        }

        // Повторная защитная проверка стены/видимости прямо перед выстрелом (на уже сдвинутой
        // бэктреком/экстраполяцией точке).
        LOS los = CheckWallbang(eye, targetPos, candPc);
        if (los == LOS::Blocked) {
            std::snprintf(statusOut, statusCap, "blocked (no LOS)"); return false;
        }
        if (los == LOS::ThinWall && !aim::aim_through_walls) {
            std::snprintf(statusOut, statusCap, "thin wall, toggle off"); return false;
        }

        V3 dir{ targetPos.x - eye.x, targetPos.y - eye.y, targetPos.z - eye.z };
        Ang des{};
        if (!FromDir(dir, des)) { std::snprintf(statusOut, statusCap, "bad dir"); return false; }

        float eY = Wrap(des.yaw - cur.yaw);
        float eP = des.pitch - cur.pitch;
        if (!std::isfinite(eY) || !std::isfinite(eP)) {
            std::snprintf(statusOut, statusCap, "non-finite delta"); return false;
        }

        float origFp = il::Read<float>(W.lock + Cfg::PL_RotationX);
        if (!std::isfinite(origFp)) { std::snprintf(statusOut, statusCap, "bad rotationX"); return false; }

        float limit = Cfg::DefaultPitchLimit;
        CallFloat(A.mLookXLimit, (void*)W.lock, limit);
        float newFp = std::clamp(origFp - eP, -limit, limit);

        uintptr_t armsCam = il::Read<uintptr_t>(arms + Cfg::Arms_CameraTransform);
        bool armsCamSame = (il::ValidPtr(armsCam) && (void*)armsCam == camTr);

        auto rotateWorldY = [&](void* tr, float deg) {
            if (!tr) return;
            float x = 0.f, y = deg, z = 0.f; int space = 0; // Space.World
            void* a[4] = { &x, &y, &z, &space };
            il::Invoke(A.mRotate4, tr, a);
        };
        auto rotateLocalX = [&](void* tr, float deg) {
            if (!tr) return;
            float x = deg, y = 0.f, z = 0.f; int space = 1; // Space.Self
            void* a[4] = { &x, &y, &z, &space };
            il::Invoke(A.mRotate4, tr, a);
        };

        rotateWorldY(bodyTr, eY);
        rotateLocalX(camTr, -eP);
        il::Write<float>(W.lock + Cfg::PL_RotationX, newFp);

        float tfBefore = il::Read<float>(arms + Cfg::Arms_TimeFired);
        il::Invoke(A.mShoot, (void*)arms, nullptr);
        float tfAfter = il::Read<float>(arms + Cfg::Arms_TimeFired);
        bool realShot = std::isfinite(tfBefore) && std::isfinite(tfAfter) && tfAfter != tfBefore;

        rotateWorldY(bodyTr, -eY);
        rotateLocalX(camTr, eP);
        il::Write<float>(W.lock + Cfg::PL_RotationX, origFp);

        std::snprintf(statusOut, statusCap, "fired(%s) eY=%.1f eP=%.1f cam=%s tf=%.2f->%.2f real=%d",
                      los == LOS::Clear ? "LOS" : "WB",
                      eY, eP, !il::ValidPtr(armsCam) ? "null" : (armsCamSame ? "same" : "DIFF"),
                      tfBefore, tfAfter, realShot);
        return true;
    }

    void AimbotFrame(ImDrawList* dl, ImVec2 center)
    {
        char st[480];
        if (!ResolveApi()) { std::snprintf(st, sizeof st, "AIM: missing %s", A.missing.c_str()); Status(dl, ImVec2(10,113), st); return; }

        double now = ImGui::GetTime();
        if (W.last < 0.0 || now - W.last >= Cfg::RefreshInterval) {
            W.last = now;
            Refresh();
            if (W.lock != W.prevLock) {
                D.Reset();
                W.lockedPc = 0; W.lostSince = -1.0; W.overrideUntil = -1.0; W.lastCmdY = 0.f;
                W.prevLock = W.lock;
            }
        }
        if (now >= g_nextRoll) { g_nextRoll = now + 1.0; g_rollHead = (std::rand() & 1) != 0; }

        uintptr_t cam = W.camera;
        if (!il::ValidPtr(cam)) {
            void* c = il::Invoke(A.mCamMain, nullptr, nullptr);
            if (il::ValidPtr((uintptr_t)c)) cam = (uintptr_t)c;
        }
        if (!il::ValidPtr(cam) || !il::ValidPtr(W.lock) || !il::ValidPtr(W.body)) {
            std::snprintf(st, sizeof st, "AIM: no local view (pc=%d loc=%d cam=%d lock=%d body=%d)",
                          W.nPc, W.nLocal, il::ValidPtr(cam), il::ValidPtr(W.lock), il::ValidPtr(W.body));
            Status(dl, ImVec2(10,113), st);
            D.Reset(); W.lockedPc = 0; W.lostSince = -1.0; W.overrideUntil = -1.0;
            return;
        }

        int sw = il::InvokeBoxed<int>(A.mScreenW, nullptr, nullptr);
        int sh = il::InvokeBoxed<int>(A.mScreenH, nullptr, nullptr);
        if (sw <= 0 || sh <= 0) return;
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        float sx = disp.x / (float)sw, sy = disp.y / (float)sh;

        void* camTr = il::Invoke(A.mGetTransform, (void*)cam, nullptr);
        void* bodyTr = il::Invoke(A.mGoGetTransform, (void*)W.body, nullptr);
        if (!il::ValidPtr((uintptr_t)camTr) || !il::ValidPtr((uintptr_t)bodyTr)) return;
        V3 eye = il::InvokeBoxed<V3>(A.mGetPos, camTr, nullptr);
        V3 fwd = il::InvokeBoxed<V3>(A.mGetFwd, camTr, nullptr);
        Ang cur{};
        if (!Finite(eye) || !Finite(fwd) || !FromDir(fwd, cur)) return;

        float fp = il::Read<float>(W.lock + Cfg::PL_RotationX);
        if (!std::isfinite(fp)) return;

        bool overriding = false;
        if (D.have) {
            float realDY = Wrap(cur.yaw - D.lastY);
            float userDY = realDY - W.lastCmdY;
            if (std::fabs(userDY) > Cfg::OverrideYawDegPerFrame) {
                W.overrideUntil = now + Cfg::OverrideHoldSec;
                W.lockedPc = 0;
                W.lostSince = -1.0;
            }
            if (std::fabs(realDY) > 1e-6f) D.corrY = userDY / realDY;
        }
        overriding = now < W.overrideUntil;
        D.lastY = cur.yaw; D.lastP = cur.pitch; D.have = true;

        bool silentActive = aim::silent && aim::aimbot;

        auto qualifies = [&](const Cand& c, ImVec2& s, float& d, V3& hitPos, HeadSrc& src) -> bool {
            if (W.haveLocal && !W.localTeam.empty() && c.team == W.localTeam) return false;
            if (IsDead(c)) return false;
            if (IsAlwaysHidden(c)) return false;
            if (!silentActive) {
                int m = il::Read<int>(c.pc + Cfg::PC_HiddenMask);
                if (!aim::aim_through_walls && (m & Cfg::HiddenByOcclusion)) return false;
            }
            // Для сайлента видимость/вуллбанг решает CheckWallbang (HardBlocked + occlusion) позже.
            if (!AimPoint(c, hitPos, src)) return false;
            if (!W2S(cam, hitPos, sx, sy, (float)sh, s)) return false;
            d = std::hypot(s.x - center.x, s.y - center.y);
            return true;
        };

        const Cand* best = nullptr; V3 bestPos{};
        HeadSrc bestSrc = HeadSrc::None;
        int inFov = 0;
        char silentStatus[96] = "idle";
        char btDiag[64] = "off";
        bool haveDiag = false; V3 diagPos{}; uintptr_t diagPc = 0;

        if (silentActive) {
            // Сайлент: замок на цель отключён (он залипал на цели за стеной и глушил стрельбу по остальным).
            // Собираем всех в FOV, сортируем по расстоянию до центра экрана (прицела) и берём ближайшую,
            // прошедшую CheckWallbang (хард-блок — никогда; тонкая стена — только если aim_through_walls).
            W.lockedPc = 0;
            W.lostSince = -1.0;

            struct SCand { const Cand* c; V3 pos; HeadSrc src; float d; };
            std::vector<SCand> list;
            for (const Cand& c : W.cands) {
                ImVec2 s; float d; V3 hp; HeadSrc src;
                if (!qualifies(c, s, d, hp, src)) continue;
                if (d >= aim::fov_radius) continue;
                list.push_back({ &c, hp, src, d });
            }
            inFov = (int)list.size();
            std::sort(list.begin(), list.end(), [](const SCand& a, const SCand& b) { return a.d < b.d; });

            int blockedCnt = 0;
            for (const SCand& sc : list) {
                LOS los = CheckWallbang(eye, sc.pos, sc.c->pc);
                if (los == LOS::Blocked) { blockedCnt++; continue; }
                if (los == LOS::ThinWall && !aim::aim_through_walls) { blockedCnt++; continue; }
                best = sc.c; bestPos = sc.pos; bestSrc = sc.src;
                break;
            }

            if (!list.empty()) {
                haveDiag = true;
                diagPos = best ? bestPos : list[0].pos;
                diagPc  = best ? best->pc : list[0].c->pc;
            }
            if (!best) {
                if (list.empty()) std::snprintf(silentStatus, sizeof silentStatus, "no target");
                else              std::snprintf(silentStatus, sizeof silentStatus, "all blocked (%d)", blockedCnt);
            } else {
                AdjustAimPoint(best->pc, bestPos, btDiag, sizeof btDiag);
            }
        } else {
            if (W.lockedPc) {
                for (const Cand& c : W.cands) {
                    if (c.pc != W.lockedPc) continue;
                    ImVec2 s; float d; V3 hp; HeadSrc src;
                    if (qualifies(c, s, d, hp, src) && d < aim::fov_radius * Cfg::KeepRadiusMul) {
                        best = &c; bestPos = hp; bestSrc = src;
                        W.lostSince = -1.0;
                    } else if (W.lostSince < 0.0) {
                        W.lostSince = now;
                    }
                    break;
                }
                if (!best && W.lostSince >= 0.0 && now - W.lostSince < Cfg::LoseGraceSec) {
                    // grace period
                } else if (!best) {
                    W.lockedPc = 0;
                    W.lostSince = -1.0;
                }
            }

            for (const Cand& c : W.cands) {
                if (c.pc == W.lockedPc) continue;

                ImVec2 s; float d; V3 hitPos; HeadSrc src;
                if (!qualifies(c, s, d, hitPos, src)) continue;
                if (d >= aim::fov_radius) continue;
                inFov++;

                if (!best) {
                    best = &c;
                    bestPos = hitPos;
                    bestSrc = src;
                }
            }
            if (best) {
                W.lockedPc = best->pc;
                haveDiag = true;
                diagPos = bestPos;
                diagPc  = best->pc;
            }
        }

        float eY = 0.f, eP = 0.f, cmdY = 0.f;
        bool wrote = false;

        if (silentActive) {
            if (best) wrote = SilentFire(camTr, bodyTr, eye, cur, best->pc, bestPos, silentStatus, sizeof silentStatus);
        } else if (best && !overriding) {
            Ang des{};
            V3 dir{ bestPos.x - eye.x, bestPos.y - eye.y, bestPos.z - eye.z };
            if (FromDir(dir, des)) {
                eY = Wrap(des.yaw - cur.yaw);
                eP = des.pitch - cur.pitch;

                float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.001f, 0.1f);
                float s = aim::aimbot_smoothing;
                float alpha = (s <= 1.f) ? 1.f : 1.f - std::pow(1.f - 1.f / s, dt * 60.f);

                float maxStep = Cfg::MaxDegPerSec * dt;
                float dY  = std::clamp(eY * alpha, -maxStep, maxStep);
                float dFp = -std::clamp(eP * alpha, -maxStep, maxStep);

                if (std::isfinite(dY) && std::isfinite(dFp) && (std::fabs(eY) > 0.05f || std::fabs(eP) > 0.05f)) {
                    float x = 0.f, y = dY, z = 0.f;
                    int space = 0;
                    void* a[4] = { &x, &y, &z, &space };
                    il::Invoke(A.mRotate4, bodyTr, a);

                    float newPitch = fp + dFp;
                    float limit = Cfg::DefaultPitchLimit;
                    CallFloat(A.mLookXLimit, (void*)W.lock, limit);
                    newPitch = std::clamp(newPitch, -limit, limit);
                    il::Write<float>(W.lock + Cfg::PL_RotationX, newPitch);

                    cmdY = dY;
                    wrote = true;
                }
            }
        }
        W.lastCmdY = cmdY;

        float headYDelta = 0.f;
        if (best) {
            if (s_haveLastHeadY) headYDelta = bestPos.y - s_lastHeadY;
            s_lastHeadY = bestPos.y;
            s_haveLastHeadY = true;
        } else {
            s_haveLastHeadY = false;
        }

        const char* srcName = "none";
        switch (bestSrc) {
            case HeadSrc::Method:    srcName = "Method";    break;
            case HeadSrc::Heuristic: srcName = "Heuristic"; break;
            case HeadSrc::RawField:  srcName = "RawField";  break;
            case HeadSrc::Chest:     srcName = "Chest";     break;
            default: break;
        }

        // Живая диагностика HardBlocked/ClearNearby/HiddenByOcclusion/Mask по текущей (или ближайшей,
        // если все заблокированы) цели.
        bool dCn = false, dHb = false, dOcc = false; int dMask = -1;
        if (haveDiag) CheckWallbang(eye, diagPos, diagPc, &dCn, &dHb, &dOcc, &dMask);

        int bestMask = best ? il::Read<int>(best->pc + Cfg::PC_HiddenMask) : -1;
        std::snprintf(st, sizeof st,
            "AIM pc=%d cand=%d fov=%d tgt=%d lock=%d ovr=%d wt=%d trace=%d cn=%d hb=%d occ=%d msk=%d | eY=%.1f eP=%.1f %s | headSrc=%s mask=0x%X headY=%.2f dY=%+.2f | silent=%s | %s",
            W.nPc, (int)W.cands.size(), inFov, best ? 1 : 0, (int)(W.lockedPc != 0),
            overriding ? 1 : 0,
            aim::aim_through_walls ? 1 : 0,
            A.mClearNearby != nullptr && A.mHardBlocked != nullptr,
            dCn, dHb, dOcc, dMask,
            eY, eP,
            overriding ? "OVERRIDE" : (wrote ? "writing" : "idle"),
            srcName, bestMask, best ? bestPos.y : 0.f, headYDelta,
            silentActive ? silentStatus : "off",
            silentActive ? btDiag : "");

        Status(dl, ImVec2(10, 113), st);
    }
}

void aim::run()
{
    if (!il::Init()) return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    float screenWidth  = ImGui::GetIO().DisplaySize.x;
    float screenHeight = ImGui::GetIO().DisplaySize.y;
    ImVec2 center(screenWidth / 2.0f, screenHeight / 2.0f);

    if (aim::aimbot) {
        AimbotFrame(draw, center);
    } else {
        D.Reset();
        W.lockedPc = 0;
        W.lostSince = -1.0;
        W.overrideUntil = -1.0;
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
}