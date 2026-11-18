<<<<<<< HEAD
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
=======
// Menu source watermark: @use_sberbank
#include "Menu/menu.h"

#include <algorithm>
#include <cmath>

#include "imgui_internal.h"
#include "Menu/blur.h"
#include "Menu/draw.h"
#include "Menu/fonts.h"
#include "Menu/style.h"
#include "Menu/widgets.h"

namespace nl {
namespace {

using namespace nl::style;
using draw::snap;

constexpr const char* k_tab_names[] = { "Aim", "Rage", "Visuals", "Misc", "Settings" };
constexpr const char* k_tab_icons[] = { icon::crosshairs, icon::mouse, icon::image, nullptr, icon::gear };
constexpr int k_tab_count = 5;

// Menu coordinates (unscaled, relative to the top-left corner `o`) to whole screen pixels.
ImVec2 at(const ImVec2& o, float x, float y) { return ImVec2(o.x + snap(px(x)), o.y + snap(px(y))); }

ImVec4 to_vec4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

float text_cap_y_centered(ImFont* font, float size, float center_y)
{
    return center_y - draw::cap_height(font, size) * 0.5f;
}

void draw_style_popup_rows(Menu& menu)
{
    ui::popup::group([&] {
        ui::popup::color("Accent", &menu.accent, true, icon::fill_drip, icon::gear);
        ui::popup::value("Menu Scale", &menu.scale_choice, scale_names, true, icon::scale);
        ui::popup::toggle("Auto Scale", &menu.auto_scale, icon::expand);
    });
    ui::popup::group([&] {
        if (ui::popup::item("Close menu", icon::xmark))
            menu.set_visible(false);
    });
}

} // namespace

void apply_imgui_style()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(0.0f, 0.0f);
    s.WindowBorderSize = 0.0f;
    s.WindowRounding = layout::rounding;
    s.PopupRounding = 6.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameRounding = 4.0f;
    s.FrameBorderSize = 0.0f;
    s.GrabRounding = 4.0f;
    s.ItemSpacing = ImVec2(0.0f, 0.0f);
    s.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    s.ScaleAllSizes(ui_scale);
    s.FontScaleDpi = ui_scale;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = to_vec4(col::label);
    c[ImGuiCol_TextDisabled] = to_vec4(col::section_header);
    c[ImGuiCol_WindowBg] = to_vec4(col::main_bg);
    c[ImGuiCol_PopupBg] = to_vec4(col::popup_bg);
    c[ImGuiCol_Border] = to_vec4(col::popup_border);
    c[ImGuiCol_FrameBg] = to_vec4(col::combo_bg);
    c[ImGuiCol_FrameBgHovered] = to_vec4(col::combo_bg_hovered);
    c[ImGuiCol_FrameBgActive] = to_vec4(col::combo_bg_hovered);
    c[ImGuiCol_Button] = to_vec4(col::combo_bg);
    c[ImGuiCol_ButtonHovered] = to_vec4(col::combo_bg_hovered);
    c[ImGuiCol_ButtonActive] = to_vec4(col::popup_item_hover);
    c[ImGuiCol_CheckMark] = to_vec4(col::accent);
    c[ImGuiCol_SliderGrab] = to_vec4(col::accent);
    c[ImGuiCol_SliderGrabActive] = to_vec4(col::toggle_on_knob);
    c[ImGuiCol_Header] = to_vec4(col::popup_item_hover);
    c[ImGuiCol_HeaderHovered] = to_vec4(col::popup_item_hover);
    c[ImGuiCol_HeaderActive] = to_vec4(col::tab_bg_active);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.70f, 0.84f, 1.0f, 0.30f);
    c[ImGuiCol_NavCursor] = to_vec4(col::accent);
}

void Menu::init(const ImVec2& pos, float scale)
{
    (void)pos; // the menu keeps itself centered on the display
    default_scale_ = ImClamp(scale, min_scale, max_scale);
    set_scale(default_scale_);
    last_scale_choice_ = scale_choice;
}

ImVec2 Menu::size() const
{
    return ImVec2(snap(layout::width * ui_scale), snap(layout::height * ui_scale));
}

void Menu::set_visible(bool visible)
{
    visible_ = visible;
    if (!visible)
        visibility_ = 0.0f;
}

void Menu::toggle_visible() { set_visible(!visible_); }

void Menu::set_tab(int tab, int subtab)
{
    if (tab == tab_ && subtab == subtab_)
        return;
    tab_ = ImClamp(tab, 0, k_tab_count - 1);
    subtab_ = subtab;
    tab_switch_ = 0.0f;
}

void Menu::set_scale(float scale)
{
    ui_scale = ImClamp(scale, min_scale, max_scale);
    apply_imgui_style();
}

float Menu::scale() const { return ui_scale; }

void Menu::update_scale()
{
    if (scale_choice != last_scale_choice_) {
        last_scale_choice_ = scale_choice;
        if (scale_choice <= 0) {
            auto_scale = true;
        } else {
            auto_scale = false;
            pending_scale_ = scale_values[ImClamp(scale_choice, 0, IM_ARRAYSIZE(scale_values) - 1)];
        }
    }
    if (pending_scale_ > 0.0f) {
        set_scale(pending_scale_);
        pending_scale_ = 0.0f;
    }
    if (!auto_scale)
        return;

    // The reference layout is 633x485; fit it into the phone screen with a margin.
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (display.x <= 1.0f || display.y <= 1.0f)
        return;
    float fit = ImMin((display.x - px(32.0f)) / layout::width, (display.y - px(32.0f)) / layout::height);
    fit = ImClamp(fit, min_scale, 1.6f);
    if (std::fabs(fit - ui_scale) > 0.004f)
        set_scale(fit);
}

void Menu::render()
{
    if (!visible_ && visibility_ <= 0.0f)
        return;

    const ImGuiIO& io = ImGui::GetIO();
    const float dt = ImMax(io.DeltaTime, 0.0f);
    col::accent = ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 1.0f));
    update_scale();

    visibility_ = ImClamp(visibility_ + (visible_ ? dt : -dt) * 7.0f, 0.0f, 1.0f);
    if (visibility_ <= 0.0f)
        return;
    if (!visible_)
        ImGui::ClosePopupsExceptModals();

    const ImVec2 sz = size();
    const ImVec2 display = io.DisplaySize;
    pos_.x = std::floor((display.x - sz.x) * 0.5f);
    pos_.y = std::floor((display.y - sz.y) * 0.5f);
    if (!visible_) {
        // Keep the fading window on the last valid position when the display shrank.
        pos_.x = ImMax(pos_.x, 0.0f);
        pos_.y = ImMax(pos_.y, 0.0f);
    }

    ImGui::SetNextWindowPos(pos_);
    ImGui::SetNextWindowSize(sz);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(layout::rounding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav;
    ImGui::Begin("##synapse", nullptr, flags);
    ImGui::PopStyleVar(4);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;
    const ImVec2 o(std::floor(window->Pos.x), std::floor(window->Pos.y));
    pos_ = o;
    const ImVec2 max(o.x + sz.x, o.y + sz.y);
    const int vtx_begin = dl->VtxBuffer.Size;

    const ImVec2 main_min = at(o, layout::main_x, 0.0f);
    const ImVec2 side_max(main_min.x + px(layout::main_rounding), max.y);
    const float side_r = px(layout::rounding);
    const float panel_a = blur::available() ? 0.52f : 0.68f;

    dl->PushClipRectFullScreen();
    draw::soft_shadow(dl, o, max, side_r, px(layout::shadow_size), col::shadow);
    dl->PopClipRect();
    blur::image(dl, o, side_max, side_r, ImDrawFlags_RoundCornersLeft);
    draw::rect_gradient_v(dl, o, side_max, draw::with_alpha(rgb(18, 24, 38), panel_a),
                          draw::with_alpha(rgb(12, 16, 26), panel_a + 0.06f), side_r, ImDrawFlags_RoundCornersLeft);
    draw::rect_gradient_v(dl, o, side_max, rgb(215, 228, 255, 28), rgb(160, 180, 220, 12), side_r,
                          ImDrawFlags_RoundCornersLeft);
    draw::border(dl, o, side_max, rgb(255, 255, 255, 28), side_r, draw::hairline());

    dl->AddRectFilled(main_min, max, col::main_bg, px(layout::main_rounding));
    draw::border(dl, main_min, max, col::main_border, px(layout::main_rounding), draw::hairline());

    draw_sidebar(dl, o);
    draw_header(dl, o);
    draw_content(o);
    draw_style_popup();

    if (visibility_ < 1.0f) {
        const float e = visibility_ * (2.0f - visibility_);
        for (int i = vtx_begin; i < dl->VtxBuffer.Size; ++i) {
            ImDrawVert& v = dl->VtxBuffer[i];
            v.col = draw::with_alpha(v.col, e);
            v.pos.y += (1.0f - e) * px(8.0f);
        }
    }
    ImGui::End();
}

void Menu::draw_sidebar(ImDrawList* dl, const ImVec2& o)
{
    const Fonts& f = fonts();

    // Logo and product name
    dl->AddRectFilled(at(o, layout::logo_x, layout::logo_y),
                      at(o, layout::logo_x + layout::logo_w, layout::logo_y + layout::logo_h), col::logo_bg,
                      px(layout::logo_rounding));
    const float logo_w = draw::text_width(f.black, font_size::logo, "S");
    draw::text_glow(dl, f.black, font_size::logo,
                    ImVec2(o.x + px(layout::logo_text_cx) - logo_w * 0.5f, o.y + px(layout::logo_text_cap_y)),
                    col::logo_text, col::logo_glow, px(layout::logo_glow_radius), "S");
    draw::text(dl, f.semibold, font_size::title, at(o, layout::title_x, layout::title_cap_y), col::title, "Synapse");
    draw::text(dl, f.medium, font_size::subtitle, at(o, layout::subtitle_x, layout::subtitle_cap_y), col::subtitle,
               "Standoff 2");

    const float sep_x0 = o.x + px(layout::sidebar_sep_x0), sep_x1 = o.x + px(layout::sidebar_sep_x1);
    draw::hline(dl, sep_x0, sep_x1, o.y + px(layout::sidebar_sep_top_y), col::sidebar_separator);
    draw::hline(dl, sep_x0, sep_x1, o.y + px(layout::sidebar_sep_bottom_y), col::sidebar_separator);

    draw::text(dl, f.medium, font_size::sidebar_header, at(o, layout::sidebar_header_x, layout::aimbot_header_cap_y),
               col::sidebar_header, "AIMBOT");
    draw::text(dl, f.medium, font_size::sidebar_header, at(o, layout::sidebar_header_x, layout::common_header_cap_y),
               col::sidebar_header, "COMMON");

    const float dt = ImGui::GetIO().DeltaTime;

    // One highlight slides between the tabs instead of each tab fading its own.
    {
        const float target_y = layout::tab_top_y[tab_];
        if (tab_marker_y_ < 0.0f)
            tab_marker_y_ = target_y;
        tab_marker_y_ = ImLerp(tab_marker_y_, target_y, 1.0f - std::exp(-16.0f * dt));
        if (std::fabs(tab_marker_y_ - target_y) < 0.01f)
            tab_marker_y_ = target_y;
        // Stretches a little while moving, like it is being pulled.
        const float stretch = ImMin(std::fabs(target_y - tab_marker_y_) * 0.25f, 6.0f);
        const float y0 = target_y < tab_marker_y_ ? tab_marker_y_ - stretch : tab_marker_y_;
        const float y1 = target_y > tab_marker_y_ ? tab_marker_y_ + layout::tab_h + stretch
                                                  : tab_marker_y_ + layout::tab_h;
        const ImVec2 mmin = at(o, layout::tab_x0, y0), mmax = at(o, layout::tab_x1, y1);
        dl->AddRectFilled(mmin, mmax, rgb(210, 225, 255, 36), px(layout::tab_rounding));
        draw::border(dl, mmin, mmax, rgb(255, 255, 255, 30), px(layout::tab_rounding), draw::hairline());
    }

    for (int t = 0; t < k_tab_count; ++t) {
        const bool selected = tab_ == t;
        const ImVec2 min = at(o, layout::tab_x0, layout::tab_top_y[t]);
        const ImVec2 max = at(o, layout::tab_x1, layout::tab_top_y[t] + layout::tab_h);
        ImGui::PushID(t);
        const ImGuiID id = ImGui::GetID("##tab");
        const ui::ItemState s = ui::item_behavior(id, min, max);
        if (s.pressed)
            set_tab(t);
        const float a = ui::animate(id + 1, selected ? 1.0f : 0.0f, 20.0f);
        const float h = ui::animate(id + 2, s.hovered ? 1.0f : 0.0f, 16.0f) * (1.0f - a);
        ImGui::PopID();

        if (h > 0.0f)
            dl->AddRectFilled(min, max, draw::with_alpha(col::tab_bg_hovered, h), px(layout::tab_rounding));

        const float cy = (min.y + max.y) * 0.5f;
        const ImU32 icon_col =
            draw::lerp_color(draw::lerp_color(col::tab_icon, col::tab_text_hovered, h), col::tab_icon_active, a);
        const ImU32 text_col =
            draw::lerp_color(draw::lerp_color(col::tab_text, col::tab_text_hovered, h), col::tab_text_active, a);
        if (k_tab_icons[t])
            draw::icon(dl, f.icons, font_size::icon, ImVec2(o.x + px(layout::tab_icon_cx), cy + px(layout::tab_icon_dy)),
                       icon_col, k_tab_icons[t]);
        else
            draw::icon_swap_square(dl, ImVec2(o.x + px(layout::misc_icon_cx), cy), icon_col,
                                   draw::lerp_color(col::sidebar_icon_hole, col::tab_icon_cutout, a));

        draw::text(dl, f.medium, font_size::tab,
                   ImVec2(snap(o.x + px(layout::tab_text_x + 2.0f * a + 1.0f * h)),
                          text_cap_y_centered(f.medium, font_size::tab, cy) + px(layout::tab_text_dy)),
                   text_col, k_tab_names[t]);
    }

    // Profile
    const ImVec2 profile_min = at(o, layout::tab_x0, layout::sidebar_sep_bottom_y + 6.0f);
    const ImVec2 profile_max = at(o, layout::tab_x1, layout::height - 4.0f);
    const ui::ItemState profile = ui::item_behavior(ImGui::GetID("##profile"), profile_min, profile_max);
    if (profile.pressed) {
        style_anchor_min_ = profile_min;
        style_anchor_max_ = profile_max;
        open_style_ = true;
    }
    const float ph = ui::animate(ImGui::GetID("##profile_h"), profile.hovered ? 1.0f : 0.0f, 16.0f);
    if (ph > 0.0f)
        dl->AddRectFilled(profile_min, profile_max, draw::with_alpha(col::tab_bg_hovered, ph), px(layout::tab_rounding));

    {
        const ImVec2 c(o.x + px(layout::avatar_cx), o.y + px(layout::avatar_cy));
        const float r = px(layout::avatar_r);
        dl->AddCircleFilled(c, r, col::logo_bg, 40);
        dl->AddCircle(c, r, draw::with_alpha(IM_COL32_WHITE, 26), 40, draw::hairline());
        const float w = draw::text_width(f.black, font_size::title, "S");
        draw::text(dl, f.black, font_size::title,
                   ImVec2(c.x - w * 0.5f, c.y - draw::cap_height(f.black, font_size::title) * 0.5f), col::logo_text, "S");
    }
    draw::text(dl, f.medium, font_size::tab, at(o, layout::username_x, layout::username_cap_y), col::username, "Synapse");
    draw::text(dl, f.medium, font_size::small, at(o, layout::days_x, layout::days_cap_y), col::subscription,
               "iOS build");
    draw::chevron_right(dl, ImVec2(o.x + px(layout::profile_chevron_cx), o.y + px(layout::profile_chevron_cy)),
                        px(1.8f), px(3.6f), draw::lerp_color(col::profile_chevron, col::tab_text_active, ph), px(1.6f));
}

void Menu::draw_header(ImDrawList* dl, const ImVec2& o)
{
    const Fonts& f = fonts();

    // Style group: the accent swatch and the scale dropdown.
    const char* label = "Style";
    const float text_w = draw::text_width(f.medium, font_size::text, label);
    const float chevron_x = o.x + px(layout::preset_text_x + layout::preset_chevron_gap) + text_w;
    const ImVec2 gmin = at(o, layout::group_x, layout::group_y);
    const ImVec2 gmax(snap(chevron_x + px(layout::preset_right_pad)), at(o, 0.0f, layout::group_y + layout::group_h).y);
    const float gcy = (gmin.y + gmax.y) * 0.5f;
    const float group_rounding = px(layout::group_rounding);
    const ImGuiID group_id = ImGui::GetID("##style_group");
    const ui::ItemState gs = ui::item_behavior(group_id, gmin, gmax);
    const bool open = ImGui::IsPopupOpen(ImGui::GetID("##style_popup"), ImGuiPopupFlags_None);
    if (gs.pressed && !open) {
        style_anchor_min_ = gmin;
        style_anchor_max_ = gmax;
        open_style_ = true;
    }
    const float gh = ui::animate(group_id + 1, (gs.hovered || open) ? 1.0f : 0.0f, 16.0f);
    dl->AddRectFilled(gmin, gmax, draw::lerp_color(col::group_bg, col::group_hovered, gh), group_rounding);

    // Accent swatch in the save button slot.
    {
        const float size = snap(px(12.0f));
        const ImVec2 swmin(snap(o.x + px(layout::save_icon_cx) - size * 0.5f), snap(gcy - size * 0.5f));
        const ImVec2 swmax(swmin.x + size, swmin.y + size);
        ImVec4 opaque(accent.x, accent.y, accent.z, 1.0f);
        dl->AddRectFilled(ImVec2(swmin.x - 1.0f, swmin.y - 1.0f), ImVec2(swmax.x + 1.0f, swmax.y + 1.0f),
                          col::toggle_outline, px(4.5f));
        dl->AddRectFilled(swmin, swmax, ImGui::ColorConvertFloat4ToU32(opaque), px(3.5f));
    }
    draw::text(dl, f.medium, font_size::text, at(o, layout::preset_text_x, layout::preset_cap_y),
               draw::lerp_color(col::preset_text, col::tab_text_active, gh * 0.6f), label);

    const ImU32 chevron_col = draw::lerp_color(col::preset_chevron, col::tab_text_active, gh * 0.6f);
    draw::chevron_down(dl, ImVec2(chevron_x, gcy + px(layout::preset_chevron_dy)), px(3.0f), px(1.5f), chevron_col,
                       px(1.4f));

    const float line = draw::hairline();
    draw::vline(dl, o.x + px(layout::group_sep_x) - line * 0.5f, gmin.y + line, gmax.y - line, col::group_border);
    draw::border(dl, gmin, gmax, draw::lerp_color(col::group_border, col::accent, gh * 0.35f), group_rounding, line);

    // Close button in the search slot.
    const ImVec2 sc(o.x + px(layout::search_cx), o.y + px(layout::search_cy));
    const float hit = px(10.0f);
    const ImGuiID close_id = ImGui::GetID("##close");
    const ui::ItemState cs =
        ui::item_behavior(close_id, ImVec2(sc.x - hit, sc.y - hit), ImVec2(sc.x + hit, sc.y + hit));
    if (cs.pressed)
        set_visible(false);
    const float ch = ui::animate(close_id + 1, cs.hovered ? 1.0f : 0.0f, 16.0f);
    if (ch > 0.0f)
        dl->AddCircleFilled(sc, px(10.0f) * (0.7f + 0.3f * ch), draw::with_alpha(col::group_hovered, ch), 24);
    draw::icon(dl, f.icons, font_size::icon * (1.0f + 0.08f * ch), sc,
               draw::lerp_color(col::search_icon, col::tab_text_active, ch), icon::xmark);

    draw::hline(dl, o.x + px(layout::header_sep_x0), o.x + px(layout::header_sep_x1), o.y + px(layout::header_sep_y),
                col::header_separator);
}

void Menu::draw_content(const ImVec2& o)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    tab_switch_ = ImMin(1.0f, tab_switch_ + ImGui::GetIO().DeltaTime * 6.0f);
    const int v0 = dl->VtxBuffer.Size;

    ImGui::PushID(tab_);
    if (host_content)
        host_content(*this, o, tab_, subtab_);
    ImGui::PopID();

    if (tab_switch_ < 1.0f) {
        const float e = tab_switch_ * (2.0f - tab_switch_);
        for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
            ImDrawVert& v = dl->VtxBuffer[i];
            v.col = draw::with_alpha(v.col, e);
            v.pos.y += (1.0f - e) * px(6.0f);
        }
    }
}

void Menu::draw_style_popup()
{
    const ImGuiID id = ImGui::GetID("##style_popup");
    if (open_style_) {
        ImGui::OpenPopup(id);
        open_style_ = false;
    }
    if (!ui::begin_popup_id(id, style_anchor_min_, style_anchor_max_, px(214.0f)))
        return;
    draw_style_popup_rows(*this);
    ui::end_popup();
}

} // namespace nl
>>>>>>> 8eb443c1a1cef2841572b2df7a49855023aeee26
