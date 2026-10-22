// SPDX-License-Identifier: GPL-3.0-or-later
#include "particles/effect.hpp"

#include "save/save_file.hpp"
#include "save/value.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string_view>

namespace rct3::particles
{

    namespace
    {

        constexpr std::string_view kEffectClass = "ParticleBaseDesc";
        constexpr std::string_view kEmitterClass = "ParticleEmitterDesc";
        constexpr std::string_view kParticleClass = "ParticleDesc";

        float number(const save::DecodedObject& o, std::string_view name, float fallback)
        {
            const auto v = o.find(name);
            if (!v)
                return fallback;
            if (v->kind() == save::ValueKind::Float)
                return v->as_float();
            if (v->kind() == save::ValueKind::Integer)
                return static_cast<float>(v->as_integer());
            return fallback;
        }

        std::string text(const save::DecodedObject& o, std::string_view name)
        {
            const auto v = o.find(name);
            return v && v->kind() == save::ValueKind::String ? std::string(v->text()) : std::string { };
        }

        std::vector<std::uint64_t> references(const save::DecodedObject& o, std::string_view name)
        {
            std::vector<std::uint64_t> out;
            if (const auto v = o.find(name); v && v->kind() == save::ValueKind::List)
                for (const save::Value& item : v->items())
                    if (item.kind() == save::ValueKind::Reference)
                        out.push_back(item.as_reference());
            return out;
        }

        std::vector<float> floats(const save::Value& list)
        {
            std::vector<float> out;
            if (list.kind() == save::ValueKind::List)
                for (const save::Value& item : list.items())
                    if (item.kind() == save::ValueKind::Float)
                        out.push_back(item.as_float());
            return out;
        }

        Curve curve(const save::DecodedObject& o, char channel)
        {
            const std::string name = std::string("Colour") + channel;
            const auto v = o.find(name);
            if (!v || v->kind() != save::ValueKind::Struct)
                return { };
            Curve c;
            if (const auto t = v->field(name + "Times"))
                c.times = floats(*t);
            if (const auto values = v->field(name + "Values"))
                c.values = floats(*values);
            if (const auto w = v->field("wrap"); w && w->kind() == save::ValueKind::Bool)
                c.wrap = w->as_bool();
            const std::size_t n = std::min(c.times.size(), c.values.size());
            c.times.resize(n);
            c.values.resize(n);
            return c;
        }

    } // namespace

    float Curve::at(float t) const noexcept
    {
        if (values.empty())
            return 1;
        if (t <= times.front())
        {
            if (!wrap || times.size() < 2)
                return values.front();
            t += 1; // Before the first time, it is still on its way round from the last
        }
        if (t >= times.back())
        {
            if (!wrap || times.size() < 2)
                return values.back();
            const float gap = times.front() + 1 - times.back();
            return gap > 0 ? std::lerp(values.back(), values.front(), std::clamp((t - times.back()) / gap, 0.0f, 1.0f)) : values.front();
        }
        const auto after = static_cast<std::size_t>(std::ranges::upper_bound(times, t) - times.begin());
        const float span = times[after] - times[after - 1];
        return span > 0 ? std::lerp(values[after - 1], values[after], (t - times[after - 1]) / span) : values[after];
    }

    const char* to_string(EffectError e) noexcept
    {
        switch (e)
        {
            case EffectError::Container:
                return "not a readable container";
            case EffectError::NoEffect:
                return "no effect in it";
            case EffectError::BadObject:
                return "an object could not be decoded";
            case EffectError::Unreferable:
                return "a reference names no object of its kind";
        }
        return "unknown";
    }

    std::expected<Effect, EffectError> read_effect(std::span<const std::byte> data)
    {
        const auto file = save::read_save(data, { .verify_checksum = false });
        if (!file)
            return std::unexpected(EffectError::Container);
        const save::ObjectIndex index(*file);
        const auto class_of = [&](const save::Object& o) { return o.class_index < file->classes.size() ? file->classes[o.class_index].name : std::string_view { }; };
        const auto root = std::ranges::find_if(file->objects, [&](const save::Object& o) { return class_of(o) == kEffectClass; });
        if (root == file->objects.end())
            return std::unexpected(EffectError::NoEffect);

        Effect effect;
        std::map<std::uint64_t, std::size_t> emitter_at;
        std::map<std::uint64_t, std::size_t> particle_at;
        // Objects found but not yet read, by kind, so shared and circular references are read once.
        std::vector<std::pair<std::uint64_t, bool>> pending; // id, is a particle
        const auto emitter_index = [&](std::uint64_t id) -> std::optional<std::size_t> {
            const auto o = index.find(id);
            if (!o || class_of(*o) != kEmitterClass)
                return std::nullopt;
            const auto [at, added] = emitter_at.try_emplace(id, effect.emitters.size());
            if (added)
            {
                effect.emitters.emplace_back();
                pending.emplace_back(id, false);
            }
            return at->second;
        };
        const auto particle_index = [&](std::uint64_t id) -> std::optional<std::size_t> {
            const auto o = index.find(id);
            if (!o || class_of(*o) != kParticleClass)
                return std::nullopt;
            const auto [at, added] = particle_at.try_emplace(id, effect.particles.size());
            if (added)
            {
                effect.particles.emplace_back();
                pending.emplace_back(id, true);
            }
            return at->second;
        };

        const auto base = save::decode_object(*file, *root);
        if (!base)
            return std::unexpected(EffectError::BadObject);
        effect.lifetime = number(*base, "Lifetime", effect.lifetime);
        effect.lifetime_random = number(*base, "LifetimeRandom", 0);
        effect.tint = { number(*base, "ColourModifierR", 1), number(*base, "ColourModifierG", 1), number(*base, "ColourModifierB", 1), number(*base, "ColourModifierA", 1) };
        effect.strength = number(*base, "StrengthModifier", 1);
        effect.sound = text(*base, "SoundName");
        effect.sound_near = number(*base, "SoundMinDist", 0);
        effect.sound_far = number(*base, "SoundMaxDist", 0);
        for (const std::uint64_t id : references(*base, "ParticleEmitterDescs"))
        {
            const auto at = emitter_index(id);
            if (!at)
                return std::unexpected(EffectError::Unreferable);
            effect.roots.push_back(*at);
        }

        // Each object is read once, so this ends after as many rounds as the file has objects.
        while (!pending.empty())
        {
            const auto [id, is_particle] = pending.back();
            pending.pop_back();
            const auto o = save::decode_object(*file, *index.find(id));
            if (!o)
                return std::unexpected(EffectError::BadObject);
            if (!is_particle)
            {
                Emitter e {
                    .start = number(*o, "StartTime", 0),
                    .end = number(*o, "EndTime", 1),
                    .time_random = number(*o, "TimeRandom", 0),
                    .count = static_cast<std::int32_t>(number(*o, "NumParticles", 0)),
                    .count_scaled = number(*o, "NumParticlesModifierEnable", 0) != 0,
                    .speed = number(*o, "ParticleSpeed", 0),
                    .speed_random = number(*o, "SpeedRandom", 0),
                    .speed_circle = number(*o, "SpeedRandomCircle", 0),
                    .position_random = number(*o, "PositionRandom", 0),
                    .position_circle = number(*o, "PositionRandomCircle", 0),
                    .parent_speed = number(*o, "ParentSpeedFactor", 0),
                    .relative = number(*o, "ParticleRelative", 0) != 0,
                    .spin_rate = { number(*o, "SpinRateX", 0), number(*o, "SpinRateY", 0), number(*o, "SpinRateZ", 0) },
                    .spin_offset = { number(*o, "SpinOffsetX", 0), number(*o, "SpinOffsetY", 0), number(*o, "SpinOffsetZ", 0) },
                };
                if (const auto v = o->find("ParticleDesc"); v && v->kind() == save::ValueKind::Reference && v->as_reference() != 0)
                {
                    e.particle = particle_index(v->as_reference());
                    if (!e.particle)
                        return std::unexpected(EffectError::Unreferable);
                }
                effect.emitters[emitter_at.at(id)] = std::move(e);
                continue;
            }
            Particle p {
                .lifetime = number(*o, "Lifetime", 1),
                .lifetime_random = number(*o, "LifetimeRandom", 0),
                .start_size = number(*o, "StartSize", 1),
                .end_size = number(*o, "EndSize", 1),
                .size_random = number(*o, "SizeRandom", 0),
                .gravity = number(*o, "GravityScale", 0),
                .drag = number(*o, "Drag", 0),
                .stretch = number(*o, "VelStretch", 0),
                .up_scale = number(*o, "UpScale", 0),
                .roll = number(*o, "CameraSpinRate", 0),
                .spin_rate = { number(*o, "SpinRateX", 0), number(*o, "SpinRateY", 0) },
                .burn_length = static_cast<std::int32_t>(number(*o, "BurnLength", 0)),
                .burn_size = number(*o, "BurnSize", 0),
                .skin = text(*o, "TextureNameReduced"),
                .colour = { curve(*o, 'R'), curve(*o, 'G'), curve(*o, 'B'), curve(*o, 'A') },
                .tinted = number(*o, "ColourModifierEnable", 0) != 0,
                .sound = text(*o, "SoundName"),
                .sound_near = number(*o, "SoundMinDist", 0),
                .sound_far = number(*o, "SoundMaxDist", 0),
            };
            for (const std::uint64_t child : references(*o, "ParticleEmitterDescs"))
            {
                const auto at = emitter_index(child);
                if (!at)
                    return std::unexpected(EffectError::Unreferable);
                p.emitters.push_back(*at);
            }
            effect.particles[particle_at.at(id)] = std::move(p);
        }
        return effect;
    }

} // namespace rct3::particles
