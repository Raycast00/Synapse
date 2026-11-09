namespace rage
{
    void run();
    void ApplyRecoilControl(bool enabled);

    // Weapon
    inline bool infinity_ammo = false;
    inline bool fire_rate = false;
    inline bool wallbang = false;
    inline bool no_recoil = false;
    inline bool no_spread = false;

    // Player
    inline bool MBT = false;
    inline bool air_jump = false;

    inline bool fly = false;
    inline float fly_speed = 20.f;
}