namespace visuals
{
    void run();

    inline bool esp_enabled = false;

    inline bool box2D = false;
    inline float box_color[4]{ 1.f, 1.f, 1.f, 1.f };

    inline int current_box_type = 0;
    inline const char* box_type[] = { "2D", "Corner" };

    inline bool boxFill = false;
    inline float box_fill_color[4]{ 1.f, 1.f, 1.f, 0.25f };

    inline bool skeleton = false;
    inline float skeleton_color[4]{ 1.f, 1.f, 1.f, 0.5f };

    inline bool healthBar = false;
    inline float healthbar_color[4]{ 0.f, 1.0f, 0.f, 1.f };

    inline bool healthBarText = false;

    inline bool names = false;
    inline float names_color[4]{ 1.f, 1.f, 1.f, 0.75f };

    inline bool weaponText = false;
    inline float weapon_text_color[4]{ 1.f, 1.f, 1.f, 1.f };

    inline bool weaponIcon = false;

    inline bool distance = false;
    inline float distance_color[4]{ 1.f, 1.f, 1.f, 1.f };

    inline bool tracer = false;
    inline float tracer_color[4]{ 1.f, 1.f, 1.f, 1.f };

    // Bomb
    inline bool bomb_timer_bar = false;
    inline float bomb_timer_color[4]{ 1.f, 1.f, 1.f, 1.f };

    // Grenade
    inline bool visualize_grenade_trajectory = false;
    inline float grenade_trajectory_color[4]{ 1.f, 1.f, 1.f, 0.75f };
}