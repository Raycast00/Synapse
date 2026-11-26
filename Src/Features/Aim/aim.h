namespace aim
{
    void run();

    inline bool aimbot = false;

    inline bool silent = false;
    namespace silent_additional
    {
        static bool automatic_fire = false;
        static bool double_tap = false;

        static bool silent_backtrack[4] = { false, false, false, false };
        static const char* silent_backtrack_effort[4] = { "Low", "Medium", "High", "Maximum" };

        static bool silent_spread[2] = { true, false };
        static const char* silent_spread_effort[2] = { "Off", "Full" };

        static float silent_extrapolation = 10.f;

        static bool silent_delay[2] = { false, false };
        static const char* silent_delay_effort[2] = { "Damage", "Hit Chance" };

        static bool refine_shot = false;
    }

    inline bool silent_force_shot = false;
    namespace silent_force_additional
    {
        static float minimum_hit_chance = 10.f;
        static float extrapolation_safety = 75.f;
    }

    inline bool anti_aim = false;

    inline bool aim_through_walls = false;

    inline bool wallbang_thin_walls = false; // сайленту разрешено стрелять через тонкие стены (наша оценка толщины)

    inline bool draw_fov = false;
    inline float fov_radius = 15.f;
    inline float fov_color[4]{ 1.f, 1.f, 1.f, 0.5f };

    inline float aimbot_smoothing = 15.f;

    inline float anti_pitch = 90.f;
    inline float anti_yaw = 180.f;

    inline int current_aimbot_hitbox = 0;
    inline const char* aimbot_hitboxes[] = { "Head", "Chest", "Random" };

    // Other
    inline bool aim_visualize_bullet = false;

    // Weapon
    inline bool RCS = false;
}