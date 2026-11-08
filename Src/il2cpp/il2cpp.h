#pragma once
#include <cstdint>
#include <vector>

namespace IL2CPP {

    bool Init();
    bool Ready();

    void* Domain();
    void* Image(const char* assemblyName = "Assembly-CSharp");

    void* Class(const char* ns, const char* name, const char* assemblyName = "Assembly-CSharp");
    void* Method(void* klass, const char* name, int argc);
    void* Field(void* klass, const char* name);
    void* Type(void* klass);

    void StaticFieldRaw(void* field, void* out);
    template <typename T>
    T StaticField(void* field) {
        T out{};
        StaticFieldRaw(field, &out);
        return out;
    }

    void* Invoke(void* method, void* instance, void** args, void** exceptionOut = nullptr);
    void* InvokeStatic(void* method, void** args);

    template <typename T>
    T InvokeBoxed(void* method, void* instance, void** args, void** exceptionOut = nullptr) {
        void* ret = Invoke(method, instance, args, exceptionOut);
        if (!ret) return T{};
        return *(T*)((uint8_t*)ret + 0x10);
    }

    std::vector<uintptr_t> FindObjectsOfType(void* klass);

    bool ValidPtr(uintptr_t addr);

    template <typename T>
    T Read(uintptr_t addr) {
        if (!ValidPtr(addr)) return T{};
        return *(T*)addr;
    }

    template <typename T>
    bool Write(uintptr_t addr, T value) {
        if (!ValidPtr(addr)) return false;
        *(T*)addr = value;
        return true;
    }
} // namespace IL2CPP