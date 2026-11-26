#include "extrapolation.h"
#include <algorithm>

namespace extrapolation
{
    ExtrapolationResult Extrapolator::extrapolate(const PlayerState& current_state, int32_t ticks, const MovementTuning& tuning)
    {
        ExtrapolationResult result;
        if (!config_.enabled || ticks <= 0) { result.valid = false; return result; }

        int32_t n = std::min(ticks, config_.max_ticks);
        PlayerState state = current_state;
        result.all_ticks.reserve(n);

        for (int32_t i = 0; i < n; i++) {
            state = simulate_tick(state, MOVE_TICK, tuning);
            if (!is_valid_position(state.position)) { result.valid = false; return result; }
            result.all_ticks.push_back(state);
        }

        result.valid = true;
        result.ticks_extrapolated = n;
        result.final_state = state;
        return result;
    }

    PlayerState Extrapolator::simulate_tick(const PlayerState& state, float tickrate, const MovementTuning& tuning)
    {
        PlayerState next = state;
        Vec3 vel = state.velocity;

        // Нет данных о wish-направлении цели — разгон к инпуту не симулируем (в отличие от примера,
        // который предполагал готовый вектор acceleration). Только трение и, если в воздухе, гравитация.
        if (!state.on_ground) vel = apply_gravity(vel, state.on_ground, tickrate, tuning);
        vel = apply_friction(vel, state.on_ground, tickrate, tuning);

        next.position = state.position + vel * tickrate;
        next.velocity = vel;
        next.tick = state.tick + 1;
        return next;
    }

    Vec3 Extrapolator::apply_friction(const Vec3& velocity, bool on_ground, float tickrate, const MovementTuning& tuning)
    {
        float speed = velocity.length();
        if (speed < STOP_EPSILON) return Vec3();

        // Настоящая формула трения Source-движка (публично известна, не секрет игры): control
        // ограничен снизу через stopSpeed, чтобы трение не схлопывалось в ноль на малой скорости.
        float frictionCoeff = on_ground ? tuning.friction : tuning.airFriction;
        float control = (speed < tuning.stopSpeed) ? tuning.stopSpeed : speed;
        float drop = control * frictionCoeff * tickrate;
        float newSpeed = std::max(0.f, speed - drop);
        if (newSpeed <= 0.f) return Vec3();
        return velocity * (newSpeed / speed);
    }

    Vec3 Extrapolator::apply_gravity(const Vec3& velocity, bool on_ground, float tickrate, const MovementTuning& tuning)
    {
        if (on_ground) return velocity;
        Vec3 result = velocity;
        result.y -= tuning.gravity * tickrate; // Unity: Y — вертикаль, не Z
        return result;
    }

    bool Extrapolator::is_valid_position(const Vec3& position)
    {
        if (std::isnan(position.x) || std::isnan(position.y) || std::isnan(position.z)) return false;
        if (std::isinf(position.x) || std::isinf(position.y) || std::isinf(position.z)) return false;
        constexpr float kBound = 1e6f; // та же граница, что Finite() в aim.mm, не чужой MAX_COORD
        return std::fabs(position.x) < kBound && std::fabs(position.y) < kBound && std::fabs(position.z) < kBound;
    }
}