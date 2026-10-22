// SPDX-License-Identifier: GPL-3.0-or-later
#include "particles/simulation.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rct3::particles
{

    namespace
    {

        constexpr float kGravity = 9.81f;
        constexpr float kTau = 2 * std::numbers::pi_v<float>;

        float fraction(random::RandomState& r)
        {
            return static_cast<float>(random::unit(r));
        }

        /// A point evenly spread in the ball of radius 1.
        glm::vec3 in_ball(random::RandomState& r)
        {
            const float z = 2 * fraction(r) - 1;
            const float a = kTau * fraction(r);
            const float across = std::sqrt(std::max(0.0f, 1 - z * z));
            return glm::vec3(across * std::cos(a), across * std::sin(a), z) * std::cbrt(fraction(r));
        }

        /// A point evenly spread in the disc of radius 1 across `up`.
        glm::vec3 in_disc(random::RandomState& r, glm::vec3 up)
        {
            const glm::vec3 side = glm::normalize(glm::cross(up, std::abs(up.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
            const glm::vec3 other = glm::cross(up, side);
            const float a = kTau * fraction(r);
            return (side * std::cos(a) + other * std::sin(a)) * std::sqrt(fraction(r));
        }

        /// The share of `emitter`'s particles due by `t` of its owner's life.
        float due(const Emitter& emitter, float t)
        {
            if (emitter.end <= emitter.start)
                return t >= emitter.start ? 1.0f : 0.0f;
            return std::clamp((t - emitter.start) / (emitter.end - emitter.start), 0.0f, 1.0f);
        }

    } // namespace

    Look look(const Effect& effect, const ParticleArrays& particles, std::size_t i)
    {
        const Particle& kind = effect.particles[particles.kind[i]];
        const float t = std::clamp(particles.age[i] / particles.life[i], 0.0f, 1.0f);
        glm::vec4 colour { kind.colour[0].at(t), kind.colour[1].at(t), kind.colour[2].at(t), kind.colour[3].at(t) };
        if (kind.tinted)
            colour *= effect.tint;
        return { colour, std::lerp(kind.start_size, kind.end_size, t) * particles.scale[i] };
    }

    EffectRun::EffectRun(const Effect& effect, random::RandomState random, bool loops, std::size_t capacity, float strength)
        : effect_(&effect)
        , random_(random)
        , loops_(loops)
        , capacity_(capacity)
        , strength_(strength)
        , root_emitted_(effect.roots.size(), 0.0f)
    {
        for (const Particle& p : effect.particles)
            widest_ = std::max(widest_, p.emitters.size());
        // Reserved whole, so emitting never moves the arrays under a pass over them.
        live_.position.reserve(capacity);
        live_.velocity.reserve(capacity);
        live_.age.reserve(capacity);
        live_.life.reserve(capacity);
        live_.scale.reserve(capacity);
        live_.roll.reserve(capacity);
        live_.kind.reserve(capacity);
        live_.relative.reserve(capacity);
        emitted_.reserve(capacity * widest_);
        life_ = effect.lifetime * (1 - fraction(random_) * effect.lifetime_random);
    }

    void EffectRun::place(glm::vec3 position, const glm::mat3& axes, glm::vec3 velocity)
    {
        // shortcut: carried particles follow the run's move but not its turn, upgrade if turning rides show it
        const glm::vec3 moved = position - position_;
        for (std::size_t i = 0; i < live_.count(); ++i)
            if (live_.relative[i] != 0)
                live_.position[i] += moved;
        position_ = position;
        axes_ = axes;
        velocity_ = velocity;
    }

    void EffectRun::advance(float seconds)
    {
        constexpr float dt = 1 / kParticleSteps;
        pending_ += seconds;
        // A long pause (a dragged window, a slow frame) does not replay everything it missed.
        pending_ = std::min(pending_, 0.25f);
        for (; pending_ >= dt; pending_ -= dt)
            step(dt);
    }

    void EffectRun::emit(const Emitter& emitter, float& emitted, float age, float life, glm::vec3 position, glm::vec3 velocity)
    {
        if (!emitter.particle || life <= 0)
            return;
        const float count = static_cast<float>(emitter.count) * (emitter.count_scaled ? effect_->strength * strength_ : 1.0f);
        const float target = std::floor(count * due(emitter, age / life));
        const Particle& kind = effect_->particles[*emitter.particle];
        for (; emitted < target; ++emitted)
        {
            // shortcut: a full pool drops new particles, upgrade to recycling the oldest if effects thin out
            if (live_.count() >= capacity_)
                continue;
            // The emitter turns over its owner's life; its up is where its particles head.
            const glm::vec3 angles = emitter.spin_offset + emitter.spin_rate * age;
            const glm::quat turn = glm::angleAxis(angles.z, glm::vec3(0, 0, 1)) * glm::angleAxis(angles.y, glm::vec3(0, 1, 0)) * glm::angleAxis(angles.x, glm::vec3(1, 0, 0));
            const glm::vec3 up = glm::normalize(axes_ * (turn * glm::vec3(0, 1, 0)));
            // One draw a statement: within one expression C++ leaves their order open, and a seed must replay.
            const glm::vec3 place_ball = in_ball(random_);
            const glm::vec3 place_disc = in_disc(random_, up);
            const glm::vec3 speed_ball = in_ball(random_);
            const glm::vec3 speed_disc = in_disc(random_, up);
            live_.position.push_back(position + place_ball * emitter.position_random + place_disc * emitter.position_circle);
            live_.velocity.push_back(up * emitter.speed + speed_ball * emitter.speed_random + speed_disc * emitter.speed_circle + velocity * emitter.parent_speed);
            live_.age.push_back(-fraction(random_) * emitter.time_random * life);
            live_.life.push_back(std::max(kind.lifetime * (1 - fraction(random_) * kind.lifetime_random), 1 / kParticleSteps));
            live_.scale.push_back(1 - fraction(random_) * kind.size_random);
            live_.roll.push_back(kTau * fraction(random_));
            live_.kind.push_back(static_cast<std::uint32_t>(*emitter.particle));
            live_.relative.push_back(emitter.relative ? 1 : 0);
            emitted_.resize(live_.count() * widest_, 0.0f);
            std::fill(emitted_.end() - static_cast<std::ptrdiff_t>(widest_), emitted_.end(), 0.0f);
        }
    }

    void EffectRun::step(float dt)
    {
        // The run's own emitters.
        if (age_ < life_)
        {
            const float after = std::min(age_ + dt, life_);
            for (std::size_t i = 0; i < effect_->roots.size(); ++i)
                emit(effect_->emitters[effect_->roots[i]], root_emitted_[i], after, life_, position_, velocity_);
            age_ += dt;
            if (age_ >= life_ && loops_)
            {
                age_ = 0;
                life_ = effect_->lifetime * (1 - fraction(random_) * effect_->lifetime_random);
                std::ranges::fill(root_emitted_, 0.0f);
            }
        }

        // Particles: age, move, emit, die. Ones born this step are moved from the next.
        const std::size_t count = live_.count();
        const glm::vec3 down(0, -kGravity, 0);
        for (std::size_t i = 0; i < count; ++i)
        {
            live_.age[i] += dt;
            if (live_.age[i] <= 0)
                continue;
            const Particle& kind = effect_->particles[live_.kind[i]];
            live_.velocity[i] += down * kind.gravity * dt;
            live_.velocity[i] *= std::max(0.0f, 1 - kind.drag * dt);
            live_.position[i] += live_.velocity[i] * dt;
            live_.roll[i] += kind.roll * dt;
            for (std::size_t e = 0; e < kind.emitters.size(); ++e)
                emit(effect_->emitters[kind.emitters[e]], emitted_[i * widest_ + e], std::min(live_.age[i], live_.life[i]), live_.life[i], live_.position[i], live_.velocity[i]);
        }
        // Dead ones are replaced by the last, in every array, counters too.
        const auto replace = [&](auto& field, std::size_t i) {
            field[i] = field.back();
            field.pop_back();
        };
        for (std::size_t i = 0; i < live_.count();)
        {
            if (live_.age[i] < live_.life[i])
            {
                ++i;
                continue;
            }
            std::copy_n(emitted_.end() - static_cast<std::ptrdiff_t>(widest_), widest_, emitted_.begin() + static_cast<std::ptrdiff_t>(i * widest_));
            emitted_.resize((live_.count() - 1) * widest_);
            replace(live_.position, i);
            replace(live_.velocity, i);
            replace(live_.age, i);
            replace(live_.life, i);
            replace(live_.scale, i);
            replace(live_.roll, i);
            replace(live_.kind, i);
            replace(live_.relative, i);
        }
    }

} // namespace rct3::particles
