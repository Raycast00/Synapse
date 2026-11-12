#include "aim.h"
#include "imgui.h"
#include "il2cpp/il2cpp.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "Dobby/include/dobby.h"

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
        constexpr uintptr_t PC_PlayerLock   = 0x68;  // PlayerController._playerLock
        constexpr uintptr_t PC_Player       = 0x90;  // PlayerController._player
        constexpr uintptr_t PC_IsDeath      = 0xA8;  // PlayerController._isDeath
        constexpr uintptr_t PC_HiddenMask   = 0xC4;  // PlayerController._hiddenMask
        constexpr int HiddenByRound = 1, HiddenBySpectate = 2, HiddenByOcclusion = 4;   // const в PlayerController
        constexpr int HiddenAlways  = HiddenByRound | HiddenBySpectate;   // не про стены, блокирует всегда

        constexpr uintptr_t PL_PlayerBody   = 0x20;  // PlayerLock._playerBody (GameObject)
        constexpr uintptr_t PL_RotationX    = 0x40;  // PlayerLock.rotationX (public float, питч, градусы Эйлера)

        constexpr uintptr_t Player_IsLocal  = 0x1C;  // Photon.Realtime.Player.IsLocal
        constexpr uintptr_t Skel_Body       = 0x60;  // PlayerSkeleton._body
        constexpr uintptr_t Skel_HeadBone   = 0xC0;  // PlayerSkeleton._headBone (предположительно Rigidbody), запасной вариант
        constexpr uintptr_t Skel_ChestBone  = 0xC8;  // PlayerSkeleton._chestBone (предположительно Rigidbody)
        constexpr uintptr_t Skel_PelvisBone = 0xD0;  // PlayerSkeleton._pelvisBone (предположительно Rigidbody), запасной вариант

        constexpr double RefreshInterval = 0.25;     // период пересборки списка целей, сек

        // Не откалибровано, подбирать в игре.
        constexpr float  KeepRadiusMul     = 1.5f;   // держим текущую цель, пока она внутри fov_radius*этот множитель
        constexpr double LoseGraceSec      = 0.15;   // сколько секунд терпим потерю цели (окклюзия на кадр, моргание), прежде чем сбросить
        constexpr float  MaxDegPerSec      = 220.f;  // жёсткий потолок скорости поворота по обеим осям
        constexpr float  DefaultPitchLimit = 80.f;   // запасной лимит питча, если get_LookXLimit недоступен

        // Детектор "ты сам резко увёл взгляд" — отпускаем цель и ставим коррекцию на паузу.
        constexpr float  OverrideYawDegPerFrame = 12.f;   // свой вклад за кадр больше этого — считаем сознательным уводом
        constexpr double OverrideHoldSec        = 0.25;   // на сколько пауза

        // Prediction (только для aimbot/silent): нет поля скорости снаряда в dump.cs (оружие похоже на хитскан),
        // поэтому компенсируем не баллистику, а задержку собственного сглаживания.
        constexpr float  PredictLeadSec  = 0.12f;
        constexpr float  MaxSaneSpeed    = 15.f;    // м/с, отсечка на случай мусора в Rigidbody.velocity

        // Silent aim: сколько времени кэшированная цель считается ещё свежей для хука FireRay.
        constexpr double SilentTargetMaxAge = 0.35;
    }

    struct Api
    {
        bool ready = false;
        std::string missing;
        void *kPC{}, *kPlayer{}, *kSkel{}, *kPlayerLock{}, *kRigidbody{};
        void *mGetTransform{}, *mGoGetTransform{}, *mGetCompInChildren{}, *mGetPos{}, *mGetFwd{}, *mRotate4{};
        void *mW2S{}, *mCamMain{}, *mScreenW{}, *mScreenH{}, *mGetHeadBone{};
        void *mTeam{}, *mIsAlive{}, *mHealth{}, *mLookXLimit{}, *mGetVelocity{};
        void *kSMR{}, *mGetBones{};
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
        A.kRigidbody  = il::Class("UnityEngine", "Rigidbody", "UnityEngine.PhysicsModule");   // опционально, для prediction
        A.kSMR        = il::Class("UnityEngine", "SkinnedMeshRenderer", core);
        A.mGetBones   = il::Method(A.kSMR, "get_bones", 0);   // опционально

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
        A.mRotate4           = il::Method(kTransform, "Rotate", 4);      // Rotate(float x, float y, float z, Space)
        A.mW2S               = il::Method(kCamera, "WorldToScreenPoint", 1);
        A.mCamMain           = il::Method(kCamera, "get_main", 0);
        A.mScreenW           = il::Method(kScreen, "get_width", 0);
        A.mScreenH           = il::Method(kScreen, "get_height", 0);
        A.mGetHeadBone       = il::Method(A.kSkel, "GetHeadBone", 0);          // опционально
        A.mTeam              = il::Method(A.kPlayer, "get_Team", 0);           // опционально
        A.mIsAlive           = il::Method(A.kPlayer, "get_IsAlive", 0);        // опционально
        A.mHealth            = il::Method(A.kPC, "get_Health", 0);             // опционально
        A.mLookXLimit        = il::Method(A.kPlayerLock, "get_LookXLimit", 0); // опционально
        A.mGetVelocity       = il::Method(A.kRigidbody, "get_velocity", 0);    // опционально, для prediction

        need(A.kPC, "PlayerController"); need(A.kSkel, "PlayerSkeleton"); need(A.kPlayerLock, "PlayerLock");
        need(A.mGetTransform, "Component.get_transform"); need(A.mGoGetTransform, "GameObject.get_transform");
        need(A.mGetCompInChildren, "GetComponentInChildren/2"); need(A.mGetPos, "get_position");
        need(A.mGetFwd, "get_forward"); need(A.mRotate4, "Transform.Rotate/4"); need(A.mW2S, "WorldToScreenPoint");
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
    bool CallFloat(void* m, void* inst, float& out)
    {
        void* r = il::Invoke(m, inst, nullptr);
        if (!il::ValidPtr((uintptr_t)r)) return false;
        out = *(float*)((uint8_t*)r + 0x10);
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

    // Скорость объекта, только если его реальный класс — Rigidbody (проверено рантаймом, не на веру).
    bool VelocityOfRigidbody(void* obj, V3& out)
    {
        if (!A.mGetVelocity || !A.kRigidbody || !il::ValidPtr((uintptr_t)obj)) return false;
        void* cls = il::ClassOf(obj);
        if (cls != A.kRigidbody) return false;   // не тот тип — не рискуем звать чужой метод
        out = il::InvokeBoxed<V3>(A.mGetVelocity, obj, nullptr);
        if (!Finite(out)) return false;
        float sp = std::sqrt(out.x * out.x + out.y * out.y + out.z * out.z);
        return sp <= Cfg::MaxSaneSpeed;
    }

    // ---- Silent aim: хук ArmsController.FireRay(Vector3 camPos, Vector3 forward, float distance) ----
    // RVA 0x124D1F4, dump.cs. Подменяем forward прямо перед вызовом оригинала, камеру не трогаем.
    namespace Silent
    {
        constexpr uintptr_t kFireRayRva = 0x124D1F4;

        struct Target
        {
            std::atomic<bool> valid{false};
            float x = 0.f, y = 0.f, z = 0.f;
            double updatedAt = -1.0;
        };
        Target g_target;

        // Сигнатура повторяет нативный вызов IL2CPP: this, Vector3 (HFA из 3 float) по значению,
        // ещё один Vector3, float, и скрытый хвостовой MethodInfo* — его IL2CPP добавляет всем методам.
        using FireRay_t = void (*)(void* self, V3 camPos, V3 forward, float distance, void* methodInfo);
        FireRay_t g_orig = nullptr;
        bool      g_hooked = false;

        void Fake(void* self, V3 camPos, V3 forward, float distance, void* methodInfo)
        {
            if (aim::silent && g_target.valid.load(std::memory_order_relaxed)) {
                double age = ImGui::GetTime() - g_target.updatedAt;
                if (age >= 0.0 && age < Cfg::SilentTargetMaxAge) {
                    V3 dir{ g_target.x - camPos.x, g_target.y - camPos.y, g_target.z - camPos.z };
                    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                    if (len > 1e-4f) forward = { dir.x / len, dir.y / len, dir.z / len };
                }
            }
            if (g_orig) g_orig(self, camPos, forward, distance, methodInfo);
        }

        bool Install()
        {
            if (g_hooked) return true;
            if (!il::Ready()) return false;
            void* base = il::ModuleBase();
            if (!base) return false;
            void* target = (void*)((uintptr_t)base + kFireRayRva);
            int rc = DobbyHook(target, (void*)Fake, (void**)&g_orig);
            if (rc != 0) return false;
            g_hooked = true;
            return true;
        }

        void Remove()
        {
            if (!g_hooked) return;
            void* base = il::ModuleBase();
            if (base) DobbyDestroy((void*)((uintptr_t)base + kFireRayRva));
            g_hooked = false;
            g_orig = nullptr;
        }

        void SetTarget(const V3& pos, double now)
        {
            g_target.x = pos.x; g_target.y = pos.y; g_target.z = pos.z;
            g_target.updatedAt = now;
            g_target.valid.store(true, std::memory_order_relaxed);
        }
        void Clear() { g_target.valid.store(false, std::memory_order_relaxed); }
    }

    // ---- Цели ----
    struct Cand { uintptr_t pc = 0, player = 0, skeleton = 0, headBone = 0; std::string team; };

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

        double overrideUntil = -1.0;   // до этого времени не пишем коррекцию — ты сам уводишь взгляд
        float  lastCmdY = 0.f;         // сколько реальных градусов по яву мы сами скомандовали прошлым кадром
    } W;

    uintptr_t PickHeadBone(uintptr_t body)
    {
        if (!A.mGetBones || !il::ValidPtr(body)) return 0;
        uintptr_t arr = (uintptr_t)il::Invoke(A.mGetBones, (void*)body, nullptr);
        if (!il::ValidPtr(arr)) return 0;
        uint64_t len = il::Read<uint64_t>(arr + 0x18);     // Il2CppArray: max_length @0x18, data @0x20
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
                uintptr_t lk = il::Read<uintptr_t>(pc + Cfg::PC_PlayerLock);
                if (il::ValidPtr(lk)) {
                    W.lock = lk;
                    uintptr_t body = il::Read<uintptr_t>(lk + Cfg::PL_PlayerBody);
                    if (il::ValidPtr(body)) W.body = body;
                }
                W.haveLocal = true;
                W.localTeam = team;
                continue;                                   // себя в цели не берём
            }

            bool inc = true;
            void* a[2] = { il::Type(A.kSkel), &inc };
            uintptr_t sk = (uintptr_t)il::Invoke(A.mGetCompInChildren, (void*)pc, a);
            if (!il::ValidPtr(sk)) { W.nNoSkel++; continue; }
            uintptr_t body = il::Read<uintptr_t>(sk + Cfg::Skel_Body);
            uintptr_t hb = PickHeadBone(body);
            W.cands.push_back({ pc, player, sk, hb, team });
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
    bool IsOccluded(const Cand& c)
    {
        int m = il::Read<int>(c.pc + Cfg::PC_HiddenMask);
        return (m & Cfg::HiddenByOcclusion) != 0;
    }

    double g_nextRoll = 0.0;
    bool   g_rollHead = true;

    // bone — указатель на объект, который дал позицию (если это реально Rigidbody, из него же берём скорость).
    bool AimPoint(const Cand& c, V3& out, void*& bone)
    {
        bone = nullptr;
        auto head = [&]() -> bool {
            if (il::ValidPtr(c.headBone) && PosOfTransform((void*)c.headBone, out)) { bone = (void*)c.headBone; return true; }
            if (A.mGetHeadBone) {
                void* t = il::Invoke(A.mGetHeadBone, (void*)c.skeleton, nullptr);
                if (PosOfTransform(t, out)) { bone = t; return true; }
            }
            uintptr_t rb = il::Read<uintptr_t>(c.skeleton + Cfg::Skel_HeadBone);
            if (il::ValidPtr(rb) && PosOfComponent((void*)rb, out)) { bone = (void*)rb; return true; }
            return false;
        };
        auto chest = [&]() -> bool {
            uintptr_t rb = il::Read<uintptr_t>(c.skeleton + Cfg::Skel_ChestBone);
            if (il::ValidPtr(rb) && PosOfComponent((void*)rb, out)) { bone = (void*)rb; return true; }
            rb = il::Read<uintptr_t>(c.skeleton + Cfg::Skel_PelvisBone);
            if (il::ValidPtr(rb) && PosOfComponent((void*)rb, out)) { bone = (void*)rb; return true; }
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
        out = ImVec2(s.x * sx, (screenH - s.y) * sy);   // Unity: начало снизу-слева, ImGui: сверху-слева
        return true;
    }

    // ---- Углы ----
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

    // Диагностика корреляции (только для статуса, в управление НЕ идёт — Transform.Rotate и rotationX
    // уже в реальных градусах, калибровка здесь лишняя и была источником перекручивания).
    struct Diag
    {
        float lastY = 0.f, lastP = 0.f;
        bool  have = false;
        float corrY = 0.f, corrP = 0.f;   // последняя измеренная "реальный угол / команда", только для чтения
        void  Reset() { have = false; }
    } D;

    void Status(ImDrawList* dl, const char* t)
    {
        if (!kStatus) return;
        dl->AddText(ImVec2(11, 114), IM_COL32(0, 0, 0, 255), t);
        dl->AddText(ImVec2(10, 113), IM_COL32(255, 160, 0, 255), t);
    }

    void AimbotFrame(ImDrawList* dl, ImVec2 center)
    {
        char st[360];
        if (!ResolveApi()) { std::snprintf(st, sizeof st, "AIM: missing %s", A.missing.c_str()); Status(dl, st); return; }

        double now = ImGui::GetTime();
        if (W.last < 0.0 || now - W.last >= Cfg::RefreshInterval) {
            W.last = now;
            Refresh();
            if (W.lock != W.prevLock) {           // новый матч/респавн локального игрока
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
            Status(dl, st);
            D.Reset(); W.lockedPc = 0; W.lostSince = -1.0; W.overrideUntil = -1.0;
            Silent::Clear();
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

        // Детектор "сам резко увёл взгляд": сравниваем реальное изменение яв за кадр с тем, что скомандовали мы сами.
        bool overriding = false;
        if (D.have) {
            float realDY = Wrap(cur.yaw - D.lastY);
            float userDY = realDY - W.lastCmdY;
            if (std::fabs(userDY) > Cfg::OverrideYawDegPerFrame) {
                W.overrideUntil = now + Cfg::OverrideHoldSec;
                W.lockedPc = 0;        // сразу отпускаем старую цель, дальше выбор пойдёт заново
                W.lostSince = -1.0;
            }
            if (std::fabs(realDY) > 1e-6f) D.corrY = userDY / realDY;   // чисто для статуса
        }
        overriding = now < W.overrideUntil;
        D.lastY = cur.yaw; D.lastP = cur.pitch; D.have = true;

        // Выбор цели: сначала пробуем удержать уже выбранную, чтобы не дёргаться на границе FOV.
        auto qualifies = [&](const Cand& c, ImVec2& s, float& d) -> bool {
            if (W.haveLocal && !W.localTeam.empty() && c.team == W.localTeam) return false;
            if (IsDead(c)) return false;
            if (IsAlwaysHidden(c)) return false;
            if (!aim::aim_through_walls && IsOccluded(c)) return false;
            V3 p; void* bone;
            if (!AimPoint(c, p, bone)) return false;
            if (!W2S(cam, p, sx, sy, (float)sh, s)) return false;
            d = std::hypot(s.x - center.x, s.y - center.y);
            return true;
        };

        const Cand* best = nullptr; V3 bestPos{}; void* bestBone = nullptr; int inFov = 0, hid = 0;

        if (W.lockedPc) {
            for (const Cand& c : W.cands) {
                if (c.pc != W.lockedPc) continue;
                ImVec2 s; float d;
                if (qualifies(c, s, d) && d < aim::fov_radius * Cfg::KeepRadiusMul) {
                    V3 p; void* bone; AimPoint(c, p, bone);
                    best = &c; bestPos = p; bestBone = bone;
                    W.lostSince = -1.0;
                } else if (W.lostSince < 0.0) {
                    W.lostSince = now;
                }
                break;
            }
            if (!best && W.lostSince >= 0.0 && now - W.lostSince < Cfg::LoseGraceSec) {
                // в пределах льготного времени просто не трогаем прицел в этом кадре
            } else if (!best) {
                W.lockedPc = 0;
                W.lostSince = -1.0;
            }
        }

        for (const Cand& c : W.cands) {
            if (c.pc == W.lockedPc) continue;
            ImVec2 s; float d;
            if (!qualifies(c, s, d)) continue;
            if (!aim::aim_through_walls && IsOccluded(c)) hid++;
            if (d < aim::fov_radius) inFov++;
            if (!best && d < aim::fov_radius) {
                V3 p; void* bone; AimPoint(c, p, bone);
                best = &c; bestPos = p; bestBone = bone;
            }
        }
        if (best) W.lockedPc = best->pc;

        bool predicted = false;
        if (best && aim::aimbot_prediction) {
            V3 vel;
            if (VelocityOfRigidbody(bestBone, vel)) {
                bestPos.x += vel.x * Cfg::PredictLeadSec;
                bestPos.y += vel.y * Cfg::PredictLeadSec;
                bestPos.z += vel.z * Cfg::PredictLeadSec;
                predicted = true;
            }
        }

        float eY = 0.f, eP = 0.f, cmdY = 0.f;
        bool wrote = false;
        if (best && !overriding) {
            Ang des{};
            V3 dir{ bestPos.x - eye.x, bestPos.y - eye.y, bestPos.z - eye.z };
            if (FromDir(dir, des)) {
                if (aim::silent) {
                    // Камеру не трогаем — только отдаём точку прицеливания хуку FireRay.
                    Silent::SetTarget(bestPos, now);
                    eY = Wrap(des.yaw - cur.yaw);
                    eP = des.pitch - cur.pitch;
                } else {
                    Silent::Clear();
                    eY = Wrap(des.yaw - cur.yaw);
                    eP = des.pitch - cur.pitch;

                    float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.001f, 0.1f);
                    float s = aim::aimbot_smoothing;
                    float alpha = (s <= 1.f) ? 1.f : 1.f - std::pow(1.f - 1.f / s, dt * 60.f);

                    float maxStep = Cfg::MaxDegPerSec * dt;
                    float dY  = std::clamp(eY * alpha, -maxStep, maxStep);    // реальные градусы, Rotate их и ждёт
                    float dFp = -std::clamp(eP * alpha, -maxStep, maxStep);   // минус: +X в Unity крутит камеру вниз

                    if (std::isfinite(dY) && std::isfinite(dFp) && (std::fabs(eY) > 0.05f || std::fabs(eP) > 0.05f)) {
                        float x = 0.f, y = dY, z = 0.f;
                        int space = 0;                          // Space.World
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
        } else {
            Silent::Clear();
        }
        W.lastCmdY = cmdY;

        std::snprintf(st, sizeof st,
            "AIM pc=%d loc=%d cand=%d fov=%d hid=%d tgt=%d lock=%d ovr=%d pred=%d sil=%d hook=%d | eY=%.1f eP=%.1f %s",
            W.nPc, W.nLocal, (int)W.cands.size(), inFov, hid, best ? 1 : 0, (int)(W.lockedPc != 0),
            overriding ? 1 : 0, predicted ? 1 : 0, (int)aim::silent, (int)Silent::g_hooked,
            eY, eP, overriding ? "OVERRIDE" : (aim::silent ? "silent" : (wrote ? "writing" : "idle")));
        Status(dl, st);
    }
}

void aim::run()
{
    if (!il::Init()) return;
    Silent::Install();   // один раз, безопасно вызывать каждый кадр — внутри своя защита от повторной установки

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
        Silent::Clear();
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