namespace aim
{
    void run();

    inline bool aimbot = false;
    inline bool silent = false;

    inline float aimbot_smoothness = 5.f;

    inline int current_aimbot_hitbox = 0;
    inline const char* aimbot_hitboxes[] = { "Head", "Chest", "Random" };

    inline bool drawFOV = false;
    inline float fov_radius = 180.f;
    inline float fov_color[4]{ 1.f, 1.f, 1.f, 0.25f };

    inline bool fovCheck = false;

    inline bool RCS = false;
}