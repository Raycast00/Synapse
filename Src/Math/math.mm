#include "math.h"

bool WorldToScreen(const float* viewProj, ImVec2 screenSize, const float* worldPos, ImVec2& out)
{
    float w = worldPos[0]*viewProj[3] + worldPos[1]*viewProj[7] + worldPos[2]*viewProj[11] + viewProj[15];
    if (w < 0.1f || !std::isfinite(w)) return false;

    float x = (worldPos[0]*viewProj[0] + worldPos[1]*viewProj[4] + worldPos[2]*viewProj[8]  + viewProj[12]) / w;
    float y = (worldPos[0]*viewProj[1] + worldPos[1]*viewProj[5] + worldPos[2]*viewProj[9]  + viewProj[13]) / w;

    out.x = (x * 0.5f + 0.5f) * screenSize.x;
    out.y = (1.0f - (y * 0.5f + 0.5f)) * screenSize.y;
    return true;
}