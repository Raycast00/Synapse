namespace aim
{
    void run();

    inline bool aimbot = false;
    inline bool silent = false;
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