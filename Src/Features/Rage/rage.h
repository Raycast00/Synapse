namespace rage
{
    void run();
    void ApplyRecoilControl(bool enabled);

    // Weapon
    inline bool infinity_ammo = false;
    inline bool rapid_fire = false;
    inline float rapid_fire_speed = 0.02f;
    inline bool wallbang = false;
    inline bool no_recoil = false;
    inline bool no_spread = false;

    // Player
    inline bool MBT = false;
    inline bool air_jump = false;

    inline bool super_jump = false;
    inline float jump_strength = 200.f;

    inline bool no_fall_damage = false;
    inline bool godmode = false;
}