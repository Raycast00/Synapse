#include "menu.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include "menu_fonts.hpp"
#include "menu_icons.hpp"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Small local helpers (kept in this translation unit on purpose).
// ---------------------------------------------------------------------------
namespace
{
    inline ImVec2 operator+(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }
    inline ImVec2 operator-(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x - b.x, a.y - b.y); }
    inline ImVec2 operator*(const ImVec2& a, float v) { return ImVec2(a.x * v, a.y * v); }

    // -----------------------------------------------------------------------
    // Palette (tokens taken from the original neverlose theme).
    // -----------------------------------------------------------------------
    const ImU32 col_shell        = IM_COL32(13, 15, 22, 240);
    const ImU32 col_shell_edge   = IM_COL32(38, 42, 55, 190);
    const ImU32 col_sidebar      = IM_COL32(18, 21, 30, 242);
    const ImU32 col_toolbar      = IM_COL32(11, 13, 20, 236);
    const ImU32 col_card         = IM_COL32(17, 19, 27, 226);
    const ImU32 col_card_edge    = IM_COL32(31, 34, 44, 255);
    const ImU32 col_control      = IM_COL32(25, 28, 38, 255);
    const ImU32 col_control_hi   = IM_COL32(31, 38, 54, 255);
    const ImU32 col_control_edge = IM_COL32(32, 35, 46, 255);
    const ImU32 col_divider      = IM_COL32(28, 31, 40, 255);
    const ImU32 col_text         = IM_COL32(214, 217, 226, 255);
    const ImU32 col_text_muted   = IM_COL32(146, 150, 162, 255);
    const ImU32 col_text_faint   = IM_COL32(94, 99, 111, 255);
    const ImU32 col_accent       = IM_COL32(75, 126, 255, 255);
    const ImU32 col_accent_hi    = IM_COL32(102, 164, 255, 255);
    const ImU32 col_selected     = IM_COL32(39, 43, 54, 255);
    const ImU32 col_thumb        = IM_COL32(248, 249, 252, 255);
    const ImU32 col_thumb_off    = IM_COL32(133, 144, 156, 255);

    // Type scale (points, the atlas is rasterized at the framebuffer density).
    const float kFontBody    = 15.0f;
    const float kFontBold    = 16.0f;
    const float kFontSmall   = 12.0f;
    const float kFontCaption = 11.0f;
    const float kFontIcon    = 14.0f;

    ImFont* g_font_body = nullptr;
    ImFont* g_font_bold = nullptr;
    ImFont* g_font_icon = nullptr;

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------
    struct CardRect { ImVec2 min, max; };

    struct PageCtx
    {
        ImDrawList*        dl = nullptr;
        ImDrawListSplitter splitter;
        float              x = 0.0f;        // card column origin
        float              w = 0.0f;        // card column width
        float              row_x = 0.0f;    // current row content left
        float              row_w = 0.0f;    // current row content width
        bool               first_row = true;
        CardRect           cards[16];
        int                card_count = 0;
        int                card_index = -1;
        bool               open = false;
    };

    struct UIState
    {
        bool   ready = false;
        menu::Page page = menu::Page::Visuals;
        float  page_mix = 1.0f;
        float  reveal = 0.0f;
        float  reveal_alpha = 1.0f;
        float  reveal_scale = 1.0f;
        ImVec2 reveal_pivot = ImVec2(0, 0);
        ImVec2 reveal_offset = ImVec2(0, 0);
        bool   window_begun = false;
        bool   window_open = false;
        int    shell_vtx = 0;
        float  inset_top = 0.0f;
        float  inset_bottom = 0.0f;
        ImVec4 shell = ImVec4(0, 0, 0, 0);
        ImVec4 button = ImVec4(0, 0, 0, 0);
        ImVec4 content = ImVec4(0, 0, 0, 0);
    };

    struct Dropdown
    {
        bool               pending = false;
        bool               active = false;
        int*               value = nullptr;
        const char* const* items = nullptr;
        int                count = 0;
        ImVec2             pos = ImVec2(0, 0);
        float              width = 0.0f;
        int                opened_frame = -1;
    };

    struct Picker
    {
        bool   pending = false;
        bool   active = false;
        float* color = nullptr;
        ImVec2 pos = ImVec2(0, 0);
    };

    UIState  g_state;
    PageCtx  g_page;
    Dropdown g_dd;
    Picker   g_picker;

    // -----------------------------------------------------------------------
    // Primitives
    // -----------------------------------------------------------------------
    inline float Saturate(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    inline ImU32 Mix(ImU32 a, ImU32 b, float t)
    {
        return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(a), ImGui::ColorConvertU32ToFloat4(b), Saturate(t)));
    }

    inline ImU32 Fade(ImU32 color, float alpha)
    {
        const ImU32 a = (color >> IM_COL32_A_SHIFT) & 255u;
        return (color & 0x00FFFFFFu) | ((ImU32)(a * Saturate(alpha)) << IM_COL32_A_SHIFT);
    }

    float Motion(ImGuiID id, float target, float speed = 16.0f, float initial = -1.0f)
    {
        float* value = ImGui::GetStateStorage()->GetFloatRef(id, initial < 0.0f ? target : initial);
        *value = ImLerp(*value, target, 1.0f - std::exp(-speed * ImGui::GetIO().DeltaTime));
        if (std::fabs(*value - target) < 0.0005f)
            *value = target;
        return *value;
    }

    void TextY(ImDrawList* d, ImFont* font, float size, float x, float y, float h, ImU32 col, const char* value)
    {
        if (!font || !value)
            return;
        const float th = font->CalcTextSizeA(size, FLT_MAX, 0.0f, value).y;
        d->AddText(font, size, ImVec2(x, y + std::floor((h - th) * 0.5f)), col, value);
    }

    void TextCentered(ImDrawList* d, ImFont* font, float size, float cx, float y, float h, ImU32 col, const char* value)
    {
        if (!font || !value)
            return;
        const float tw = font->CalcTextSizeA(size, FLT_MAX, 0.0f, value).x;
        TextY(d, font, size, cx - tw * 0.5f, y, h, col, value);
    }

    void TextYClip(ImDrawList* d, ImFont* font, float size, float x, float y, float h, float max_w, ImU32 col, const char* value)
    {
        if (!font || !value)
            return;
        if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, value).x <= max_w)
        {
            TextY(d, font, size, x, y, h, col, value);
            return;
        }

        char buf[192];
        buf[0] = '\0';
        const size_t len = std::strlen(value);
        for (size_t cut = len; cut > 0; --cut)
        {
            std::snprintf(buf, sizeof(buf), "%.*s...", (int)cut, value);
            if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, buf).x <= max_w)
                break;
        }
        TextY(d, font, size, x, y, h, col, buf);
    }

    void DrawToggle(ImDrawList* d, ImVec2 p, float on, float hover, bool enabled)
    {
        if (enabled && hover > 0.001f)
            d->AddRectFilled(p - ImVec2(1.0f + hover, 1.0f + hover), p + ImVec2(39.0f + hover, 23.0f + hover),
                             IM_COL32(75, 126, 255, (int)(48.0f * hover)), 12.0f);

        const ImU32 off_track = enabled ? IM_COL32(46, 51, 64, 255) : IM_COL32(33, 36, 45, 255);
        const ImU32 on_track = enabled ? col_accent : IM_COL32(52, 58, 72, 255);
        d->AddRectFilled(p, p + ImVec2(38.0f, 22.0f), Mix(off_track, on_track, on), 11.0f);

        const ImU32 off_thumb = enabled ? col_thumb_off : IM_COL32(96, 102, 114, 255);
        const ImU32 on_thumb = enabled ? col_thumb : IM_COL32(148, 153, 163, 255);
        d->AddCircleFilled(p + ImVec2(ImLerp(11.0f, 27.0f, on), 11.0f), 8.0f, Mix(off_thumb, on_thumb, on));
    }

    void OpenPicker(float* color, ImVec2 anchor)
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float w = 236.0f;
        const float h = 306.0f;
        ImVec2 pos(anchor.x - w + 56.0f, anchor.y - 8.0f);
        pos.x = ImClamp(pos.x, 8.0f, ImMax(8.0f, display.x - w - 8.0f));
        pos.y = ImClamp(pos.y, 8.0f, ImMax(8.0f, display.y - h - 8.0f));

        g_picker.pos = pos;
        g_picker.color = color;
        g_picker.active = true;
        g_picker.pending = true;
    }

    // Returns true when the chip was tapped.
    bool ColorChip(const char* id, ImVec2 pos, float height, const float color[4], bool enabled)
    {
        ImGui::PushID(id);
        ImGui::SetCursorScreenPos(ImVec2(pos.x - 8.0f, pos.y));
        ImGui::InvisibleButton("##chip", ImVec2(34.0f, height));
        const bool hovered = enabled && ImGui::IsItemHovered();
        const bool pressed = enabled && ImGui::IsItemClicked();
        ImGui::PopID();

        ImDrawList* d = g_page.dl;
        const ImVec2 c0(pos.x, pos.y + (height - 16.0f) * 0.5f);
        const ImVec2 c1(c0.x + 24.0f, c0.y + 16.0f);

        if (hovered)
            d->AddRect(c0 - ImVec2(3.0f, 3.0f), c1 + ImVec2(3.0f, 3.0f), IM_COL32(75, 126, 255, 100), 9.0f);

        d->AddRectFilled(c0, c1, IM_COL32(26, 29, 38, 255), 5.0f);
        d->AddRectFilled(c0, c1, Fade(ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], ImClamp(color[3], 0.0f, 1.0f))),
                                    enabled ? 1.0f : 0.35f), 5.0f);
        d->AddRect(c0, c1, IM_COL32(255, 255, 255, 40), 5.0f);
        return pressed;
    }

    // -----------------------------------------------------------------------
    // Row layout
    // -----------------------------------------------------------------------
    struct RowLayout
    {
        ImVec2 top;
        ImVec2 label;
        float  label_maxw;
        float  right;
        float  height;
    };

    RowLayout RowBegin(float height, float reserve)
    {
        PageCtx& pg = g_page;
        ImDrawList* d = pg.dl;
        const ImVec2 top = ImGui::GetCursorScreenPos();

        if (!pg.first_row)
            d->AddLine(ImVec2(pg.row_x, top.y), ImVec2(pg.row_x + pg.row_w, top.y), col_divider);
        pg.first_row = false;

        RowLayout r;
        r.top = top;
        r.label = ImVec2(pg.row_x, top.y);
        r.label_maxw = ImMax(24.0f, pg.row_w - reserve - 10.0f);
        r.right = pg.row_x + pg.row_w;
        r.height = height;
        return r;
    }

    void RowEnd(const RowLayout& r)
    {
        ImGui::SetCursorScreenPos(ImVec2(g_page.x, r.top.y + r.height));
    }

    void DeferredIcon(ImDrawList* d, ImVec2 p, ImU32 color)
    {
        d->AddLine(p, p + ImVec2(3.0f, 3.0f), color, 1.3f);
        d->AddLine(p + ImVec2(3.0f, 3.0f), p + ImVec2(8.0f, 0.0f), color, 1.3f);
    }

    void CheckMark(ImDrawList* d, ImVec2 p, ImU32 color)
    {
        d->AddLine(p, p + ImVec2(3.0f, 3.0f), color, 1.5f);
        d->AddLine(p + ImVec2(3.0f, 3.0f), p + ImVec2(9.0f, -4.0f), color, 1.5f);
    }

    // -----------------------------------------------------------------------
    // Sidebar / toolbar
    // -----------------------------------------------------------------------
    struct NavItem { menu::Page page; const char* icon; const char* label; };

    const NavItem kNav[] =
    {
        { menu::Page::Visuals,  ICON_NL_VISUALS,  "Visuals"  },
        { menu::Page::Aim,      ICON_NL_AIM,      "Aim"      },
        { menu::Page::Rage,     ICON_NL_RAGE,     "Rage"     },
        { menu::Page::Misc,     ICON_NL_MISC,     "Misc"     },
        { menu::Page::Settings, ICON_NL_SETTINGS, "Settings" },
    };

    const char* PageTitle(menu::Page page)
    {
        switch (page)
        {
        case menu::Page::Visuals:  return "Visuals";
        case menu::Page::Aim:      return "Aim";
        case menu::Page::Rage:     return "Rage";
        case menu::Page::Misc:     return "Miscellaneous";
        case menu::Page::Settings: return "Settings";
        }
        return "Synapse";
    }

    void DrawSidebar(ImDrawList* d, ImVec2 base, float w, float h)
    {
        const bool compact = (w < 78.0f);

        d->AddRectFilled(base, base + ImVec2(w, h), col_sidebar, 14.0f, ImDrawFlags_RoundCornersLeft);
        d->AddRectFilled(base + ImVec2(w - 14.0f, 0.0f), base + ImVec2(w, h), col_sidebar);
        d->AddLine(base + ImVec2(w, 0.0f), base + ImVec2(w, h), IM_COL32(30, 33, 43, 255));

        const float logo = compact ? 30.0f : 32.0f;
        const float logo_x = std::floor((w - logo) * 0.5f);
        d->AddRectFilled(base + ImVec2(logo_x, 14.0f), base + ImVec2(logo_x + logo, 14.0f + logo), IM_COL32(13, 30, 52, 255), 9.0f);
        TextCentered(d, g_font_bold, kFontBold, base.x + w * 0.5f, base.y + 14.0f, logo, col_accent_hi, "S");

        const float divider_y = compact ? 60.0f : 72.0f;
        if (!compact)
            TextCentered(d, g_font_body, kFontCaption, base.x + w * 0.5f, base.y + 50.0f, 16.0f, col_text_muted, "Synapse");

        d->AddLine(base + ImVec2(10.0f, divider_y), base + ImVec2(w - 10.0f, divider_y), IM_COL32(28, 31, 41, 255));
        if (!compact)
            TextY(d, g_font_body, kFontCaption, base.x + 16.0f, base.y + divider_y + 6.0f, 14.0f, col_text_faint, "PAGES");

        const float item_w = w - (compact ? 12.0f : 16.0f);
        float y = base.y + (compact ? divider_y + 8.0f : divider_y + 24.0f);

        for (int i = 0; i < IM_ARRAYSIZE(kNav); ++i)
        {
            const NavItem& nav = kNav[i];
            const bool selected = (g_state.page == nav.page);
            const ImVec2 pos(base.x + (compact ? 6.0f : 8.0f), y);
            const ImVec2 size(item_w, 40.0f);

            ImGui::PushID(1000 + i);
            ImGui::SetCursorScreenPos(pos);
            ImGui::InvisibleButton("##nav", size);
            const bool hovered = ImGui::IsItemHovered();
            const bool clicked = ImGui::IsItemClicked();
            const ImGuiID id = ImGui::GetItemID();
            ImGui::PopID();

            if (clicked && !selected)
            {
                g_state.page = nav.page;
                g_state.page_mix = 0.0f;
                g_dd.active = false;
                g_picker.active = false;
            }

            const float response = Motion(id ^ 0x9e3779b9u, selected ? 1.0f : (hovered ? 0.5f : 0.0f), 16.0f);
            if (response > 0.001f)
                d->AddRectFilled(pos, pos + size, Mix(IM_COL32(18, 21, 30, 0), col_selected, response), 9.0f);

            const ImU32 icon_col = Mix(col_text_muted, col_accent_hi, response);
            const ImU32 label_col = Mix(col_text_muted, col_text, response);
            if (compact)
            {
                TextY(d, g_font_icon, kFontIcon, pos.x + (size.x - 14.0f) * 0.5f, pos.y, size.y, icon_col, nav.icon);
            }
            else
            {
                TextY(d, g_font_icon, kFontIcon, pos.x + 11.0f, pos.y, size.y, icon_col, nav.icon);
                TextY(d, g_font_body, kFontBody - 1.0f, pos.x + 34.0f, pos.y, size.y, label_col, nav.label);
            }

            y += 44.0f;
        }

        if (!compact)
            TextY(d, g_font_body, kFontCaption, base.x + 16.0f, base.y + h - 26.0f, 14.0f, col_text_faint, "v0.0.4b");
    }

    void DrawToolbar(ImDrawList* d, ImVec2 base, float w, float h, float side_w, bool& open)
    {
        d->AddRectFilled(base + ImVec2(side_w, 0.0f), base + ImVec2(w, h), col_toolbar, 14.0f, ImDrawFlags_RoundCornersTopRight);
        d->AddLine(base + ImVec2(side_w, h), base + ImVec2(w, h), IM_COL32(27, 30, 39, 255));

        TextY(d, g_font_bold, kFontBody, base.x + side_w + 18.0f, base.y, h, col_text, PageTitle(g_state.page));
        TextY(d, g_font_body, kFontCaption, base.x + side_w + 18.0f, base.y + h - 15.0f, 12.0f, col_text_faint, "iOS");

        const ImVec2 size(40.0f, 40.0f);
        const ImVec2 pos(base.x + w - size.x - 6.0f, base.y + (h - size.y) * 0.5f);

        ImGui::PushID(2000);
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##close", size);
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();
        const ImGuiID id = ImGui::GetItemID();
        ImGui::PopID();

        const float response = Motion(id ^ 0x77aa11u, hovered ? 1.0f : 0.0f, 18.0f);
        if (response > 0.001f)
            d->AddRectFilled(pos, pos + size, IM_COL32(39, 43, 54, (int)(255.0f * response)), 10.0f);

        const ImVec2 c = pos + size * 0.5f;
        const ImU32 cross_col = Mix(col_text_muted, col_text, response);
        d->AddLine(c + ImVec2(-5.0f, -5.0f), c + ImVec2(5.0f, 5.0f), cross_col, 1.6f);
        d->AddLine(c + ImVec2(5.0f, -5.0f), c + ImVec2(-5.0f, 5.0f), cross_col, 1.6f);

        if (clicked)
            open = false;
    }

    void DrawLauncher(bool& open)
    {
        const ImVec2 size(88.0f, 36.0f);
        const ImVec2 pos(14.0f, std::floor(g_state.inset_top + 14.0f));

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##synapse_launcher", nullptr, flags))
        {
            ImDrawList* d = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetWindowPos();

            ImGui::SetCursorScreenPos(p);
            ImGui::InvisibleButton("##launch", size);
            const bool hovered = ImGui::IsItemHovered();
            const bool clicked = ImGui::IsItemClicked();
            const ImGuiID id = ImGui::GetItemID();

            if (clicked)
                open = true;

            const float response = Motion(id ^ 0x1a2b3cu, hovered ? 1.0f : 0.0f, 18.0f);
            d->AddRectFilled(p, p + size, Mix(IM_COL32(18, 20, 28, 236), IM_COL32(28, 33, 46, 246), response), 12.0f);
            d->AddRect(p, p + size, Mix(IM_COL32(44, 49, 62, 220), IM_COL32(75, 126, 255, 210), response), 12.0f);

            for (int i = 0; i < 3; ++i)
                d->AddRectFilled(ImVec2(p.x + 14.0f, p.y + 13.0f + i * 5.0f),
                                 ImVec2(p.x + 28.0f, p.y + 14.0f + i * 5.0f),
                                 Mix(col_text_muted, col_text, response), 1.0f);

            TextY(d, g_font_body, kFontSmall, p.x + 37.0f, p.y, size.y, Mix(col_text_muted, col_text, response), "Menu");

            g_state.button = ImVec4(pos.x, pos.y, size.x, size.y);
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    // -----------------------------------------------------------------------
    // Vertex animation helpers
    // -----------------------------------------------------------------------
    void TransformVtx(ImDrawList* d, int first, float alpha, ImVec2 offset, float scale, ImVec2 pivot)
    {
        if (!d)
            return;
        const float a = Saturate(alpha);
        for (int i = first; i < d->VtxBuffer.Size; ++i)
        {
            ImDrawVert& v = d->VtxBuffer[i];
            v.pos = ImVec2(pivot.x + (v.pos.x - pivot.x) * scale + offset.x,
                           pivot.y + (v.pos.y - pivot.y) * scale + offset.y);
            const ImU32 old = (v.col >> IM_COL32_A_SHIFT) & 255u;
            v.col = (v.col & 0x00FFFFFFu) | ((ImU32)(old * a) << IM_COL32_A_SHIFT);
        }
    }

    // -----------------------------------------------------------------------
    // Fonts
    // -----------------------------------------------------------------------
    int Base64Value(char c)
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    void Base64Decode(const char* src, std::vector<unsigned char>& out)
    {
        out.clear();
        const size_t len = std::strlen(src);
        out.reserve(len * 3 / 4 + 8);
        int acc = 0;
        int bits = 0;
        for (size_t i = 0; i < len; ++i)
        {
            const int v = Base64Value(src[i]);
            if (v < 0)
                continue;
            acc = (acc << 6) | v;
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out.push_back((unsigned char)((acc >> bits) & 0xFF));
            }
        }
    }

    void LoadFonts()
    {
        if (g_font_body)
            return;

        static std::vector<unsigned char> medium;
        static std::vector<unsigned char> bold;
        static std::vector<unsigned char> icons;
        Base64Decode(menu::detail::kFontMediumB64, medium);
        Base64Decode(menu::detail::kFontBoldB64, bold);
        Base64Decode(menu::detail::kFontIconB64, icons);

        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false; // the buffers above must outlive the atlas

        g_font_body = io.Fonts->AddFontFromMemoryTTF(medium.data(), (int)medium.size(), kFontBody, &cfg);
        g_font_bold = io.Fonts->AddFontFromMemoryTTF(bold.data(), (int)bold.size(), kFontBold, &cfg);

        static const ImWchar icon_ranges[] =
        {
            0xf03e, 0xf03e, // image
            0xf05b, 0xf05b, // crosshairs
            0xf0e7, 0xf0e7, // bolt
            0xf1de, 0xf1de, // sliders-h
            0xf013, 0xf013, // cog
            0
        };
        g_font_icon = io.Fonts->AddFontFromMemoryTTF(icons.data(), (int)icons.size(), kFontIcon, &cfg, icon_ranges);

        if (!g_font_body)
        {
            g_font_body = io.Fonts->AddFontDefault();
            if (!g_font_body && io.Fonts->Fonts.Size > 0)
                g_font_body = io.Fonts->Fonts[0];
        }
        if (!g_font_bold)
            g_font_bold = g_font_body;
        if (!g_font_icon)
            g_font_icon = g_font_body;
        if (g_font_body)
            io.FontDefault = g_font_body;
    }

    void InstallStyle()
    {
        ImGuiStyle& st = ImGui::GetStyle();
        st.FontSizeBase = kFontBody;

        st.WindowRounding = 14.0f;
        st.WindowBorderSize = 0.0f;
        st.WindowPadding = ImVec2(0.0f, 0.0f);
        st.ChildRounding = 0.0f;
        st.ChildBorderSize = 0.0f;
        st.PopupRounding = 16.0f;
        st.PopupBorderSize = 1.0f;
        st.FrameRounding = 8.0f;
        st.FrameBorderSize = 0.0f;
        st.FramePadding = ImVec2(8.0f, 6.0f);
        st.ItemSpacing = ImVec2(8.0f, 6.0f);
        st.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        st.ScrollbarSize = 5.0f;
        st.ScrollbarRounding = 3.0f;
        st.GrabMinSize = 14.0f;
        st.GrabRounding = 4.0f;
        st.WindowMenuButtonPosition = ImGuiDir_None;

        ImVec4* c = st.Colors;
        c[ImGuiCol_Text]                  = ImVec4(0.84f, 0.85f, 0.89f, 1.00f);
        c[ImGuiCol_TextDisabled]          = ImVec4(0.55f, 0.57f, 0.61f, 1.00f);
        c[ImGuiCol_WindowBg]              = ImVec4(0.05f, 0.06f, 0.09f, 0.94f);
        c[ImGuiCol_ChildBg]               = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_PopupBg]               = ImVec4(0.08f, 0.08f, 0.11f, 0.97f);
        c[ImGuiCol_Border]                = ImVec4(0.22f, 0.24f, 0.30f, 0.80f);
        c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_FrameBg]               = ImVec4(0.10f, 0.11f, 0.15f, 1.00f);
        c[ImGuiCol_FrameBgHovered]        = ImVec4(0.12f, 0.15f, 0.21f, 1.00f);
        c[ImGuiCol_FrameBgActive]         = ImVec4(0.16f, 0.20f, 0.30f, 1.00f);
        c[ImGuiCol_TitleBg]               = ImVec4(0.05f, 0.06f, 0.09f, 1.00f);
        c[ImGuiCol_TitleBgActive]         = ImVec4(0.05f, 0.06f, 0.09f, 1.00f);
        c[ImGuiCol_MenuBarBg]             = ImVec4(0.05f, 0.06f, 0.09f, 1.00f);
        c[ImGuiCol_ScrollbarBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.24f, 0.27f, 0.34f, 0.90f);
        c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.31f, 0.34f, 0.42f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.36f, 0.40f, 0.50f, 1.00f);
        c[ImGuiCol_CheckMark]             = ImVec4(0.29f, 0.49f, 1.00f, 1.00f);
        c[ImGuiCol_SliderGrab]            = ImVec4(0.29f, 0.49f, 1.00f, 1.00f);
        c[ImGuiCol_SliderGrabActive]      = ImVec4(0.40f, 0.60f, 1.00f, 1.00f);
        c[ImGuiCol_Button]                = ImVec4(0.13f, 0.15f, 0.20f, 1.00f);
        c[ImGuiCol_ButtonHovered]         = ImVec4(0.18f, 0.22f, 0.32f, 1.00f);
        c[ImGuiCol_ButtonActive]          = ImVec4(0.24f, 0.31f, 0.45f, 1.00f);
        c[ImGuiCol_Header]                = ImVec4(0.15f, 0.18f, 0.24f, 1.00f);
        c[ImGuiCol_HeaderHovered]         = ImVec4(0.19f, 0.23f, 0.32f, 1.00f);
        c[ImGuiCol_HeaderActive]          = ImVec4(0.24f, 0.31f, 0.45f, 1.00f);
        c[ImGuiCol_Separator]             = ImVec4(0.11f, 0.12f, 0.16f, 1.00f);
        c[ImGuiCol_SeparatorHovered]      = ImVec4(0.29f, 0.49f, 1.00f, 0.70f);
        c[ImGuiCol_SeparatorActive]       = ImVec4(0.29f, 0.49f, 1.00f, 1.00f);
        c[ImGuiCol_ResizeGrip]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_TextSelectedBg]        = ImVec4(0.29f, 0.49f, 1.00f, 0.35f);
        c[ImGuiCol_NavHighlight]          = ImVec4(0.29f, 0.49f, 1.00f, 0.80f);
        c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    }

    // -----------------------------------------------------------------------
    // Transient popups (dropdown + color picker), drawn in the shell window so
    // they float above the scrolling content layer.
    // -----------------------------------------------------------------------
    void DrawDropdown()
    {
        if (g_dd.pending)
        {
            ImGui::OpenPopup("##nl_dropdown");
            g_dd.opened_frame = ImGui::GetFrameCount();
            g_dd.pending = false;
        }
        if (!g_dd.active)
            return;

        ImGui::SetNextWindowPos(g_dd.pos, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 16.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));

        if (ImGui::BeginPopup("##nl_dropdown"))
        {
            const float pad = 6.0f;
            const float row_h = 38.0f;
            const float w = ImMax(140.0f, g_dd.width);
            const float h = pad * 2.0f + row_h * (float)g_dd.count;

            ImGui::Dummy(ImVec2(w, h));
            const ImVec2 p = ImGui::GetWindowPos();
            ImDrawList* d = ImGui::GetWindowDrawList();

            d->AddRectFilled(p - ImVec2(4.0f, 3.0f), p + ImVec2(w + 4.0f, h + 7.0f), IM_COL32(0, 0, 0, 70), 18.0f);
            d->AddRectFilled(p, p + ImVec2(w, h), IM_COL32(20, 20, 29, 253), 16.0f);
            d->AddRect(p, p + ImVec2(w, h), IM_COL32(57, 61, 76, 205), 16.0f);
            d->AddLine(p + ImVec2(16.0f, 1.0f), p + ImVec2(w - 16.0f, 1.0f), IM_COL32(255, 255, 255, 22));

            const bool accepts = ImGui::GetFrameCount() > g_dd.opened_frame;
            for (int i = 0; i < g_dd.count; ++i)
            {
                const ImVec2 row_pos = p + ImVec2(4.0f, pad + row_h * (float)i);

                ImGui::PushID(500 + i);
                ImGui::SetCursorScreenPos(row_pos);
                ImGui::InvisibleButton("##row", ImVec2(w - 8.0f, row_h));
                const bool hovered = ImGui::IsItemHovered();
                const bool clicked = ImGui::IsItemClicked();
                const ImGuiID id = ImGui::GetItemID();
                ImGui::PopID();

                const float response = Motion(id ^ 0x9921u, hovered ? 1.0f : 0.0f, 22.0f);
                if (response > 0.001f)
                    d->AddRectFilled(row_pos, row_pos + ImVec2(w - 8.0f, row_h), IM_COL32(75, 126, 255, (int)(30.0f * response)), 10.0f);

                const bool selected = (g_dd.value != nullptr && *g_dd.value == i);
                if (selected)
                    CheckMark(d, row_pos + ImVec2(15.0f, row_h * 0.5f - 1.0f), IM_COL32(230, 233, 241, 255));

                const char* item = (g_dd.items != nullptr) ? g_dd.items[i] : "";
                TextYClip(d, g_font_body, kFontBody - 1.0f, row_pos.x + 36.0f, row_pos.y, row_h, w - 52.0f,
                          selected ? IM_COL32(224, 229, 243, 255) : IM_COL32(182, 185, 196, 255), item);

                if (accepts && clicked && g_dd.value)
                {
                    *g_dd.value = i;
                    g_dd.active = false;
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }
        else
        {
            g_dd.active = false;
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    void DrawPicker()
    {
        if (g_picker.pending)
        {
            ImGui::OpenPopup("##nl_picker");
            g_picker.pending = false;
        }
        if (!g_picker.active || !g_picker.color)
            return;

        ImGui::SetNextWindowPos(g_picker.pos, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 16.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.075f, 0.078f, 0.11f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.24f, 0.26f, 0.32f, 0.85f));

        if (ImGui::BeginPopup("##nl_picker"))
        {
            ImGui::PushItemWidth(196.0f);
            ImGui::ColorPicker4("##color", g_picker.color,
                                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoSidePreview |
                                ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_PickerHueBar |
                                ImGuiColorEditFlags_NoInputs);
            ImGui::PopItemWidth();
            ImGui::EndPopup();
        }
        else
        {
            g_picker.active = false;
            g_picker.color = nullptr;
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    void CloseTransientPopups()
    {
        g_dd.active = false;
        g_dd.pending = false;
        g_picker.active = false;
        g_picker.pending = false;
        g_picker.color = nullptr;
        if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
            ImGui::ClosePopupToLevel(0, false);
    }

    void ClosePage()
    {
        PageCtx& pg = g_page;
        if (!pg.open)
            return;
        pg.open = false;

        // Card backgrounds are deferred to channel 0 so they land under the
        // already submitted rows.
        pg.splitter.SetCurrentChannel(pg.dl, 0);
        for (int i = 0; i < pg.card_count; ++i)
        {
            const CardRect& rect = pg.cards[i];
            if (rect.max.y - rect.min.y <= 0.5f)
                continue;
            pg.dl->AddRectFilled(rect.min, rect.max, col_card, 14.0f);
            pg.dl->AddRect(rect.min, rect.max, col_card_edge, 14.0f);
        }
        pg.splitter.Merge(pg.dl);

        const float page_ease = 1.0f - std::pow(1.0f - g_state.page_mix, 3.0f);
        const float alpha = page_ease * g_state.reveal_alpha;
        if (alpha < 0.999f || page_ease < 0.999f)
            TransformVtx(pg.dl, 0, alpha,
                         g_state.reveal_offset + ImVec2(9.0f * (1.0f - page_ease), 0.0f),
                         g_state.reveal_scale, g_state.reveal_pivot);

        g_state.page_mix = ImMin(1.0f, g_state.page_mix + ImGui::GetIO().DeltaTime / 0.22f);

        ImGui::PopID();
        ImGui::EndChild();
    }

    void EnsureReady()
    {
        if (!g_state.ready)
            menu::initialize();
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
namespace menu
{
    void initialize()
    {
        if (g_state.ready)
            return;
        g_state.ready = true;

        LoadFonts();
        InstallStyle();
    }

    void setSafeInsets(float top, float bottom)
    {
        g_state.inset_top = top > 0.0f ? top : 0.0f;
        g_state.inset_bottom = bottom > 0.0f ? bottom : 0.0f;
    }

    bool begin(bool& open)
    {
        EnsureReady();

        g_state.button = ImVec4(0, 0, 0, 0);

        if (!open)
        {
            if (g_dd.active || g_picker.active || g_state.window_open)
                CloseTransientPopups();
            g_state.reveal = 0.0f;
            g_state.page_mix = 1.0f;
            g_state.shell = ImVec4(0, 0, 0, 0);
            g_state.window_open = false;
            g_state.window_begun = false;
            g_page.open = false;
            DrawLauncher(open);
            return false;
        }

        if (g_state.reveal <= 0.0f)
            g_state.page_mix = 1.0f;

        const ImGuiIO& io = ImGui::GetIO();
        g_state.reveal = ImMin(1.0f, g_state.reveal + io.DeltaTime / 0.18f);
        const float ease = 1.0f - std::pow(1.0f - g_state.reveal, 3.0f);

        const float margin = 8.0f;
        const float avail_h = io.DisplaySize.y - g_state.inset_top - g_state.inset_bottom;
        float sw = ImMin(io.DisplaySize.x - margin * 2.0f, 540.0f);
        float sh = ImMin(avail_h - margin * 2.0f, 840.0f);
        sw = ImMax(sw, 240.0f);
        sh = ImMax(sh, 260.0f);

        const ImVec2 pos(std::floor((io.DisplaySize.x - sw) * 0.5f),
                         std::floor(g_state.inset_top + (avail_h - sh) * 0.5f));

        g_state.shell = ImVec4(pos.x, pos.y, sw, sh);
        g_state.reveal_alpha = ease;
        g_state.reveal_scale = ImLerp(0.965f, 1.0f, ease);
        g_state.reveal_pivot = pos + ImVec2(sw, sh) * 0.5f;
        g_state.reveal_offset = ImVec2(0.0f, (1.0f - ease) * 12.0f);

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(sw, sh), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (g_state.reveal < 0.999f)
            flags |= ImGuiWindowFlags_NoInputs;

        g_state.window_begun = true;
        g_state.window_open = ImGui::Begin("##synapse_menu", nullptr, flags);

        ImDrawList* d = ImGui::GetWindowDrawList();
        g_state.shell_vtx = d->VtxBuffer.Size;

        const float side_w = (sw < 340.0f) ? 64.0f : (sw < 420.0f ? 96.0f : 116.0f);
        const float toolbar_h = 50.0f;

        d->AddRectFilled(pos, pos + ImVec2(sw, sh), col_shell, 14.0f);
        d->AddRect(pos, pos + ImVec2(sw, sh), col_shell_edge, 14.0f);

        DrawSidebar(d, pos, side_w, sh);
        DrawToolbar(d, pos, sw, toolbar_h, side_w, open);

        g_state.content = ImVec4(pos.x + side_w + 12.0f,
                                 pos.y + toolbar_h + 10.0f,
                                 sw - side_w - 24.0f,
                                 sh - toolbar_h - 20.0f);
        return true;
    }

    bool page(Page id)
    {
        ClosePage();
        if (!g_state.window_open || g_state.page != id)
            return false;

        const ImVec4 area = g_state.content;
        ImGui::SetCursorScreenPos(ImVec2(area.x, area.y));

        ImGuiWindowFlags child_flags = ImGuiWindowFlags_None;
        if (g_state.reveal < 0.999f)
            child_flags |= ImGuiWindowFlags_NoInputs;
        ImGui::BeginChild("##content", ImVec2(area.z, area.w), ImGuiChildFlags_None, child_flags);
        ImGui::PushID((int)id);

        PageCtx& pg = g_page;
        pg.dl = ImGui::GetWindowDrawList();
        pg.x = area.x;
        pg.w = area.z - 8.0f;
        pg.row_x = area.x + 14.0f;
        pg.row_w = pg.w - 28.0f;
        pg.first_row = true;
        pg.card_count = 0;
        pg.card_index = -1;
        pg.open = true;
        pg.splitter.Split(pg.dl, 2);
        pg.splitter.SetCurrentChannel(pg.dl, 1);

        // Drag anywhere except on a control to scroll the page.
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) && !ImGui::IsAnyItemActive())
        {
            const float max_scroll = ImGui::GetScrollMaxY();
            if (max_scroll > 0.0f)
                ImGui::SetScrollY(ImClamp(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y, 0.0f, max_scroll));
        }
        return true;
    }

    void end()
    {
        ClosePage();
        if (!g_state.window_begun)
            return;

        if (g_state.window_open)
        {
            DrawDropdown();
            DrawPicker();
        }

        if (g_state.reveal < 0.999f)
            TransformVtx(ImGui::GetWindowDrawList(), g_state.shell_vtx, g_state.reveal_alpha,
                         g_state.reveal_offset, g_state.reveal_scale, g_state.reveal_pivot);

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        g_state.window_open = false;
        g_state.window_begun = false;
    }

    // -----------------------------------------------------------------------
    // Content
    // -----------------------------------------------------------------------
    void card(const char* title)
    {
        PageCtx& pg = g_page;
        if (!pg.open)
            return;

        ImGui::PushID(pg.card_count);
        ImDrawList* d = pg.dl;
        const ImVec2 cursor = ImGui::GetCursorScreenPos();

        if (title && title[0])
        {
            TextY(d, g_font_body, kFontCaption, pg.x + 6.0f, cursor.y, 20.0f, col_text_faint, title);
            ImGui::SetCursorScreenPos(ImVec2(pg.x, cursor.y + 24.0f));
        }
        else
        {
            ImGui::SetCursorScreenPos(ImVec2(pg.x, cursor.y + 10.0f));
        }

        if (pg.card_count < IM_ARRAYSIZE(pg.cards))
            pg.cards[pg.card_count].min = ImGui::GetCursorScreenPos();
        pg.card_index = pg.card_count;
        pg.card_count += 1;

        pg.first_row = true;
        pg.row_x = pg.x + 14.0f;
        pg.row_w = pg.w - 28.0f;

        ImGui::Dummy(ImVec2(pg.w, 8.0f));
    }

    void endCard()
    {
        PageCtx& pg = g_page;
        if (!pg.open || pg.card_index < 0)
            return;

        ImGui::Dummy(ImVec2(pg.w, 8.0f));
        const ImVec2 end = ImGui::GetCursorScreenPos();
        if (pg.card_index < IM_ARRAYSIZE(pg.cards))
        {
            CardRect& rect = pg.cards[pg.card_index];
            rect.max = ImVec2(rect.min.x + pg.w, end.y);
        }
        ImGui::Dummy(ImVec2(pg.w, 6.0f));

        ImGui::PopID();
        pg.card_index = -1;
    }

    bool toggle(const char* label, bool* value, bool enabled)
    {
        PageCtx& pg = g_page;
        if (!pg.open || !value || !label)
            return false;

        const float height = 44.0f;
        const RowLayout row = RowBegin(height, 58.0f);

        const ImVec2 hit(56.0f, height);
        const ImVec2 hit_pos(row.right - hit.x, row.top.y);
        ImGui::PushID(label);
        ImGui::SetCursorScreenPos(hit_pos);
        ImGui::InvisibleButton("##toggle", hit);
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = enabled && ImGui::IsItemClicked();
        const ImGuiID id = ImGui::GetItemID();
        ImGui::PopID();

        TextYClip(pg.dl, g_font_body, kFontBody, row.label.x, row.top.y, height, row.label_maxw,
                  enabled ? col_text : col_text_faint, label);

        if (clicked)
            *value = !*value;

        const float on = Motion(id ^ 0x55aa721u, *value ? 1.0f : 0.0f, 19.0f);
        const float response = Motion(id ^ 0x118a0u, (enabled && hovered) ? 1.0f : 0.0f, 20.0f);
        DrawToggle(pg.dl, ImVec2(row.right - 40.0f, row.top.y + (height - 22.0f) * 0.5f), on, response, enabled);

        RowEnd(row);
        return clicked;
    }

    bool toggleColor(const char* label, bool* value, float color[4], bool enabled)
    {
        PageCtx& pg = g_page;
        if (!pg.open || !value || !color || !label)
            return false;

        const float height = 44.0f;
        const RowLayout row = RowBegin(height, 94.0f);

        const ImVec2 hit(56.0f, height);
        const ImVec2 hit_pos(row.right - hit.x, row.top.y);
        ImGui::PushID(label);
        ImGui::SetCursorScreenPos(hit_pos);
        ImGui::InvisibleButton("##toggle", hit);
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = enabled && ImGui::IsItemClicked();
        const ImGuiID id = ImGui::GetItemID();
        ImGui::PopID();

        TextYClip(pg.dl, g_font_body, kFontBody, row.label.x, row.top.y, height, row.label_maxw,
                  enabled ? col_text : col_text_faint, label);

        if (clicked)
            *value = !*value;

        const float on = Motion(id ^ 0x55aa721u, *value ? 1.0f : 0.0f, 19.0f);
        const float response = Motion(id ^ 0x118a0u, (enabled && hovered) ? 1.0f : 0.0f, 20.0f);
        const ImVec2 toggle_pos(row.right - 40.0f, row.top.y + (height - 22.0f) * 0.5f);
        DrawToggle(pg.dl, toggle_pos, on, response, enabled);

        const ImVec2 chip_pos(row.right - 84.0f, row.top.y);
        if (ColorChip(label, chip_pos, height, color, enabled))
            OpenPicker(color, chip_pos);

        RowEnd(row);
        return clicked;
    }

    void color(const char* label, float color[4], bool enabled)
    {
        PageCtx& pg = g_page;
        if (!pg.open || !color || !label)
            return;

        const float height = 44.0f;
        const RowLayout row = RowBegin(height, 44.0f);

        TextYClip(pg.dl, g_font_body, kFontBody, row.label.x, row.top.y, height, row.label_maxw,
                  enabled ? col_text : col_text_faint, label);

        const ImVec2 chip_pos(row.right - 34.0f, row.top.y);
        if (ColorChip(label, chip_pos, height, color, enabled))
            OpenPicker(color, chip_pos);

        RowEnd(row);
    }

    bool slider(const char* label, float* value, float min_value, float max_value, const char* fmt, bool enabled)
    {
        PageCtx& pg = g_page;
        if (!pg.open || !value || !label)
            return false;
        if (!fmt)
            fmt = "%.2f";

        const float height = 64.0f;
        const float span = (max_value != min_value) ? (max_value - min_value) : 1.0f;

        char buf[64];
        std::snprintf(buf, sizeof(buf), fmt, *value);
        const float pill_w = ImMax(54.0f, g_font_body->CalcTextSizeA(kFontBody - 1.0f, FLT_MAX, 0.0f, buf).x + 18.0f);
        const float reserve = pill_w + 16.0f;

        const RowLayout row = RowBegin(height, reserve);

        const float track_x = row.label.x;
        const float track_w = ImMax(40.0f, row.right - row.label.x);
        const float track_y = row.top.y + 44.0f;

        ImGui::PushID(label);
        ImGui::SetCursorScreenPos(ImVec2(track_x - 6.0f, track_y - 16.0f));
        ImGui::InvisibleButton("##slider", ImVec2(track_w + 12.0f, 34.0f));
        const bool active = ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();
        const ImGuiID id = ImGui::GetItemID();
        ImGui::PopID();

        bool changed = false;
        if (enabled && active)
        {
            const float t = ImClamp((ImGui::GetIO().MousePos.x - track_x) / track_w, 0.0f, 1.0f);
            float next = min_value + t * span;
            const bool decimals = (std::strstr(fmt, ".1f") != nullptr) || (std::strstr(fmt, ".2f") != nullptr) || (std::strstr(fmt, ".3f") != nullptr);
            if (!decimals)
                next = std::floor(next + 0.5f);
            if (next != *value)
            {
                *value = next;
                changed = true;
            }
            std::snprintf(buf, sizeof(buf), fmt, *value);
        }

        const float t = ImClamp((*value - min_value) / span, 0.0f, 1.0f);
        const float shown = Motion(id ^ 0xa8f1u, t, 20.0f, t);

        TextYClip(pg.dl, g_font_body, kFontBody, row.label.x, row.top.y, 34.0f, track_w - reserve,
                  enabled ? col_text : col_text_faint, label);

        const ImVec2 pill(row.right - pill_w, row.top.y + 2.0f);
        pg.dl->AddRectFilled(pill, pill + ImVec2(pill_w, 26.0f), col_control, 8.0f);
        const float value_w = g_font_body->CalcTextSizeA(kFontBody - 1.0f, FLT_MAX, 0.0f, buf).x;
        TextY(pg.dl, g_font_body, kFontBody - 1.0f, pill.x + (pill_w - value_w) * 0.5f, pill.y, 26.0f,
              enabled ? IM_COL32(176, 179, 190, 255) : IM_COL32(120, 124, 134, 255), buf);

        pg.dl->AddRectFilled(ImVec2(track_x, track_y), ImVec2(track_x + track_w, track_y + 4.0f), IM_COL32(38, 43, 55, 255), 2.0f);
        pg.dl->AddRectFilled(ImVec2(track_x, track_y), ImVec2(track_x + track_w * shown, track_y + 4.0f),
                             enabled ? col_accent : IM_COL32(70, 76, 90, 255), 2.0f);

        const ImVec2 thumb(track_x + track_w * shown, track_y + 2.0f);
        if (enabled && (hovered || active))
            pg.dl->AddCircleFilled(thumb, 12.0f, IM_COL32(75, 126, 255, 44));
        pg.dl->AddCircleFilled(thumb, 8.0f, enabled ? col_thumb : IM_COL32(170, 175, 185, 255));

        RowEnd(row);
        return changed;
    }

    bool combo(const char* label, int* current, const char* const items[], int count, bool enabled)
    {
        PageCtx& pg = g_page;
        if (!pg.open || !current || !items || count <= 0 || !label)
            return false;

        const float height = 48.0f;
        const float control_w = ImClamp(pg.row_w * 0.46f, 92.0f, 152.0f);
        const RowLayout row = RowBegin(height, control_w + 16.0f);

        const ImVec2 control(row.right - control_w, row.top.y + (height - 30.0f) * 0.5f);

        ImGui::PushID(label);
        ImGui::SetCursorScreenPos(ImVec2(control.x - 6.0f, control.y - 6.0f));
        ImGui::InvisibleButton("##combo", ImVec2(control_w + 12.0f, 42.0f));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = enabled && ImGui::IsItemClicked();
        const ImGuiID id = ImGui::GetItemID();
        ImGui::PopID();

        const float response = Motion(id ^ 0x6161u, (enabled && hovered) ? 1.0f : 0.0f, 18.0f);

        TextYClip(pg.dl, g_font_body, kFontBody, row.label.x, row.top.y, height, row.label_maxw,
                  enabled ? col_text : col_text_faint, label);

        pg.dl->AddRectFilled(control, control + ImVec2(control_w, 30.0f), Mix(col_control, col_control_hi, response), 8.0f);
        pg.dl->AddRect(control, control + ImVec2(control_w, 30.0f), Mix(col_control_edge, IM_COL32(75, 126, 255, 190), response), 8.0f);

        const char* text = (*current >= 0 && *current < count) ? items[*current] : "Select";
        TextYClip(pg.dl, g_font_body, kFontBody - 1.0f, control.x + 10.0f, control.y, 30.0f, control_w - 30.0f,
                  enabled ? IM_COL32(178, 181, 192, 255) : IM_COL32(120, 124, 134, 255), text);
        DeferredIcon(pg.dl, ImVec2(control.x + control_w - 18.0f, control.y + 11.0f), IM_COL32(139, 143, 154, 255));

        if (clicked)
        {
            g_dd.value = current;
            g_dd.items = items;
            g_dd.count = count;
            g_dd.width = ImMax(control_w, 150.0f);

            const ImVec2 display = ImGui::GetIO().DisplaySize;
            const float popup_h = (float)count * 38.0f + 12.0f;
            ImVec2 pos(control.x, control.y + 36.0f);
            if (pos.y + popup_h > display.y - 8.0f)
                pos.y = ImMax(8.0f, control.y - popup_h - 6.0f);
            pos.x = ImClamp(pos.x, 8.0f, ImMax(8.0f, display.x - g_dd.width - 8.0f));
            g_dd.pos = pos;
            g_dd.active = true;
            g_dd.pending = true;
        }

        RowEnd(row);
        return clicked;
    }

    void text(const char* value)
    {
        PageCtx& pg = g_page;
        if (!pg.open || !value)
            return;

        const float height = 32.0f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        TextYClip(pg.dl, g_font_body, kFontSmall + 1.0f, pg.row_x, p.y, height, pg.row_w, col_text_muted, value);
        ImGui::SetCursorScreenPos(ImVec2(pg.x, p.y + height));
    }

    void caption(const char* value)
    {
        PageCtx& pg = g_page;
        if (!pg.open || !value)
            return;

        const float height = 26.0f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        TextYClip(pg.dl, g_font_body, kFontCaption, pg.row_x, p.y, height, pg.row_w, col_text_faint, value);
        ImGui::SetCursorScreenPos(ImVec2(pg.x, p.y + height));
    }

    bool popupOpen()
    {
        return g_dd.active || g_picker.active;
    }

    ImVec4 buttonRect()
    {
        return g_state.button;
    }

    ImVec4 shellRect()
    {
        return g_state.shell;
    }
}
