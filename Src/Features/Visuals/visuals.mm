#include "Memory/KittyInclude.hpp"
#include "imgui.h"
#include "Math/math.h"
#include "Features/Visuals/visuals.h"

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

void visuals::run()
{

}