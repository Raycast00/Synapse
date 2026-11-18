// Menu source watermark: @use_sberbank
#pragma once

#include "imgui.h"
#include "Menu/menu.h"

namespace nl {

// Draws the rows of the active tab into the menu's main area. `origin` is the menu's
// top-left corner on screen, `tab` the index of the sidebar entry. Implemented in
// content.cpp against the Synapse feature variables.
void host_content(Menu& menu, const ImVec2& origin, int tab, int subtab);

} // namespace nl
