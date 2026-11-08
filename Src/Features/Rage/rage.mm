#import <Foundation/Foundation.h>
#include "rage.h"
#include "imgui.h"
#include "Memory/KittyInclude.hpp"
#include <dlfcn.h>
#include <unordered_map>
#include <chrono>

// ---------- memory helpers ----------
static inline bool ok(uintptr_t p) { return p > 0x100000000ULL && p < 0x1000000000000ULL; }

template <typename T>
inline T Read(uintptr_t addr)
{
    if (!ok(addr)) return T{};
    return *(T*)addr;
}

template <typename T>
inline bool Write(uintptr_t addr, T value)
{
    if (!ok(addr)) return false;
    *(T*)addr = value;
    return true;
}

// ---------- il2cpp api ----------
namespace il2cpp {
    using domain_get_t    = void* (*)();
    using thread_attach_t = void* (*)(void*);
    using asm_open_t      = void* (*)(void*, const char*);
    using asm_image_t     = void* (*)(void*);
    using class_name_t    = void* (*)(void*, const char*, const char*);
    using method_name_t   = void* (*)(void*, const char*, int);
    using invoke_t        = void* (*)(void*, void*, void**, void**);
    using field_name_t    = void* (*)(void*, const char*);
    using static_get_t    = void  (*)(void*, void*);

    static domain_get_t    domain_get;
    static thread_attach_t thread_attach;
    static asm_open_t      assembly_open;
    static asm_image_t     assembly_image;
    static class_name_t    class_from_name;
    static method_name_t   get_method;
    static invoke_t        runtime_invoke;
    static field_name_t    get_field;
    static static_get_t    static_get;

    static void* image = nullptr;
    static bool  ready = false;

    // зовётся каждый кадр, но реально работает только после ~5 сек и раз в секунду
    static bool init()
    {
        if (ready) return true;

        static int frame = 0;
        frame++;
        if (frame < 300 || frame % 60 != 0) return false;

        // берём только УЖЕ загруженный UnityFramework, сами ничего не грузим
        NSString *path = [NSString stringWithFormat:@"%@/Frameworks/UnityFramework.framework/UnityFramework",
                          NSBundle.mainBundle.bundlePath];
        void *h = dlopen(path.UTF8String, RTLD_NOLOAD | RTLD_NOW);
        if (!h) { NSLog(@"[rage] UnityFramework not loaded yet: %@", path); return false; }

        domain_get      = (domain_get_t)dlsym(h, "il2cpp_domain_get");
        thread_attach   = (thread_attach_t)dlsym(h, "il2cpp_thread_attach");
        assembly_open   = (asm_open_t)dlsym(h, "il2cpp_domain_assembly_open");
        assembly_image  = (asm_image_t)dlsym(h, "il2cpp_assembly_get_image");
        class_from_name = (class_name_t)dlsym(h, "il2cpp_class_from_name");
        get_method      = (method_name_t)dlsym(h, "il2cpp_class_get_method_from_name");
        runtime_invoke  = (invoke_t)dlsym(h, "il2cpp_runtime_invoke");
        get_field       = (field_name_t)dlsym(h, "il2cpp_class_get_field_from_name");
        static_get      = (static_get_t)dlsym(h, "il2cpp_field_static_get_value");

        if (!domain_get || !thread_attach || !assembly_open || !assembly_image ||
            !class_from_name || !get_method || !runtime_invoke || !get_field || !static_get) {
            NSLog(@"[rage] il2cpp exports missing");
            return false;
        }

        void *dom = domain_get();
        if (!dom) { NSLog(@"[rage] domain null, runtime not up"); return false; }
        thread_attach(dom);

        void *as = assembly_open(dom, "Assembly-CSharp");
        if (!as) { NSLog(@"[rage] Assembly-CSharp not found"); return false; }

        image = assembly_image(as);
        ready = image != nullptr;
        NSLog(@"[rage] il2cpp ready=%d", (int)ready);
        return ready;
    }

    // static WeaponParameters WeaponCatalog.Get(WeaponId id)
    static uintptr_t catalogGet(uint8_t id)
    {
        static void* klass  = class_from_name(image, "", "WeaponCatalog");
        static void* method = klass ? get_method(klass, "Get", 1) : nullptr;
        if (!method) return 0;

        void* args[1] = { &id };
        void* exc = nullptr;
        void* ret = runtime_invoke(method, nullptr, args, &exc);
        return exc ? 0 : (uintptr_t)ret;
    }

    // PlayerFPSCamera.Local (static property backing field)
    static uintptr_t fpsCamera()
    {
        static void* klass = class_from_name(image, "", "PlayerFPSCamera");
        static void* field = klass ? get_field(klass, "<Local>k__BackingField") : nullptr;
        if (!field) return 0;

        uintptr_t out = 0;
        static_get(field, &out);
        return out;
    }
}

// ---------- offsets (dump.cs v1.4, pc-билд) ----------
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

// ---------- spread / recoil ----------
struct Orig {
    float pellet, scoped, horiz, vert, air[off::airFloats];
    int   edge;
};
static std::unordered_map<uintptr_t, Orig> g_orig;
static std::vector<uintptr_t> g_weapons;

// только огнестрел, без ножей и гранат: для несуществующих id Get может кидать исключение
static const uint8_t kGuns[] = {
    0,1,2,3,9,11,12,15,16,17,24,25,28,29,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49
};

static void refreshWeapons()
{
    g_weapons.clear();
    for (uint8_t id : kGuns) {
        uintptr_t wp = il2cpp::catalogGet(id);
        if (ok(wp)) g_weapons.push_back(wp);
    }
}

static void applyWeapon(uintptr_t wp, bool spread, bool recoil)
{
    uintptr_t air = Read<uintptr_t>(wp + off::airInaccuracy);
    uintptr_t rp  = Read<uintptr_t>(wp + off::recoilParams);

    if (!g_orig.count(wp)) {                       // оригиналы сохраняем один раз
        Orig o{};
        o.pellet = Read<float>(wp + off::pelletSpread);
        o.scoped = Read<float>(wp + off::scopedMoveInacc);
        o.edge   = Read<int>(wp + off::edgeSpreadShots);
        if (ok(rp))  { o.horiz = Read<float>(rp + off::horizRange); o.vert = Read<float>(rp + off::vertRange); }
        if (ok(air)) for (int i = 0; i < off::airFloats; i++) o.air[i] = Read<float>(air + off::airStart + i * 4);
        g_orig[wp] = o;
    }
    const Orig& o = g_orig[wp];

    // галка включена -> 0, выключена -> оригинал
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
    uintptr_t cam = il2cpp::fpsCamera();

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

// ---------- entry ----------
void rage::run()
{
    if (!il2cpp::init()) return;

    // один раз проверяем, что оффсеты похожи на правду
    static bool once = false;
    if (!once) {
        once = true;
        uintptr_t ak = il2cpp::catalogGet(9);   // AK47
        NSLog(@"[rage] ak=%p", (void*)ak);
        if (ok(ak))
            NSLog(@"[rage] range=%f pellet=%f edge=%d",
                  Read<float>(ak + 0x90), Read<float>(ak + off::pelletSpread),
                  Read<int>(ak + off::edgeSpreadShots));
    }

    // конфиги трогаем только при смене галок
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