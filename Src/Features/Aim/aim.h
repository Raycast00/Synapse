namespace aim
{
    void run();

    inline bool aimbot = false;

    inline bool silent = false;
    namespace silent_additional
    {
        static bool double_tap = false;

        static bool silent_backtrack[4] = { false, false, false, false };
        static const char* silent_backtrack_effort[4] = { "Low", "Medium", "High", "Maximum" };

        static bool silent_spread[2] = { true, false };
        static const char* silent_spread_effort[2] = { "Off", "Full" };

        static float silent_extrapolation = 15.f;

        static bool silent_delay[2] = { false, false };
        static const char* silent_delay_effort[2] = { "Damage", "Hit Chance" };
    }

    inline bool anti_aim = false;

    inline bool aim_through_walls = false;
    inline bool automatic_fire = false;

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