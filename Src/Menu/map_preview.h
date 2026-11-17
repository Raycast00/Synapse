// Menu source watermark: @use_sberbank
// Map thumbnails belong to the donor's World tab (CS2 map catalogue), which is
// not part of the iOS port. Rows never pass a row image, so this is a no-op.
#pragma once

#include "imgui.h"

namespace nl::maps {

inline bool draw(ImDrawList*, const char*, const ImVec2&, const ImVec2&) { return false; }

} // namespace nl::maps
