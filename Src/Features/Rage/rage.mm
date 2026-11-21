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

    constexpr uintptr_t pm_NoclipEnabled = 0x38; // public bool NoclipEnabled
    constexpr uintptr_t pm_NoclipSpeed   = 0x3C; // private float _noclipSpeed

    constexpr uintptr_t gc_FriendlyFire = 0x20;   // private bool _friendlyFire
    constexpr uintptr_t gc_RoundsToWin  = 0x94;   // <RoundsToWin>k__BackingField
}

struct Orig {
    float pellet, scoped, horiz, vert, air[off::airFloats];
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

static void applyWeapon(uintptr_t wp, bool spread, bool recoil)
{
    uintptr_t air = Read<uintptr_t>(wp + off::airInaccuracy);
    uintptr_t rp  = Read<uintptr_t>(wp + off::recoilParams);

    if (!g_orig.count(wp)) {
        Orig o{};
        o.pellet = Read<float>(wp + off::pelletSpread);
        o.scoped = Read<float>(wp + off::scopedMoveInacc);
        o.edge   = Read<int>(wp + off::edgeSpreadShots);
        if (ok(rp))  { o.horiz = Read<float>(rp + off::horizRange); o.vert = Read<float>(rp + off::vertRange); }
        if (ok(air)) for (int i = 0; i < off::airFloats; i++) o.air[i] = Read<float>(air + off::airStart + i * 4);
        g_orig[wp] = o;
    }
    const Orig& o = g_orig[wp];

    Write<float>(wp + off::pelletSpread,    spread ? 0.f : o.pellet);
    Write<float>(wp + off::scopedMoveInacc, spread ? 0.f : o.scoped);
    Write<int>(wp + off::edgeSpreadShots,   spread ? 0   : o.edge);
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
    for (uintptr_t wp : g_weapons) applyWeapon(wp, false, enabled);
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
        // 1.0 / 0.0 — нейтральные значения, выведены из того, что стоя на месте (без штрафа
        // движения) у тебя и так всё чисто; если после этого разброс станет ХУЖЕ — семантика
        // обратная, напиши, поменяем на большое значение вместо 1.0.
        Write<float>(arms + off::arms_AccuracyMult, 100.0f);
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

static void drawStatus(bool ilReady)
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    char l1[200], l2[200], l3[200];

    if (!ilReady) {
        std::snprintf(l1, sizeof l1, "RAGE: IL2CPP not ready");
        l2[0] = 0; l3[0] = 0;
    } else {
        uintptr_t pc   = localPlayerController();
        uintptr_t arms = ok(pc) ? Read<uintptr_t>(pc + off::pcArms) : 0;
        uintptr_t mv   = ok(pc) ? Read<uintptr_t>(pc + off::pcMovement) : 0;
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
}

void rage::run()
{
    bool ilReady = IL2CPP::Init();
    drawStatus(ilReady);
    if (!ilReady) return;

    static bool lastSpread = false, lastRecoil = false, pending = false;
    if (rage::no_spread != lastSpread || rage::no_recoil != lastRecoil) {
        lastSpread = rage::no_spread;
        lastRecoil = rage::no_recoil;
        pending = true;
    }
    if (pending) {
        refreshWeapons();
        if (!g_weapons.empty()) {
            for (uintptr_t wp : g_weapons)
                applyWeapon(wp, rage::no_spread, rage::no_recoil);
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
}