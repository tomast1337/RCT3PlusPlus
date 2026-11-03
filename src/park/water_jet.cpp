// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/water_jet.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rct3::park
{

    namespace
    {
        constexpr float kDropsPerSecond = 20;              ///< Chosen by eye: enough rings that a swinging jet still bends smoothly
        constexpr int kSides = 8;                          ///< Chosen by eye: round enough close up
        constexpr float kPictureLength = 3;                ///< Chosen by eye: metres of jet the picture covers
        constexpr float kPictureSpeed = 0.8f;              ///< Chosen by eye: pictures a second the water runs along it
        constexpr float kFlare = 1.0f;                     ///< Chosen by eye: how much wider than its spread broken water gets
        constexpr float kRagged = 0.9f;                    ///< Chosen by eye: of its radius, how far broken water's edge wanders
        constexpr float kGaps = 0.85f;                     ///< Chosen by eye: most of broken water's opacity its gaps take
        constexpr float kThinning = 0.6f;                  ///< Chosen by eye: of the clear tube's opacity, how much is gone where the water is all broken
        constexpr float kSprayWider = 1.15f;               ///< Chosen by eye: the frothy tube round the clear one
        constexpr float kFading = 0.3f;                    ///< Chosen by eye: last share of its life over which broken water fades
        constexpr float kLeastAngle = 0.002f;              ///< Chosen by eye: radians the tube's radius spans at least, seen from afar
        constexpr glm::vec3 kWhole { 1.5f, 1.65f, 1.85f }; ///< Chosen by eye: tint of whole water, bright and a little blue
        constexpr glm::vec3 kSpray { 2.6f, 2.6f, 2.6f };   ///< Chosen by eye: of broken water, white

        /// A steady number 0 to 1 for a drop's ring corner.
        float noise(std::uint32_t serial, std::uint32_t corner)
        {
            std::uint32_t h = (serial * 9 + corner) * 2654435761U;
            h ^= h >> 15;
            h *= 2246822519U;
            h ^= h >> 13;
            return static_cast<float>(h & 0xffffU) / 65535.0f;
        }
    } // namespace

    WaterJet::WaterJet(float life, float radius, float spread, float breakup)
        : life_(life)
        , radius_(radius)
        , spread_(spread)
        , breakup_(breakup)
    {
    }

    float WaterJet::aged(const JetDrop& drop) const noexcept
    {
        return std::clamp(drop.age / life_, 0.0f, 1.0f);
    }

    float WaterJet::broken(const JetDrop& drop) const noexcept
    {
        return breakup_ * aged(drop);
    }

    float WaterJet::radius(const JetDrop& drop) const noexcept
    {
        const float b = broken(drop);
        return (radius_ + spread_ * drop.age) * (1 + kFlare * b * b);
    }

    void WaterJet::aim(glm::vec3 nozzle, glm::vec3 velocity)
    {
        if (!shooting_)
        {
            shooting_ = true;
            drops_.insert(drops_.begin(), { .position = nozzle, .velocity = velocity, .serial = shot_++ });
        }
        nozzle_ = nozzle;
        velocity_ = velocity;
        // The newest drop stays on the nozzle until the next leaves, so the jet starts where the nozzle is.
        drops_.front().position = nozzle;
        drops_.front().velocity = velocity;
    }

    void WaterJet::stop()
    {
        shooting_ = false;
    }

    void WaterJet::advance(float seconds)
    {
        constexpr float kStep = 1 / kDropsPerSecond;
        pending_ += seconds;
        for (int steps = 0; pending_ >= kStep && steps < 64; ++steps)
        {
            pending_ -= kStep;
            for (JetDrop& d : drops_)
            {
                d.velocity.y -= kJetGravity * kStep;
                d.position += d.velocity * kStep;
                d.age += kStep;
            }
            std::erase_if(drops_, [&](const JetDrop& d) { return d.age > life_; });
            if (shooting_)
                drops_.insert(drops_.begin(), { .position = nozzle_, .velocity = velocity_, .serial = shot_++ });
        }
        pending_ = std::min(pending_, kStep);
    }

    std::optional<glm::vec3> WaterJet::landing(const std::function<float(glm::vec2)>& ground) const
    {
        for (std::size_t i = 1; i < drops_.size(); ++i)
        {
            const glm::vec3 a = drops_[i - 1].position;
            const glm::vec3 b = drops_[i].position;
            const float below = ground({ b.x, b.z }) - b.y;
            if (below < 0)
                continue;
            const float above = a.y - ground({ a.x, a.z });
            return glm::mix(a, b, above + below > 0 ? std::clamp(above / (above + below), 0.0f, 1.0f) : 1.0f);
        }
        return std::nullopt;
    }

    std::optional<Throw> throw_to(glm::vec3 from, glm::vec3 to, float angle)
    {
        const glm::vec2 level { to.x - from.x, to.z - from.z };
        const float across = glm::length(level);
        const float rise = across * std::tan(angle) - (to.y - from.y);
        if (across < 1e-3f || rise <= 0)
            return std::nullopt;
        // Thrown at speed v, it falls g t^2 / 2 below its straight line by the time t it is `across` along.
        const float level_speed = across * std::sqrt(kJetGravity / (2 * rise));
        const glm::vec2 way = level / across;
        return Throw { .velocity = { way.x * level_speed, level_speed * std::tan(angle), way.y * level_speed }, .seconds = across / level_speed };
    }

    void jet_mesh(std::span<const WaterJet* const> jets, glm::vec3 eye, float seconds, ObjectMesh& mesh)
    {
        // Water breaking up shows through a second, frothy tube, as the clear water in the first thins.
        for (const std::string_view picture : { kJetPicture, kJetSpray })
        {
            const bool spray = picture == kJetSpray;
            const auto first = static_cast<std::uint32_t>(mesh.indices.size());
            for (const WaterJet* jet : jets)
            {
                const std::span<const JetDrop> drops = jet->drops();
                if (drops.size() < 2 || (spray && jet->broken(drops.back()) == 0))
                    continue;
                float run = 0;
                for (std::size_t i = 0; i < drops.size(); ++i)
                {
                    const glm::vec3 p = drops[i].position;
                    const glm::vec3 step = i + 1 < drops.size() ? drops[i + 1].position - p : p - drops[i - 1].position;
                    const glm::vec3 way = glm::dot(step, step) > 1e-10f ? glm::normalize(step) : glm::vec3(0, 0, 1);
                    const glm::vec3 side = glm::normalize(glm::cross(way, std::abs(way.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
                    const glm::vec3 lift = glm::cross(side, way);
                    const glm::vec3 view = glm::normalize(eye - p);
                    // Thin at the oldest end; water that breaks up also fades away over the last of its life.
                    const float tail = std::clamp((static_cast<float>(drops.size() - 1 - i)) / 3.0f, 0.0f, 1.0f);
                    const float broken = jet->broken(drops[i]);
                    const float fade = std::min(tail, broken > 0 ? std::clamp((1 - jet->aged(drops[i])) / kFading, 0.0f, 1.0f) : 1.0f);
                    // Too thin to cover a pixel from afar, it is drawn wider and fainter, so it neither flickers nor vanishes.
                    const float radius = jet->radius(drops[i]) * tail;
                    const float least = glm::distance(eye, p) * kLeastAngle;
                    const float thin = radius < least ? std::max(radius / least, 0.5f) : 1.0f;
                    const glm::vec3 tint = spray ? kSpray : glm::mix(kWhole, kSpray, broken);
                    const float share = spray ? broken : 1 - kThinning * broken;
                    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                    for (int k = 0; k <= kSides; ++k)
                    {
                        const float turn = 2 * std::numbers::pi_v<float> * static_cast<float>(k) / kSides;
                        const glm::vec3 out = side * std::cos(turn) + lift * std::sin(turn);
                        const float wander = noise(drops[i].serial, static_cast<std::uint32_t>(k % kSides));
                        const float gap = noise(drops[i].serial, static_cast<std::uint32_t>(k % kSides) + kSides);
                        // Seen edge on, water looks thicker; broken water lets more through face on, and has gaps.
                        const float rim = 1 - std::abs(glm::dot(out, view));
                        const float alpha = glm::mix(0.85f + 0.15f * rim * rim, 0.25f + 0.5f * rim * rim, broken) * (1 - kGaps * broken * gap) * fade * thin * share;
                        mesh.vertices.push_back({ .position = p + out * std::max(radius, least) * (spray ? kSprayWider : 1.0f) * (1 + kRagged * broken * (wander - 0.5f)), .colour = { tint, alpha }, .uv = { static_cast<float>(k) / kSides, (run - seconds * kPictureSpeed * kPictureLength) / kPictureLength }, .normal = out });
                        mesh.owners.push_back(0);
                    }
                    if (i + 1 < drops.size())
                    {
                        run += glm::distance(p, drops[i + 1].position);
                        for (std::uint32_t k = 0; k < kSides; ++k)
                        {
                            const std::uint32_t a = base + k;
                            const std::uint32_t b = a + kSides + 1;
                            mesh.indices.insert(mesh.indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
                        }
                    }
                }
            }
            const auto count = static_cast<std::uint32_t>(mesh.indices.size()) - first;
            if (count != 0)
                mesh.batches.push_back({ .texture = std::string(picture), .picture = std::string(picture), .blend = PartBlend::Blended, .first = first, .count = count });
        }
    }

} // namespace rct3::park
