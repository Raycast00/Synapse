// Menu source watermark: @use_sberbank
// Hotkey binds are not part of the iOS port: rows stay clickable, but right-click
// menus and the hotkey overlay are disabled. The API is kept so the donor panel
// and popup widgets compile unchanged.
#include "Menu/binds.h"

namespace nl::binds {

void set_context(Config*, const Config*, List*) {}
void set_external_state(void*, std::size_t) {}

Bind* add(const Target&) { return nullptr; }

void row(const Target&, const ImVec2&, const ImVec2&) {}

bool bound(const void*) { return false; }

void bound_icon(ImDrawList*, float, float, ImU32) {}

void update() {}
void release_all() {}

const char* key_name(ImGuiKey) { return ""; }
const char* mode_name(Mode) { return ""; }

void draw_hotkeys(bool, bool) {}

} // namespace nl::binds
