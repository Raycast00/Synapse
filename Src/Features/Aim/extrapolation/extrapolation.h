#pragma once
#include <cstdint>
#include <cmath>
#include <vector>

// Структура — по присланному примеру (hackvshack.net), физика — наша: MovementConVars читается
// живьём через IL2CPP StaticField (dump.cs, static class, namespace ""), а не зашиты числа другой
// игры. Gravity применяется к Y (Unity — Y-up), не к Z, как в примере (там Z-up/Source-конвенция).
namespace extrapolation
{
    struct Vec3
    {
        float x = 0.f, y = 0.f, z = 0.f;
        Vec3() = default;
        Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
        Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
        Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
        Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
        float length() const { return std::sqrt(x * x + y * y + z * z); }
        Vec3 normalized() const { float l = length(); return l > 1e-6f ? Vec3(x / l, y / l, z / l) : Vec3(); }
    };

    struct PlayerState
    {
        Vec3 position;
        Vec3 velocity;
        bool on_ground = true; // on-ground цели не подтверждён (см. aim.mm) — по умолчанию "на земле"
        int32_t tick = 0;
    };

    // Живые значения этого билда, передаются снаружи при каждом вызове — класс сам ничего не
    // резолвит и не кэширует.
    struct MovementTuning
    {
        float friction = 0.f;      // MovementConVars.Friction (static, live)
        float stopSpeed = 0.f;     // MovementConVars.StopSpeed (static, live)
        float gravity = 0.f;       // MovementConVars.Gravity (static, live) — только если on_ground=false
        float airFriction = 0.25f; // SourceGameMovement.AirSurfaceFriction — C#-const (не поле, в
                                   // рантайме не читается, компилятор зашивает его в код вызывающей
                                   // стороны) — буквальное значение из dump.cs
    };

    struct ExtrapolationConfig
    {
        bool enabled = true;
        int32_t max_ticks = 16; // 0.25с (MaxExtrapolationDuration) / 0.015625с (MoveTick) — dump.cs
    };

    struct ExtrapolationResult
    {
        bool valid = false;
        int32_t ticks_extrapolated = 0;
        PlayerState final_state;
        std::vector<PlayerState> all_ticks;
    };

    constexpr float MOVE_TICK = 0.015625f; // PlayerMovement.MoveTick, dump.cs — этот билд
    constexpr float STOP_EPSILON = 0.1f;   // числовой эпсилон остановки трения, не игровая константа

    class Extrapolator
    {
    public:
        void set_config(const ExtrapolationConfig& cfg) { config_ = cfg; }
        const ExtrapolationConfig& get_config() const { return config_; }

        ExtrapolationResult extrapolate(const PlayerState& current_state, int32_t ticks, const MovementTuning& tuning);

    private:
        ExtrapolationConfig config_;

        PlayerState simulate_tick(const PlayerState& state, float tickrate, const MovementTuning& tuning);
        Vec3 apply_friction(const Vec3& velocity, bool on_ground, float tickrate, const MovementTuning& tuning);
        Vec3 apply_gravity(const Vec3& velocity, bool on_ground, float tickrate, const MovementTuning& tuning);
        bool is_valid_position(const Vec3& position);
    };
}