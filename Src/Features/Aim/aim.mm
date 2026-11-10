#include «aim.h»
#include «imgui.h»
#include «il2cpp/il2cpp.h»

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
        constexpr uintptr_t PC_PlayerLock   = 0x68;  // PlayerController._playerLock
        constexpr uintptr_t PC_Player       = 0x90;  // PlayerController._player
        constexpr uintptr_t PC_IsDeath      = 0xA8;  // PlayerController._isDeath
        constexpr uintptr_t PC_HiddenMask   = 0xC4;  // PlayerController._hiddenMask
        constexpr int HiddenByRound = 1, HiddenBySpectate = 2, HiddenByOcclusion = 4;   // const в PlayerController
        constexpr int HiddenAlways  = HiddenByRound | HiddenBySpectate;   // не про стены, блокирует всегда

        constexpr uintptr_t PL_PlayerBody   = 0x20;  // PlayerLock._playerBody (GameObject)
        constexpr uintptr_t PL_RotationX    = 0x40;  // PlayerLock.rotationX (public float, питч)

        constexpr uintptr_t Player_IsLocal  = 0x1C;  // Photon.Realtime.Player.IsLocal
        constexpr uintptr_t Skel_HeadBone   = 0xC0;  // PlayerSkeleton._headBone (Rigidbody), запасной вариант
        constexpr uintptr_t Skel_ChestBone  = 0xC8;  // PlayerSkeleton._chestBone (Rigidbody)
        constexpr uintptr_t Skel_PelvisBone = 0xD0;  // PlayerSkeleton._pelvisBone (Rigidbody), запасной вариант

        constexpr double RefreshInterval = 0.25;     // период пересборки списка целей, сек
        constexpr double CalibWindow     = 0.20;     // окно калибровки, сек
        constexpr float  MinDelta        = 0.30f;    // минимальное изменение для калибровочного сэмпла
        constexpr float  UncalibStepYaw  = 0.5f;     // макс. шаг по яву (градусы) до калибровки
        constexpr float  UncalibStepPit  = 0.25f;    // макс. шаг записи в rotationX до калибровки
        constexpr int    CalibNeeded     = 5;        // сэмплов для «откалибровано»

        // Не откалибровано, подбирать в игре.
        constexpr float  KeepRadiusMul    = 1.5f;    // держим текущую цель, пока она внутри fov_radius*этот множитель
        constexpr double LoseGraceSec    = 0.15;     // сколько секунд терпим потерю цели (окклюзия на кадр, моргание), прежде чем сбросить
        constexpr float  MaxDegPerSec    = 220.f;    // жёсткий потолок скорости поворота, не зависит от aimbot_smoothing
        constexpr double BadResetTimeout = 2.0;      // если NO RESPONSE держится дольше этого — сбросить счётчик самим (самовосстановление)
    }

    struct Api
    {
        bool ready = false;
        std::string missing;
        void *kPC{}, *kPlayer{}, *kSkel{};
        void *mGetTransform{}, *mGoGetTransform{}, *mGetCompInChildren{}, *mGetPos{}, *mGetFwd{}, *mRotate4{};
        void *mW2S{}, *mCamMain{}, *mScreenW{}, *mScreenH{}, *mGetHeadBone{};
        void *mTeam{}, *mIsAlive{}, *mHealth{};
    } A;

    bool ResolveApi()
    {
        if (A.ready) return true;
        A.missing.clear();
        const char* core = «UnityEngine.CoreModule»;
        auto need = [&](void* p, const char* n) { if (!p) { A.missing += n; A.missing += ‘ ‘; } };

        A.kPC     = il::Class(«», «PlayerController», «Assembly-CSharp»);
        A.kSkel   = il::Class(«ScarletWork.Game», «PlayerSkeleton», «Assembly-CSharp»);
        A.kPlayer = il::Class(«Photon.Realtime», «Player», «PhotonRealtime»);

        void* kComponent  = il::Class(«UnityEngine», «Component», core);
        void* kGameObject = il::Class(«UnityEngine», «GameObject», core);
        void* kTransform  = il::Class(«UnityEngine», «Transform», core);
        void* kCamera     = il::Class(«UnityEngine», «Camera», core);

        void* kScreen     = il::Class(«UnityEngine», «Screen», core);

        A.mGetTransform      = il::Method(kComponent, «get_transform», 0);
        A.mGoGetTransform    = il::Method(kGameObject, «get_transform», 0);
        A.mGetCompInChildren = il::Method(kComponent, «GetComponentInChildren», 2);
        A.mGetPos            = il::Method(kTransform, «get_position», 0);
        A.mGetFwd            = il::Method(kTransform, «get_forward», 0);
        A.mRotate4           = il::Method(kTransform, «Rotate», 4);      // Rotate(float x, float y, float z, Space)
        A.mW2S               = il::Method(kCamera, «WorldToScreenPoint», 1);
        A.mCamMain           = il::Method(kCamera, «get_main», 0);
        A.mScreenW           = il::Method(kScreen, «get_width», 0);
        A.mScreenH           = il::Method(kScreen, «get_height», 0);
        A.mGetHeadBone       = il::Method(A.kSkel, «GetHeadBone», 0);    // опционально
        A.mTeam              = il::Method(A.kPlayer, «get_Team», 0);     // опционально
        A.mIsAlive           = il::Method(A.kPlayer, «get_IsAlive», 0);  // опционально
        A.mHealth            = il::Method(A.kPC, «get_Health», 0);       // опционально

        need(A.kPC, «PlayerController»); need(A.kSkel, «PlayerSkeleton»);
        need(A.mGetTransform, «Component.get_transform»); need(A.mGoGetTransform, «GameObject.get_transform»);
        need(A.mGetCompInChildren, «GetComponentInChildren/2»); need(A.mGetPos, «get_position»);
        need(A.mGetFwd, «get_forward»); need(A.mRotate4, «Transform.Rotate/4»); need(A.mW2S, «WorldToScreenPoint»);
        need(A.mScreenW, «get_width»); need(A.mScreenH, «get_height»);

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
            out += (c < 0x80) ? (char)c : ‘?’;
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

    // -— Цели -—
    struct Cand { uintptr_t pc = 0, player = 0, skeleton = 0; std::string team; };

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

        uintptr_t prevLock = 0;   // слежение за сменой локального PlayerLock (новый матч/респавн игрока)
    } W;

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
            W.cands.push_back({ pc, player, sk, team });
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

    // Два разных бита: HiddenAlways (раунд не идёт / спектейт) не про стены и блокирует всегда.
    // HiddenByOcclusion — реальная стена/укрытие, её учитываем только при выключенном aim_through_walls.
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
        return wantHead ? (head()  chest()) : (chest()  head());
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

    // -— Углы и калибровка -—
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

    // cy: накопленный командованный поворот по яву (градусы), fp: значение rotationX,

    // ay/ap: реальные углы камеры (ay без разрыва через 180).
    struct Sample { double t; float cy, fp, ay, ap; };

    struct Calib
    {
        float gY = 1.f, gP = -1.f;       // реальных градусов на единицу команды; начальные значения только стартовое допущение
        int   nY = 0, nP = 0, badY = 0, badP = 0;
        double badSinceY = -1.0, badSinceP = -1.0;   // когда счётчик начал расти без перерыва на 0
        bool  have = false;
        float lastY = 0.f, lastP = 0.f, unwY = 0.f, unwP = 0.f, cumY = 0.f;
        std::vector<Sample> hist;
        bool  OkY() const { return nY >= Cfg::CalibNeeded; }
        bool  OkP() const { return nP >= Cfg::CalibNeeded; }
        bool  NoResp() const { return badY >= 10 || badP >= 10; }
        void  ResetTrack()
        {
            have = false; hist.clear(); cumY = 0.f;
            badY = badP = 0; badSinceY = badSinceP = -1.0;
        }
    } C;

    void CalibUpdate(double now)
    {
        const Sample* ref = nullptr;
        for (const Sample& s : C.hist) if (now - s.t <= Cfg::CalibWindow) { ref = &s; break; }
        if (!ref || now - ref->t < Cfg::CalibWindow * 0.5) return;
        const Sample& cur = C.hist.back();

        auto upd = [&](float dF, float dA, float& g, int& n, int& bad, double& badSince) {
            if (std::fabs(dF) < Cfg::MinDelta) return;
            float s = dA / dF;
            if (!std::isfinite(s)) return;
            if (std::fabs(s) < 0.02f) {
                if (bad == 0) badSince = now;
                bad++;
                // Самовосстановление: не держим «нет ответа» вечно, если корреляция так и не вернулась,
                // например из-за временной не наша камеры (конец раунда, киллкам и т.п.).
                if (badSince > 0.0 && now - badSince > Cfg::BadResetTimeout) { bad = 0; badSince = -1.0; }
                return;
            }
            bad = 0; badSince = -1.0;
            s = std::clamp(s, -200.f, 200.f);
            g = (n == 0) ? s : g + 0.08f * (s - g);
            n++;
        };
        upd(cur.cy - ref->cy, cur.ay - ref->ay, C.gY, C.nY, C.badY, C.badSinceY);
        upd(cur.fp - ref->fp, cur.ap - ref->ap, C.gP, C.nP, C.badP, C.badSinceP);
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
        if (!ResolveApi()) { std::snprintf(st, sizeof st, «AIM: missing %s», A.missing.c_str()); Status(dl, st); return; }

        double now = ImGui::GetTime();
        if (W.last < 0.0 || now - W.last >= Cfg::RefreshInterval) {
            W.last = now;
            Refresh();
            if (W.lock != W.prevLock) {           // новый матч/респавн локального игрока — старое состояние больше не актуально
                C.ResetTrack();
                W.lockedPc = 0;
                W.lostSince = -1.0;
                W.prevLock = W.lock;
            }
        }
        if (now >= g_nextRoll) { g_nextRoll = now + 1.0; g_rollHead = (std::rand() & 1) != 0; }

        uintptr_t cam = W.camera;
        if (!il::ValidPtr(cam)) {
            void* c = il::Invoke(A.mCamMain, nullptr, nullptr);
            if (il::ValidPtr((uintptr_t)c)) cam = (uintptr_t)c;
        }
        if (!il::ValidPtr(cam)  !il::ValidPtr(W.lock)  !il::ValidPtr(W.body)) {
            std::snprintf(st, sizeof st, «AIM: no local view (pc=%d loc=%d cam=%d lock=%d body=%d)»,
                          W.nPc, W.nLocal, il::ValidPtr(cam), il::ValidPtr(W.lock), il::ValidPtr(W.body));
            Status(dl, st);
            C.ResetTrack();
            W.lockedPc = 0; W.lostSince = -1.0;
            return;
        }

        int sw = il::InvokeBoxed<int>(A.mScreenW, nullptr, nullptr);
        int sh = il::InvokeBoxed<int>(A.mScreenH, nullptr, nullptr);
        if (sw <= 0 || sh <= 0) return;

        ImVec2 disp = ImGui::GetIO().DisplaySize;
        float sx = disp.x / (float)sw, sy = disp.y / (float)sh;

        // Поза камеры.
        void* camTr = il::Invoke(A.mGetTransform, (void*)cam, nullptr);
        void* bodyTr = il::Invoke(A.mGoGetTransform, (void*)W.body, nullptr);
        if (!il::ValidPtr((uintptr_t)camTr) || !il::ValidPtr((uintptr_t)bodyTr)) return;
        V3 eye = il::InvokeBoxed<V3>(A.mGetPos, camTr, nullptr);
        V3 fwd = il::InvokeBoxed<V3>(A.mGetFwd, camTr, nullptr);
        Ang cur{};
        if (!Finite(eye)  !Finite(fwd)  !FromDir(fwd, cur)) return;

        float fp = il::Read<float>(W.lock + Cfg::PL_RotationX);
        if (!std::isfinite(fp)) return;

        if (!C.have) { C.unwY = cur.yaw; C.unwP = cur.pitch; C.have = true; }
        else         { C.unwY += Wrap(cur.yaw - C.lastY); C.unwP += (cur.pitch - C.lastP); }
        C.lastY = cur.yaw; C.lastP = cur.pitch;

        C.hist.push_back({ now, C.cumY, fp, C.unwY, C.unwP });
        while (!C.hist.empty() && now - C.hist.front().t > 0.6) C.hist.erase(C.hist.begin());
        CalibUpdate(now);

        // Выбор цели: сначала пробуем удержать уже выбранную, чтобы не дёргаться на границе FOV.
        auto qualifies = [&](const Cand& c, ImVec2& s, float& d) -> bool {
            if (W.haveLocal && !W.localTeam.empty() && c.team == W.localTeam) return false;
            if (IsDead(c)) return false;
            if (IsAlwaysHidden(c)) return false;                                   // раунд ещё не идёт / спектейт — не про стены
            if (!aim::aim_through_walls && IsOccluded(c)) return false;            // а вот это про стены
            V3 p;
            if (!AimPoint(c, p)) return false;
            if (!W2S(cam, p, sx, sy, (float)sh, s)) return false;
            d = std::hypot(s.x - center.x, s.y - center.y);
            return true;
        };

        const Cand* best = nullptr; V3 bestPos{}; int inFov = 0, hid = 0;

        if (W.lockedPc) {
            for (const Cand& c : W.cands) {
                if (c.pc != W.lockedPc) continue;
                ImVec2 s; float d;
                if (qualifies(c, s, d) && d < aim::fov_radius * Cfg::KeepRadiusMul) {
                    V3 p; AimPoint(c, p);
                    best = &c; bestPos = p;
                    W.lostSince = -1.0;
                } else if (W.lostSince < 0.0) {
                    W.lostSince = now;
                }
                break;
            }
            if (!best && W.lostSince >= 0.0 && now - W.lostSince < Cfg::LoseGraceSec) {
                // в пределах льготного времени просто не трогаем прицел в этом кадре, цель не сбрасываем
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
                V3 p; AimPoint(c, p);
                best = &c; bestPos = p;
            }
        }
        if (best) W.lockedPc = best->pc;

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

                float maxStep = Cfg::MaxDegPerSec * dt;
                float wantY = std::clamp(eY * alpha, -maxStep, maxStep);
                float wantP = std::clamp(eP * alpha, -maxStep, maxStep);

                float dY  = wantY / SafeG(C.gY);    // градусы поворота тела

                float dFp = wantP / SafeG(C.gP);    // приращение rotationX
                if (!C.OkY()) dY  = std::clamp(dY,  -Cfg::UncalibStepYaw, Cfg::UncalibStepYaw);
                if (!C.OkP()) dFp = std::clamp(dFp, -Cfg::UncalibStepPit, Cfg::UncalibStepPit);

                if (std::isfinite(dY) && std::isfinite(dFp) && (std::fabs(eY) > 0.05f || std::fabs(eP) > 0.05f)) {
                    float x = 0.f, y = dY, z = 0.f;
                    int space = 0;                          // Space.World: поворот вокруг мировой оси Y
                    void* a[4] = { &x, &y, &z, &space };
                    il::Invoke(A.mRotate4, bodyTr, a);
                    C.cumY += dY;
                    il::Write<float>(W.lock + Cfg::PL_RotationX, fp + dFp);
                    wrote = true;
                }
            }
        }
        // aim::silent здесь не обрабатывается: для него нужен хук на выстрел, это отдельный шаг.

        std::snprintf(st, sizeof st,
            «AIM pc=%d loc=%d cand=%d fov=%d hid=%d tgt=%d lock=%d | gY=%.2f(%d) gP=%.2f(%d) bad=%d/%d | eY=%.1f eP=%.1f rx=%.1f %s»,
            W.nPc, W.nLocal, (int)W.cands.size(), inFov, hid, best ? 1 : 0, (int)(W.lockedPc != 0),
            C.gY, C.nY, C.gP, C.nP, C.badY, C.badP, eY, eP, fp,
            C.NoResp() ? «NO RESPONSE» : (wrote ? «writing» : «idle»));
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

    if (aim::aimbot) {
        AimbotFrame(draw, center);
    } else {
        C.ResetTrack();
        W.lockedPc = 0;
        W.lostSince = -1.0;
    }

    if (aim::draw_fov) {
        draw->AddCircle(center, aim::fov_radius, ImGui::ColorConvertFloat4ToU32({
            aim::fov_color[0], aim::fov_color[1], aim::fov_color[2], aim::fov_color[3]
        }), 64, 1.0f);
    }

    if (aim::anti_aim) {
        static void* cmcClass = IL2CPP::Class(«ScarletWork.Game.Camera», «CameraMovementController»);
        std::vector<uintptr_t> controllers = IL2CPP::FindObjectsOfType(cmcClass);
        if (!controllers.empty()) {
            uintptr_t cmc = controllers[0];
            IL2CPP::Write<float>(cmc + 0x40, aim::anti_yaw * (float)(M_PI / 180.0));
        }
    }
}