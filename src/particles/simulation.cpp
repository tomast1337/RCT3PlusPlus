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

    Look look(const Effect& effect, const LiveParticle& p)
    {
        const Particle& kind = effect.particles[p.kind];
        const float t = std::clamp(p.age / p.life, 0.0f, 1.0f);
        glm::vec4 colour { kind.colour[0].at(t), kind.colour[1].at(t), kind.colour[2].at(t), kind.colour[3].at(t) };
        if (kind.tinted)
            colour *= effect.tint;
        return { colour, std::lerp(kind.start_size, kind.end_size, t) * p.size };
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
        live_.reserve(capacity);
        emitted_.reserve(capacity * widest_);
        life_ = effect.lifetime * (1 - fraction(random_) * effect.lifetime_random);
    }

    void EffectRun::place(glm::vec3 position, const glm::mat3& axes, glm::vec3 velocity)
    {
        // shortcut: carried particles follow the run's move but not its turn, upgrade if turning rides show it
        const glm::vec3 moved = position - position_;
        for (LiveParticle& p : live_)
            if (p.relative)
                p.position += moved;
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
            if (live_.size() >= capacity_)
                continue;
            // The emitter turns over its owner's life; its up is where its particles head.
            const glm::vec3 angles = emitter.spin_offset + emitter.spin_rate * age;
            const glm::quat turn = glm::angleAxis(angles.z, glm::vec3(0, 0, 1)) * glm::angleAxis(angles.y, glm::vec3(0, 1, 0)) * glm::angleAxis(angles.x, glm::vec3(1, 0, 0));
            const glm::vec3 up = glm::normalize(axes_ * (turn * glm::vec3(0, 1, 0)));
            LiveParticle p {
                .position = position + in_ball(random_) * emitter.position_random + in_disc(random_, up) * emitter.position_circle,
                .velocity = up * emitter.speed + in_ball(random_) * emitter.speed_random + in_disc(random_, up) * emitter.speed_circle + velocity * emitter.parent_speed,
                .age = -fraction(random_) * emitter.time_random * life,
                .life = std::max(kind.lifetime * (1 - fraction(random_) * kind.lifetime_random), 1 / kParticleSteps),
                .size = 1 - fraction(random_) * kind.size_random,
                .roll = kTau * fraction(random_),
                .kind = static_cast<std::uint32_t>(*emitter.particle),
                .relative = emitter.relative,
            };
            live_.push_back(p);
            emitted_.resize(live_.size() * widest_, 0.0f);
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
        const std::size_t count = live_.size();
        const glm::vec3 down(0, -kGravity, 0);
        for (std::size_t i = 0; i < count; ++i)
        {
            LiveParticle& p = live_[i];
            p.age += dt;
            if (p.age <= 0)
                continue;
            const Particle& kind = effect_->particles[p.kind];
            p.velocity += down * kind.gravity * dt;
            p.velocity *= std::max(0.0f, 1 - kind.drag * dt);
            p.position += p.velocity * dt;
            p.roll += kind.roll * dt;
            for (std::size_t e = 0; e < kind.emitters.size(); ++e)
                emit(effect_->emitters[kind.emitters[e]], emitted_[i * widest_ + e], std::min(live_[i].age, live_[i].life), live_[i].life, live_[i].position, live_[i].velocity);
        }
        // Dead ones are replaced by the last, with their counters.
        for (std::size_t i = 0; i < live_.size();)
        {
            if (live_[i].age < live_[i].life)
            {
                ++i;
                continue;
            }
            live_[i] = live_.back();
            std::copy_n(emitted_.end() - static_cast<std::ptrdiff_t>(widest_), widest_, emitted_.begin() + static_cast<std::ptrdiff_t>(i * widest_));
            live_.pop_back();
            emitted_.resize(live_.size() * widest_);
        }
    }

} // namespace rct3::particles
