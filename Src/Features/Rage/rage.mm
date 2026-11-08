#import <Foundation/Foundation.h>
#include "rage.h"
#include "il2cpp/il2cpp.h"
#include <unordered_map>
#include <vector>
#include <chrono>

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
    // WeaponParameters
    constexpr uintptr_t scopedMoveInacc = 0x58;
    constexpr uintptr_t edgeSpreadShots = 0x5C;
    constexpr uintptr_t airInaccuracy   = 0x68;
    constexpr uintptr_t pelletSpread    = 0xA4;
    constexpr uintptr_t recoilParams    = 0x150;
    // AirInaccuracy: 0x10..0x33 = stand/jump/land/ladder (Vector2) + jumpInitial
    constexpr uintptr_t airStart  = 0x10;
    constexpr int       airFloats = 9;
    // RecoilParameters
    constexpr uintptr_t horizRange = 0x10;
    constexpr uintptr_t vertRange  = 0x14;
    // PlayerFPSCamera
    constexpr uintptr_t camKick[] = { 0x3C, 0x4C, 0x50, 0x54, 0x58, 0x74 };
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

    Write<float>(wp + off::pelletSpread,   spread ? 0.f : o.pellet);
    Write<float>(wp + off::scopedMoveInacc, spread ? 0.f : o.scoped);
    Write<int>(wp + off::edgeSpreadShots,  spread ? 0   : o.edge);
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

    static bool logged = false;
    if (!logged) {
        logged = true;
        NSLog(@"[rage] cam=%p kickBack=%f punchPitch=%f punchShotPitch=%f",
              (void*)cam,
              ok(cam) ? Read<float>(cam + 0x3C) : -1.f,
              ok(cam) ? Read<float>(cam + 0x4C) : -1.f,
              ok(cam) ? Read<float>(cam + 0x58) : -1.f);
    }

    if (!ok(cam)) return;
    for (uintptr_t o : off::camKick)
        Write<float>(cam + o, 0.f);
}

void rage::run()
{
    if (!IL2CPP::Init()) return;

    static bool once = false;
    if (!once) {
        once = true;
        uintptr_t ak = catalogGet(9);   // AK47
        NSLog(@"[rage] ak=%p", (void*)ak);
        if (ok(ak))
            NSLog(@"[rage] range=%f pellet=%f edge=%d",
                  Read<float>(ak + 0x90), Read<float>(ak + off::pelletSpread),
                  Read<int>(ak + off::edgeSpreadShots));
    }

    static bool lastSpread = false, lastRecoil = false, pending = false;
    if (rage::noSpread != lastSpread || rage::noRecoil != lastRecoil) {
        lastSpread = rage::noSpread;
        lastRecoil = rage::noRecoil;
        pending = true;
    }
    if (pending) {
        refreshWeapons();
        if (!g_weapons.empty()) {
            for (uintptr_t wp : g_weapons)
                applyWeapon(wp, rage::noSpread, rage::noRecoil);
            NSLog(@"[rage] applied to %d weapons (spread=%d recoil=%d)",
                  (int)g_weapons.size(), (int)rage::noSpread, (int)rage::noRecoil);
            pending = false;
        }
    }

    if (rage::noRecoil)
        applyCameraRecoil();
}