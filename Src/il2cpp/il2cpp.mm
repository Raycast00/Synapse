#import <Foundation/Foundation.h>
#include "il2cpp.h"
#include <dlfcn.h>
#include <unordered_map>
#include <string>

namespace IL2CPP {

using domain_get_t    = void* (*)();
using thread_attach_t = void* (*)(void*);
using asm_open_t      = void* (*)(void*, const char*);
using asm_image_t     = void* (*)(void*);
using class_name_t    = void* (*)(void*, const char*, const char*);
using method_name_t   = void* (*)(void*, const char*, int);
using invoke_t        = void* (*)(void*, void*, void**, void**);
using field_name_t    = void* (*)(void*, const char*);
using static_get_t    = void  (*)(void*, void*);
using class_type_t    = void* (*)(void*);          // il2cpp_class_get_type
using type_obj_t      = void* (*)(void*);           // il2cpp_type_get_object
using obj_get_class_t = void* (*)(void*);

static domain_get_t    s_domain_get;
static thread_attach_t s_thread_attach;
static asm_open_t      s_assembly_open;
static asm_image_t     s_assembly_image;
static class_name_t    s_class_from_name;
static obj_get_class_t s_object_get_class;
static method_name_t   s_get_method;
static invoke_t        s_runtime_invoke;
static field_name_t    s_get_field;
static static_get_t    s_static_get;
static class_type_t    s_class_get_type;
static type_obj_t      s_type_get_object;

static void* s_domain = nullptr;
static bool  s_ready  = false;

static std::unordered_map<std::string, void*> s_imageCache;
static std::unordered_map<std::string, void*> s_classCache;
static std::unordered_map<std::string, void*> s_methodCache;
static std::unordered_map<std::string, void*> s_fieldCache;
static std::unordered_map<void*, void*>       s_typeCache;

bool  Ready()  { return s_ready; }
void* Domain() { return s_domain; }

bool ValidPtr(uintptr_t addr)
{
    return addr > 0x100000000ULL && addr < 0x1000000000000ULL;
}

bool Init()
{
    if (s_ready) return true;

    static int frame = 0;
    frame++;
    if (frame < 300 || frame % 60 != 0) return false;

    NSString *path = [NSString stringWithFormat:@"%@/Frameworks/UnityFramework.framework/UnityFramework",
                      NSBundle.mainBundle.bundlePath];
    void *h = dlopen(path.UTF8String, RTLD_NOLOAD | RTLD_NOW);
    if (!h) { NSLog(@"[IL2CPP] UnityFramework not loaded yet"); return false; }

    s_domain_get      = (domain_get_t)dlsym(h, "il2cpp_domain_get");
    s_thread_attach   = (thread_attach_t)dlsym(h, "il2cpp_thread_attach");
    s_assembly_open   = (asm_open_t)dlsym(h, "il2cpp_domain_assembly_open");
    s_assembly_image  = (asm_image_t)dlsym(h, "il2cpp_assembly_get_image");
    s_class_from_name = (class_name_t)dlsym(h, "il2cpp_class_from_name");
    s_object_get_class = (obj_get_class_t)dlsym(h, "il2cpp_object_get_class");
    s_get_method      = (method_name_t)dlsym(h, "il2cpp_class_get_method_from_name");
    s_runtime_invoke  = (invoke_t)dlsym(h, "il2cpp_runtime_invoke");
    s_get_field       = (field_name_t)dlsym(h, "il2cpp_class_get_field_from_name");
    s_static_get      = (static_get_t)dlsym(h, "il2cpp_field_static_get_value");
    s_class_get_type  = (class_type_t)dlsym(h, "il2cpp_class_get_type");
    s_type_get_object = (type_obj_t)dlsym(h, "il2cpp_type_get_object");

    if (!s_domain_get || !s_thread_attach || !s_assembly_open || !s_assembly_image ||
        !s_class_from_name || !s_get_method || !s_runtime_invoke || !s_get_field ||
        !s_static_get || !s_class_get_type || !s_type_get_object) {
        NSLog(@"[IL2CPP] exports missing");
        return false;
    }

    s_domain = s_domain_get();
    if (!s_domain) { NSLog(@"[IL2CPP] domain null"); return false; }
    s_thread_attach(s_domain);

    s_ready = true;
    NSLog(@"[IL2CPP] ready");
    return true;
}

void* Image(const char* assemblyName)
{
    if (!s_ready) return nullptr;
    auto it = s_imageCache.find(assemblyName);
    if (it != s_imageCache.end()) return it->second;

    s_thread_attach(s_domain);
    void* as  = s_assembly_open(s_domain, assemblyName);
    void* img = as ? s_assembly_image(as) : nullptr;
    s_imageCache[assemblyName] = img;
    if (!img) NSLog(@"[IL2CPP] assembly not found: %s", assemblyName);
    return img;
}

void* Class(const char* ns, const char* name, const char* assemblyName)
{
    if (!s_ready) return nullptr;
    std::string key = std::string(assemblyName) + "|" + ns + "|" + name;
    auto it = s_classCache.find(key);
    if (it != s_classCache.end()) return it->second;

    void* img = Image(assemblyName);
    void* k = img ? s_class_from_name(img, ns, name) : nullptr;
    s_classCache[key] = k;
    if (!k) NSLog(@"[IL2CPP] class not found: %s.%s (%s)", ns, name, assemblyName);
    return k;
}

void* Method(void* klass, const char* name, int argc)
{
    if (!klass) return nullptr;
    std::string key = std::to_string((uintptr_t)klass) + "|" + name + "|" + std::to_string(argc);
    auto it = s_methodCache.find(key);
    if (it != s_methodCache.end()) return it->second;

    void* m = s_get_method(klass, name, argc);
    s_methodCache[key] = m;
    if (!m) NSLog(@"[IL2CPP] method not found: %s/%d", name, argc);
    return m;
}

void* Field(void* klass, const char* name)
{
    if (!klass) return nullptr;
    std::string key = std::to_string((uintptr_t)klass) + "|" + name;
    auto it = s_fieldCache.find(key);
    if (it != s_fieldCache.end()) return it->second;

    void* f = s_get_field(klass, name);
    s_fieldCache[key] = f;
    if (!f) NSLog(@"[IL2CPP] field not found: %s", name);
    return f;
}

void* Type(void* klass)
{
    if (!klass) return nullptr;
    auto it = s_typeCache.find(klass);
    if (it != s_typeCache.end()) return it->second;

    void* t = s_class_get_type(klass);
    void* obj = t ? s_type_get_object(t) : nullptr;
    s_typeCache[klass] = obj;
    return obj;
}

void* ClassOf(void* obj)
{
    if (!s_ready || !ValidPtr((uintptr_t)obj)) return nullptr;
    return s_object_get_class(obj);
}

void StaticFieldRaw(void* field, void* out)
{
    if (field) s_static_get(field, out);
}

void* Invoke(void* method, void* instance, void** args, void** exceptionOut)
{
    if (!method) return nullptr;
    void* exc = nullptr;
    void* ret = s_runtime_invoke(method, instance, args, &exc);
    if (exceptionOut) *exceptionOut = exc;
    return exc ? nullptr : ret;
}

void* InvokeStatic(void* method, void** args)
{
    return Invoke(method, nullptr, args);
}

std::vector<uintptr_t> FindObjectsOfType(void* klass)
{
    std::vector<uintptr_t> out;
    if (!s_ready || !klass) return out;

    static void* objKlass = Class("UnityEngine", "Object", "UnityEngine.CoreModule");
    static void* findMethod = objKlass ? Method(objKlass, "FindObjectsOfType", 1) : nullptr;
    if (!findMethod) return out;

    void* typeArg = Type(klass);
    if (!typeArg) return out;

    void* args[1] = { typeArg };
    uintptr_t arr = (uintptr_t)Invoke(findMethod, nullptr, args);   // массив — ссылочный тип, НЕ упакован
    if (!ValidPtr(arr)) return out;

    // layout Il2CppArray: [klass 8][monitor 8][bounds 8][length 8][data...]
    uint64_t length = Read<uint64_t>(arr + 0x18);
    if (length > 4096) return out;   // защита от мусора, если layout не совпал

    for (uint64_t i = 0; i < length; i++) {
        uintptr_t elem = Read<uintptr_t>(arr + 0x20 + i * 8);
        if (ValidPtr(elem)) out.push_back(elem);
    }
    return out;
}

} // namespace IL2CPP