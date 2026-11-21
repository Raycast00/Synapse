namespace rage
{
    void run();
    void ApplyRecoilControl(bool enabled);

    // Weapon
    inline bool infinity_ammo = false;
    inline bool rapid_fire = false;
    inline float rapid_fire_speed = 0.001f;
    inline bool wallbang = false;
    inline bool perfect_accuracy = false;
    inline bool no_recoil = false;
    inline bool no_spread = false;

    // Player
    inline bool MBT = false;
    inline bool no_fall_damage = false;
    inline bool air_strafe = false;

    // Game
    inline bool auto_win = false;
}