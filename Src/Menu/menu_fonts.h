#pragma once

struct ImFont;

namespace menu_fonts
{
    // Registers SST Medium, SST Bold and Font Awesome into the current ImGui atlas.
    // Font files are embedded as stb-compressed Base85 strings, so nothing is loaded
    // from the bundle or from the system at runtime.
    void add();

    // SST Bold used for titles and branding.
    ImFont* strong();
}
