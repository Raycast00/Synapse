#import <Foundation/Foundation.h>
#include "rage.h"
#include "imgui.h"
#include "Memory/KittyInclude.hpp"
#include <dlfcn.h>
#include <unordered_map>
#include <chrono>

// ---------- memory helpers ----------
template <typename T>
inline T Read(uintptr_t addr)
{
    T out{};
    KittyMemory::memRead(addr, &out, sizeof(T));
    return out;
}

template <typename T>
inline bool Write(uintptr_t addr, T value)
{
    return KittyMemory::memWrite(addr, &value, sizeof(T));
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
struct SavedWeapon {
    float pellet, scoped, horiz, vert, air[off::airFloats];
    int   edge;
};
static std::unordered_map<uintptr_t, SavedWeapon> g_saved;

static void applySpread(uintptr_t wp, bool enable)
{
    uintptr_t air = Read<uintptr_t>(wp + off::airInaccuracy);
    uintptr_t rp  = Read<uintptr_t>(wp + off::recoilParams);

    if (enable) {
        if (!g_saved.count(wp)) {                       // оригиналы сохраняем один раз
            SavedWeapon s{};
            s.pellet = Read<float>(wp + off::pelletSpread);
            s.scoped = Read<float>(wp + off::scopedMoveInacc);
            s.edge   = Read<int>(wp + off::edgeSpreadShots);
            if (rp)  { s.horiz = Read<float>(rp + off::horizRange); s.vert = Read<float>(rp + off::vertRange); }
            if (air) for (int i = 0; i < off::airFloats; i++) s.air[i] = Read<float>(air + off::airStart + i * 4);
            g_saved[wp] = s;
        }
        Write<float>(wp + off::pelletSpread, 0.f);
        Write<float>(wp + off::scopedMoveInacc, 0.f);
        Write<int>(wp + off::edgeSpreadShots, 0);
        if (rp)  { Write<float>(rp + off::horizRange, 0.f); Write<float>(rp + off::vertRange, 0.f); }
        if (air) for (int i = 0; i < off::airFloats; i++) Write<float>(air + off::airStart + i * 4, 0.f);
    } else {
        auto it = g_saved.find(wp);
        if (it == g_saved.end()) return;
        const SavedWeapon& s = it->second;
        Write<float>(wp + off::pelletSpread, s.pellet);
        Write<float>(wp + off::scopedMoveInacc, s.scoped);
        Write<int>(wp + off::edgeSpreadShots, s.edge);
        if (rp)  { Write<float>(rp + off::horizRange, s.horiz); Write<float>(rp + off::vertRange, s.vert); }
        if (air) for (int i = 0; i < off::airFloats; i++) Write<float>(air + off::airStart + i * 4, s.air[i]);
        g_saved.erase(it);
    }
}

static void forEachWeapon(bool enable)
{
    for (int id = 0; id <= 52; id++) {
        uintptr_t wp = il2cpp::catalogGet((uint8_t)id);
        if (wp) applySpread(wp, enable);
    }
}

static void applyCameraRecoil()
{
    uintptr_t cam = il2cpp::fpsCamera();
    if (!cam) return;
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
        if (ak) {
            NSLog(@"[rage] range=%f pellet=%f edge=%d",
                  Read<float>(ak + 0x90), Read<float>(ak + off::pelletSpread),
                  Read<int>(ak + off::edgeSpreadShots));
        }
    }

    static bool spreadWasOn = false;
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();

    // конфиги трогаем раз в секунду
    if (now - last > std::chrono::seconds(1)) {
        last = now;
        if (rage::noSpread) {
            forEachWeapon(true);
            spreadWasOn = true;
        } else if (spreadWasOn) {
            forEachWeapon(false);               // вернуть оригиналы
            spreadWasOn = false;
        }
    }

    if (rage::noRecoil)
        applyCameraRecoil();
}