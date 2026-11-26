#import <Foundation/Foundation.h>
#include "rage.h"
#include "il2cpp/il2cpp.h"
#include <unordered_map>
#include <vector>
#include "imgui.h"
#include <cstdio>

using IL2CPP::Read;
using IL2CPP::Write;
using IL2CPP::ValidPtr;
static inline bool ok(uintptr_t p) { return ValidPtr(p); }

static uintptr_t catalogGet(uint8_t id)
{
    static void* klass  = IL2CPP::Class("", "WeaponCatalog");
    static void* method = IL2CPP::Method(klass, "Get", 1);
    void* args[1] = { &id };
    return (uintptr_t)IL2CPP::Invoke(method, nullptr, args);
}

static uintptr_t fpsCamera()
{
    static void* klass = IL2CPP::Class("", "PlayerFPSCamera");
    static void* field = IL2CPP::Field(klass, "<Local>k__BackingField");
    return IL2CPP::StaticField<uintptr_t>(field);
}

namespace off {
    // WeaponParameters (Assembly-CSharp, dump.cs: class WeaponParameters)
    constexpr uintptr_t scopedMoveInacc = 0x58;
    constexpr uintptr_t edgeSpreadShots = 0x5C;
    constexpr uintptr_t airInaccuracy   = 0x68;
    constexpr uintptr_t pelletSpread    = 0xA4;
    constexpr uintptr_t recoilParams    = 0x150;
    constexpr uintptr_t rechargeTime    = 0xB0;  // _rechargeTime — вероятный реальный gate скорострельности

    constexpr uintptr_t arms_AccuracyMult     = 0x114;
    constexpr uintptr_t arms_RecoilMult       = 0x110;
    constexpr uintptr_t arms_AccuracyAdditive = 0x11C;
    // AirInaccuracy: 0x10..0x33 = stand/jump/land/ladder (Vector2) + jumpInitial
    constexpr uintptr_t airStart  = 0x10;
    constexpr int       airFloats = 9;
    // RecoilParameters
    constexpr uintptr_t horizRange = 0x10;
    constexpr uintptr_t vertRange  = 0x14;
    // PlayerFPSCamera
    constexpr uintptr_t camKick[] = { 0x3C, 0x4C, 0x50, 0x54, 0x58, 0x74 };
    constexpr uintptr_t camPlayer = 0x118;   // PlayerFPSCamera._player (PlayerController)

    // PlayerController
    constexpr uintptr_t pcMovement = 0x40;   // PlayerController._movementController
    constexpr uintptr_t pcArms     = 0x60;   // PlayerController._armsController
    constexpr uintptr_t pc_IsDeath = 0xA8;   // PlayerController._isDeath

    // ArmsController. Источник имён: dump.cs, точная роль _timeFired/_nextDryFire в геймплее не прослежена.
    constexpr uintptr_t arms_MagazineCapacity = 0x1B8;  // short, текущий патрон в магазине
    constexpr uintptr_t arms_Capacity         = 0x1BA;  // short, текущий резерв
    constexpr uintptr_t arms_TimeFired        = 0x1BC;  // float
    constexpr uintptr_t arms_NextDryFire      = 0x1C0;  // float

    // ArmsController, живой накопленный кик отдачи (из dump.cs, подтверждено в отдельном разборе)
    constexpr uintptr_t arms_CamRecoilV  = 0x1CC;
    constexpr uintptr_t arms_CamRecoilH  = 0x1D0;
    constexpr uintptr_t arms_RecoilBaseV = 0x1DC;
    constexpr uintptr_t arms_RecoilBaseH = 0x1E0;

    // PlayerMovement
    constexpr uintptr_t pm_IsFreezing = 0x164;  // bool, читается в приватном get_Frozen()
    constexpr uintptr_t pm_IsGrounded = 0x15C;  // bool, public _isGrounded

    constexpr uintptr_t pm_Core            = 0x70; // PlayerMovement._core (SourceGameMovement)
    constexpr uintptr_t pm_Tuning          = 0x28; // PlayerMovement._movement (SourceMovementTuning)
    constexpr uintptr_t pm_CameraTransform = 0x30;

    constexpr uintptr_t pm_NoclipEnabled = 0x38; // public bool NoclipEnabled
    constexpr uintptr_t pm_NoclipSpeed   = 0x3C; // private float _noclipSpeed

    constexpr uintptr_t gc_FriendlyFire = 0x20;   // private bool _friendlyFire
    constexpr uintptr_t gc_RoundsToWin  = 0x94;   // <RoundsToWin>k__BackingField

    // SourceGameMovement (dump.cs)
    constexpr uintptr_t sgm_Velocity = 0x1C; // Vector3
    constexpr uintptr_t sgm_OnGround = 0x29; // bool

    // SourceMovementTuning (dump.cs)
    constexpr uintptr_t smt_AirAccelerate   = 0x20;
    constexpr uintptr_t smt_AirWishSpeedCap = 0x24;
    constexpr uintptr_t smt_MaxSpeed        = 0x28;
}

struct Orig {
    float pellet, scoped, horiz, vert, recharge, air[off::airFloats];
    int   edge;
};
static std::unordered_map<uintptr_t, Orig> g_orig;
static std::vector<uintptr_t> g_weapons;

static const uint8_t kGuns[] = {
    0,1,2,3,9,11,12,15,16,17,24,25,28,29,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49
};

static void refreshWeapons()
{
    g_weapons.clear();
    for (uint8_t id : kGuns) {
        uintptr_t wp = catalogGet(id);
        if (ok(wp)) g_weapons.push_back(wp);
    }
}

static void applyWeapon(uintptr_t wp, bool spread, bool recoil, bool rapidFire)
{
    uintptr_t air = Read<uintptr_t>(wp + off::airInaccuracy);
    uintptr_t rp  = Read<uintptr_t>(wp + off::recoilParams);

    if (!g_orig.count(wp)) {
        Orig o{};
        o.pellet   = Read<float>(wp + off::pelletSpread);
        o.scoped   = Read<float>(wp + off::scopedMoveInacc);
        o.edge     = Read<int>(wp + off::edgeSpreadShots);
        o.recharge = Read<float>(wp + off::rechargeTime);
        if (ok(rp))  { o.horiz = Read<float>(rp + off::horizRange); o.vert = Read<float>(rp + off::vertRange); }
        if (ok(air)) for (int i = 0; i < off::airFloats; i++) o.air[i] = Read<float>(air + off::airStart + i * 4);
        g_orig[wp] = o;
    }
    const Orig& o = g_orig[wp];

    Write<float>(wp + off::pelletSpread,    spread ? 0.f : o.pellet);
    Write<float>(wp + off::scopedMoveInacc, spread ? 0.f : o.scoped);
    Write<int>(wp + off::edgeSpreadShots,   spread ? 0   : o.edge);
    Write<float>(wp + off::rechargeTime,    rapidFire ? 0.f : o.recharge);
    if (ok(air))
        for (int i = 0; i < off::airFloats; i++)
            Write<float>(air + off::airStart + i * 4, spread ? 0.f : o.air[i]);
    if (ok(rp)) {
        Write<float>(rp + off::horizRange, recoil ? 0.f : o.horiz);
        Write<float>(rp + off::vertRange,  recoil ? 0.f : o.vert);
    }
}

static void applyCameraRecoil()
{
    uintptr_t cam = fpsCamera();
    if (!ok(cam)) return;
    for (uintptr_t o : off::camKick)
        Write<float>(cam + o, 0.f);
}

// Экспортируется для aim::RCS, чтобы не дублировать логику отдачи.
void rage::ApplyRecoilControl(bool enabled)
{
    refreshWeapons();
    for (uintptr_t wp : g_weapons) applyWeapon(wp, false, enabled, false);
    if (enabled) applyCameraRecoil();
}

static uintptr_t localPlayerController()
{
    static uintptr_t cached = 0;
    static double nextRefresh = 0.0;
    double now = ImGui::GetTime();
    if (!ok(cached) || now >= nextRefresh) {
        nextRefresh = now + 0.25;
        cached = 0;
        static void* klass = IL2CPP::Class("", "PlayerController", "Assembly-CSharp");
        for (uintptr_t pc : IL2CPP::FindObjectsOfType(klass)) {
            uintptr_t pl = Read<uintptr_t>(pc + 0x90);          // PlayerController._player
            if (ok(pl) && Read<uint8_t>(pl + 0x1C)) {            // Photon Player.IsLocal
                cached = pc;
                break;
            }
        }
    }
    return cached;
}

static uintptr_t gameController()
{
    static void* klass = IL2CPP::Class("ScarletWork.Game", "GameController", "Assembly-CSharp");
    for (uintptr_t go : IL2CPP::FindObjectsOfType(klass))
        if (ok(go)) return go; // активный контроллер режима (DefuseController и т.п.) — ровно один
    return 0;
}

static void applyInfinityAmmo()
{
    uintptr_t pc = localPlayerController();
    if (!ok(pc)) return;
    uintptr_t arms = Read<uintptr_t>(pc + off::pcArms);
    if (!ok(arms)) return;

    Write<int16_t>(arms + off::arms_MagazineCapacity, 999);
    Write<int16_t>(arms + off::arms_Capacity, 999);
}

// _rechargeTime (per-weapon, в applyWeapon) — основной gate скорострельности, вероятно. Это —
// подстраховка на уровне инстанса, не мешает, даже если роль этих полей не та, что мы думаем.
static void applyRapidFire()
{
    uintptr_t pc = localPlayerController();
    if (!ok(pc)) return;
    uintptr_t arms = Read<uintptr_t>(pc + off::pcArms);
    if (!ok(arms)) return;

    if (rage::rapid_fire) {
        Write<float>(arms + off::arms_TimeFired, 10.0f);
        Write<float>(arms + off::arms_NextDryFire, 0.001f);
    }
}

// Живой per-instance штраф движения (AccuracyMult/Additive) и живой накопленный кик отдачи
// (RecoilMult + CamRecoil*/RecoilBase*) — это то, что система ходьбы/прыжка двигает каждый кадр
// ПОВЕРХ статических полей WeaponParameters, которые уже зануляет applyWeapon(). Без этого
// no_spread/no_recoil держатся только стоя на месте.
static void applyLiveAccuracy()
{
    uintptr_t pc = localPlayerController();
    if (!ok(pc)) return;
    uintptr_t arms = Read<uintptr_t>(pc + off::pcArms);
    if (!ok(arms)) return;

    if (rage::no_spread) {
        Write<float>(arms + off::arms_AccuracyMult, 0.001f);
        Write<float>(arms + off::arms_AccuracyAdditive, 0.0f);
    }
    if (rage::no_recoil) {
        Write<float>(arms + off::arms_RecoilMult, 1.0f);
        Write<float>(arms + off::arms_CamRecoilV, 0.0f);
        Write<float>(arms + off::arms_CamRecoilH, 0.0f);
        Write<float>(arms + off::arms_RecoilBaseV, 0.0f);
        Write<float>(arms + off::arms_RecoilBaseH, 0.0f);
    }
}

static void applyMBT()
{
    uintptr_t pc = localPlayerController();
    if (!ok(pc)) return;
    uintptr_t mv = Read<uintptr_t>(pc + off::pcMovement);
    if (!ok(mv)) return;
    Write<uint8_t>(mv + off::pm_IsFreezing, 0);
}

static void applyNoFallDamage()
{
    uintptr_t pc = localPlayerController();
    if (!ok(pc)) return;
    uintptr_t mv = Read<uintptr_t>(pc + off::pcMovement);
    if (!ok(mv)) return;

    Write<float>(mv + 0xB8, 0.0f);
    Write<float>(mv + 0xB4, 0.0f);
}

struct V3 { float x, y, z; };

// Реальная формула воздушного ускорения Quake/Source (публично известная, не секрет игры),
// константы AirAccelerate/AirWishSpeedCap/MaxSpeed — настоящие значения этого билда (dump.cs).
static void applyAirStrafe()
{
    if (!rage::air_strafe) return;

    uintptr_t pc = localPlayerController();
    if (!ok(pc)) return;
    uintptr_t mv = Read<uintptr_t>(pc + off::pcMovement);
    if (!ok(mv)) return;

    uintptr_t core = Read<uintptr_t>(mv + off::pm_Core);
    if (!ok(core)) return;
    bool onGround = Read<uint8_t>(core + off::sgm_OnGround) != 0;
    if (onGround) return; // работаем только в воздухе

    uintptr_t camTr = Read<uintptr_t>(mv + off::pm_CameraTransform);
    if (!ok(camTr)) return;

    static void* kTr  = IL2CPP::Class("UnityEngine", "Transform", "UnityEngine.CoreModule");
    static void* mFwd = IL2CPP::Method(kTr, "get_forward", 0);
    if (!mFwd) return;

    V3 fwd = IL2CPP::InvokeBoxed<V3>(mFwd, (void*)camTr, nullptr);
    float fl = std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z);
    if (!(fl > 1e-4f)) return;
    float dirX = fwd.x / fl, dirZ = fwd.z / fl;

    V3 vel = Read<V3>(core + off::sgm_Velocity);
    float horizSpeed = std::sqrt(vel.x * vel.x + vel.z * vel.z);

    uintptr_t tuning  = Read<uintptr_t>(mv + off::pm_Tuning);
    float airAccel = ok(tuning) ? Read<float>(tuning + off::smt_AirAccelerate)   : 0.f;
    float wishCap  = ok(tuning) ? Read<float>(tuning + off::smt_AirWishSpeedCap) : 0.f;
    float maxSpeed = ok(tuning) ? Read<float>(tuning + off::smt_MaxSpeed)        : 0.f;
    float speedCap = (wishCap > 0.f) ? wishCap : maxSpeed;

    float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.001f, 0.1f);
    float wishSpeed = (speedCap > 0.f) ? speedCap : horizSpeed;
    float currentSpeed = vel.x * dirX + vel.z * dirZ; // dot(velocity, wishdir)
    float addSpeed = wishSpeed - currentSpeed;

    if (addSpeed > 0.f) {
        float accelSpeed = std::min(airAccel * wishSpeed * dt, addSpeed);
        vel.x += accelSpeed * dirX;
        vel.z += accelSpeed * dirZ;
    } else {
        vel.x = dirX * horizSpeed;
        vel.z = dirZ * horizSpeed;
    }

    Write<float>(core + off::sgm_Velocity,     vel.x);
    Write<float>(core + off::sgm_Velocity + 8, vel.z); // Y (+0x20) не трогаем
}

static void applyAutoWin()
{
    uintptr_t gc = gameController();
    if (!ok(gc)) return;

    int32_t state = Read<int32_t>(gc + 0xC0); // GameController._matchState
    static uintptr_t lastGc = 0;
    static int32_t lastState = -1;
    if (gc != lastGc) { lastGc = gc; lastState = state; } // новый матч/контроллер — без ложного триггера

    bool enteredActive = (state == 4 /*ActiveRoundState*/) && (lastState != 4);
    lastState = state;

    if (!rage::auto_win || !enteredActive) return;

    uintptr_t pc = localPlayerController();
    if (!ok(pc)) return;
    uintptr_t pl = Read<uintptr_t>(pc + 0x90); // PlayerController._player
    if (!ok(pl)) return;

    static void* kPlayer  = IL2CPP::Class("Photon.Realtime", "Player", "PhotonRealtime");
    static void* mGetTeam = IL2CPP::Method(kPlayer, "get_Team", 0);
    if (!mGetTeam) return;

    void* teamStr = IL2CPP::Invoke(mGetTeam, (void*)pl, nullptr);
    if (!ok((uintptr_t)teamStr)) return;

    static void* kGC         = IL2CPP::Class("ScarletWork.Game", "GameController", "Assembly-CSharp");
    static void* mAddScore   = IL2CPP::Method(kGC, "AddScore", 2);
    static void* mCheckScore = IL2CPP::Method(kGC, "RoundCheckScore", 0);
    if (!mAddScore) return;

    int32_t winType = 0; // WinType.MostPlayersKilled
    void* args[2] = { teamStr, &winType };
    IL2CPP::Invoke(mAddScore, (void*)gc, args);
    if (mCheckScore) IL2CPP::Invoke(mCheckScore, (void*)gc, nullptr);
}

// Suicide() — реальный метод игры (dump.cs, PlayerController), ноль аргументов. Вызываем на
// ЧУЖОМ инстансе — если внутри есть проверка владельца/IsLocal, вызов может отрисоваться только
// локально и откатиться сервером; verified ниже — грубая проверка по факту упавшего Health, не гарантия.
static int g_lastKillAttempts = 0;
static int g_lastKillVerified = 0;

static void applyAutoKill()
{
    uintptr_t gc = gameController();
    if (!ok(gc)) return;

    int32_t state = Read<int32_t>(gc + 0xC0); // GameController._matchState
    static uintptr_t lastGc = 0;
    static int32_t lastState = -1;
    if (gc != lastGc) { lastGc = gc; lastState = state; }

    bool enteredActive = (state == 4 /*ActiveRoundState*/) && (lastState != 4);
    lastState = state;

    if (!rage::auto_kill || !enteredActive) return;

    uintptr_t localPc = localPlayerController();

    static void* kPC      = IL2CPP::Class("", "PlayerController", "Assembly-CSharp");
    static void* mSuicide = IL2CPP::Method(kPC, "Suicide", 0);
    static void* mHealth  = IL2CPP::Method(kPC, "get_Health", 0);
    if (!mSuicide) return;

    g_lastKillAttempts = 0;
    g_lastKillVerified = 0;

    for (uintptr_t pc : IL2CPP::FindObjectsOfType(kPC)) {
        if (!ok(pc) || pc == localPc) continue;
        if (Read<uint8_t>(pc + off::pc_IsDeath) != 0) continue; // уже мёртв — пропускаем

        g_lastKillAttempts++;
        IL2CPP::Invoke(mSuicide, (void*)pc, nullptr);

        if (mHealth) {
            void* r = IL2CPP::Invoke(mHealth, (void*)pc, nullptr);
            int hp = ok((uintptr_t)r) ? *(int*)((uint8_t*)r + 0x10) : 1;
            if (hp <= 0 || Read<uint8_t>(pc + off::pc_IsDeath) != 0) g_lastKillVerified++;
        }
    }
}

static void drawStatus(bool ilReady)
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    char l1[200], l2[200], l3[200], l4[200];

    if (!ilReady) {
        std::snprintf(l1, sizeof l1, "RAGE: IL2CPP not ready");
        l2[0] = 0; l3[0] = 0; l4[0] = 0;
    } else {
        uintptr_t pc   = localPlayerController();
        uintptr_t arms = ok(pc) ? Read<uintptr_t>(pc + off::pcArms) : 0;
        uintptr_t mv   = ok(pc) ? Read<uintptr_t>(pc + off::pcMovement) : 0;
        uintptr_t gc   = gameController();

        std::snprintf(l1, sizeof l1,
            "RAGE tog: ammo=%d rate=%d mbt=%d rec=%d spr=%d win=%d kill=%d | pc=%d arms=%d mv=%d wpn=%d cam=%d gc=%d",
            rage::infinity_ammo, rage::rapid_fire, rage::MBT, rage::no_recoil, rage::no_spread,
            rage::auto_win, rage::auto_kill,
            ok(pc), ok(arms), ok(mv), (int)g_weapons.size(), ok(fpsCamera()), ok(gc));

        std::snprintf(l2, sizeof l2, "RAGE val: mag=%d res=%d timeFired=%.2f freezing=%d grounded=%d",
            ok(arms) ? (int)Read<int16_t>(arms + off::arms_MagazineCapacity) : -1,
            ok(arms) ? (int)Read<int16_t>(arms + off::arms_Capacity) : -1,
            ok(arms) ? Read<float>(arms + off::arms_TimeFired) : -1.f,
            ok(mv) ? (int)Read<uint8_t>(mv + off::pm_IsFreezing) : -1,
            ok(mv) ? (int)Read<uint8_t>(mv + off::pm_IsGrounded) : -1);

        std::snprintf(l3, sizeof l3, "RAGE acc: mult=%.3f add=%.2f recMult=%.2f camRV=%.2f camRH=%.2f",
            ok(arms) ? Read<float>(arms + off::arms_AccuracyMult) : -1.f,
            ok(arms) ? Read<float>(arms + off::arms_AccuracyAdditive) : -1.f,
            ok(arms) ? Read<float>(arms + off::arms_RecoilMult) : -1.f,
            ok(arms) ? Read<float>(arms + off::arms_CamRecoilV) : -1.f,
            ok(arms) ? Read<float>(arms + off::arms_CamRecoilH) : -1.f);

        std::snprintf(l4, sizeof l4, "RAGE kill: attempts=%d verified=%d | wpn0 recharge=%.3f",
            g_lastKillAttempts, g_lastKillVerified,
            !g_weapons.empty() ? Read<float>(g_weapons[0] + off::rechargeTime) : -1.f);
    }
    dl->AddText(ImVec2(11, 81), IM_COL32(0, 0, 0, 255), l1);
    dl->AddText(ImVec2(10, 80), IM_COL32(0, 255, 255, 255), l1);
    if (l2[0]) {
        dl->AddText(ImVec2(11, 97), IM_COL32(0, 0, 0, 255), l2);
        dl->AddText(ImVec2(10, 96), IM_COL32(0, 255, 255, 255), l2);
    }
    if (l3[0]) {
        dl->AddText(ImVec2(11, 113), IM_COL32(0, 0, 0, 255), l3);
        dl->AddText(ImVec2(10, 112), IM_COL32(0, 255, 255, 255), l3);
    }
    if (l4[0]) {
        dl->AddText(ImVec2(11, 129), IM_COL32(0, 0, 0, 255), l4);
        dl->AddText(ImVec2(10, 128), IM_COL32(0, 255, 255, 255), l4);
    }
}

void rage::run()
{
    bool ilReady = IL2CPP::Init();
    drawStatus(ilReady);
    if (!ilReady) return;

    static bool lastSpread = false, lastRecoil = false, lastRapid = false, pending = false;
    if (rage::no_spread != lastSpread || rage::no_recoil != lastRecoil || rage::rapid_fire != lastRapid) {
        lastSpread = rage::no_spread;
        lastRecoil = rage::no_recoil;
        lastRapid  = rage::rapid_fire;
        pending = true;
    }
    if (pending) {
        refreshWeapons();
        if (!g_weapons.empty()) {
            for (uintptr_t wp : g_weapons)
                applyWeapon(wp, rage::no_spread, rage::no_recoil, rage::rapid_fire);
            pending = false;
        }
    }

    if (rage::no_recoil) applyCameraRecoil();
    if (rage::no_spread || rage::no_recoil) applyLiveAccuracy();
    if (rage::infinity_ammo) applyInfinityAmmo();
    if (rage::rapid_fire) applyRapidFire();
    if (rage::MBT)           applyMBT();
    if (rage::no_fall_damage) applyNoFallDamage();
    applyAutoWin();
    applyAutoKill();
    applyAirStrafe();
}