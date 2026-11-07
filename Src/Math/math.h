#include <cmath>
#include <vector>
#include "imgui.h"

struct Vector2
{
    float x, y;
};

struct Vector3
{
    float x, y, z;
};

bool WorldToScreen(const float* viewProj, ImVec2 screenSize, const float* worldPos, ImVec2& out)