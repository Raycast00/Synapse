// Menu source watermark: @use_sberbank
// Content of the menu tabs, wired to the Synapse feature variables. The donor menu
// draws its tabs from tabs.cpp; here every tab only lays out Panels of rows.
#include "Menu/content.h"

#include <cstddef>
#include <span>

#include "Menu/panel.h"
#include "Menu/style.h"

#include "Features/Aim/aim.h"
#include "Features/Rage/rage.h"
#include "Features/Visuals/visuals.h"
#include "Settings/settings.h"

namespace nl {
namespace {

// std::span over a string table for Panel::combo.
template <std::size_t N>
std::span<const char* const> span_of(const char* const (&items)[N])
{
    return std::span<const char* const>(items, N);
}

// The feature colors are plain float[4]; the widgets edit an ImVec4. The copy is
// written back when the row is done, which is also when its color popup edits it.
struct ColorRow {
    ImVec4 value;
    float* target;

    explicit ColorRow(float* color)
        : value(color[0], color[1], color[2], color[3]), target(color)
    {
    }

    ~ColorRow()
    {
        target[0] = value.x;
        target[1] = value.y;
        target[2] = value.z;
        target[3] = value.w;
    }

    ColorRow(const ColorRow&) = delete;
    ColorRow& operator=(const ColorRow&) = delete;
};

void draw_aim(const ImVec2& o)
{
    const float top = style::layout::section_cap_y;

    float next = top;
    {
        Panel p(o, Column::left, next, "AIMBOT", 7);
        p.toggle("Aimbot", &aim::aimbot);
        p.toggle("Silent", &aim::silent, aim::aimbot);
        p.toggle("Automatic Fire", &aim::automatic_fire);
        p.slider("Smoothing", &aim::aimbot_smoothing, 0.0f, 100.0f, "%.0f%%");
        p.combo("Hitboxes", &aim::current_aimbot_hitbox, span_of(aim::aimbot_hitboxes));
        p.toggle("RCS", &aim::RCS);
        p.toggle("Visualize Bullets", &aim::aim_visualize_bullet);
        next = p.next_header_cap_y();
    }

    {
        Panel p(o, Column::left, next, "FIELD OF VIEW", 2);
        {
            ColorRow color(aim::fov_color);
            p.color_toggle("Draw FOV", &color.value, &aim::draw_fov);
        }
        p.slider("Radius", &aim::fov_radius, 15.0f, 180.0f, "%.0f");
    }

    {
        Panel p(o, Column::right, top, "ANTI AIM", 3);
        p.toggle("Enabled", &aim::anti_aim);
        p.slider("Pitch", &aim::anti_pitch, -90.0f, 180.0f, "%.1f");
        p.slider("Yaw", &aim::anti_yaw, -180.0f, 180.0f, "%.1f");
    }
}

void draw_rage(const ImVec2& o)
{
    const float top = style::layout::section_cap_y;

    float next = top;
    {
        Panel p(o, Column::left, next, "PLAYER", 5);
        p.toggle("Move Before Timer", &rage::MBT);
        p.toggle("No Clip", &rage::flight);
        p.slider("Speed", &rage::flight_speed, 1.0f, 20.0f, "%.1f");
        p.toggle("No Fall Damage", &rage::no_fall_damage);
        p.toggle("Wallbang", &rage::wallbang);
        next = p.next_header_cap_y();
    }

    {
        Panel p(o, Column::left, next, "GAME", 2);
        p.toggle("Fast Defuse", &rage::fast_defuse);
        p.toggle("Auto Win", &rage::auto_win);
    }

    {
        Panel p(o, Column::right, top, "WEAPON", 5);
        p.toggle("Infinity Ammo", &rage::infinity_ammo);
        p.toggle("Rapid Fire", &rage::rapid_fire);
        p.toggle("Perfect Accuracy", &rage::perfect_accuracy);
        p.toggle("No Recoil", &rage::no_recoil);
        p.toggle("No Spread", &rage::no_spread);
    }
}

void draw_visuals(const ImVec2& o)
{
    const float top = style::layout::section_cap_y;

    float next = top;
    {
        Panel p(o, Column::left, next, "ENEMY", 7);
        p.toggle("Enabled", &visuals::esp_enabled);
        {
            ColorRow color(visuals::box_color);
            p.color_toggle("Box", &color.value, &visuals::box2D);
        }
        {
            ColorRow color(visuals::box_fill_color);
            p.color_toggle("Filled", &color.value, &visuals::boxFill);
        }
        p.combo("Box Type", &visuals::current_box_type, span_of(visuals::box_type));
        {
            ColorRow color(visuals::skeleton_color);
            p.color_toggle("Skeleton", &color.value, &visuals::skeleton);
        }
        {
            ColorRow color(visuals::healthbar_color);
            p.color_toggle("Health Bar", &color.value, &visuals::healthBar);
        }
        p.toggle("Health Text", &visuals::healthBarText, visuals::healthBar);
        next = p.next_header_cap_y();
    }

    {
        Panel p(o, Column::left, next, "ENEMY INFO", 3);
        {
            ColorRow color(visuals::names_color);
            p.color_toggle("Names", &color.value, &visuals::names);
        }
        {
            ColorRow color(visuals::distance_color);
            p.color_toggle("Distance", &color.value, &visuals::distance);
        }
        {
            ColorRow color(visuals::tracer_color);
            p.color_toggle("Tracer", &color.value, &visuals::tracer);
        }
    }

    {
        Panel p(o, Column::right, top, "WORLD", 4);
        {
            ColorRow color(visuals::weapon_text_color);
            p.color_toggle("Weapon Text", &color.value, &visuals::weaponText);
        }
        p.toggle("Weapon Icon", &visuals::weaponIcon);
        p.toggle("Bomb Timer", &visuals::bomb_timer_bar);
        {
            ColorRow color(visuals::grenade_trajectory_color);
            p.color_toggle("Trajectory", &color.value, &visuals::visualize_grenade_trajectory);
        }
    }
}

void draw_misc(const ImVec2& o)
{
    const float top = style::layout::section_cap_y;

    Panel p(o, Column::left, top, "MISC", 3);
    p.toggle("Watermark", &settings::watermark);
    p.toggle("Overlay", &settings::overlay);
    p.combo("Theme", &settings::current_theme, span_of(settings::theme_type));
}

void draw_settings(Menu& menu, const ImVec2& o)
{
    const float top = style::layout::section_cap_y;

    float next = top;
    {
        Panel p(o, Column::left, next, "INTERFACE", 3);
        p.color("Accent", &menu.accent);
        p.combo("Menu Scale", &menu.scale_choice, span_of(scale_names));
        p.toggle("Auto Scale", &menu.auto_scale);
        next = p.next_header_cap_y();
    }

    {
        Panel p(o, Column::left, next, "MENU", 1);
        if (p.open_row("Close menu"))
            menu.set_visible(false);
    }
}

} // namespace

void host_content(Menu& menu, const ImVec2& origin, int tab, int subtab)
{
    (void)subtab;
    switch (tab) {
    case 0: draw_aim(origin); break;
    case 1: draw_rage(origin); break;
    case 2: draw_visuals(origin); break;
    case 3: draw_misc(origin); break;
    case 4: draw_settings(menu, origin); break;
    default: break;
    }
}

} // namespace nl
