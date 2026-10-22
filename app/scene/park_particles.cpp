// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_particles.hpp"

#include "logging/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <tuple>

namespace rct3::scene
{

    namespace
    {
        /// Effects farther from the eye than this wait and are not drawn.
        constexpr float kShowDistance = 250;
        /// At most this many particles alive in one effect; the busiest firework keeps about 4300.
        constexpr std::size_t kRunCapacity = 8192;

        struct Corner
        {
            glm::vec3 position;
            glm::vec4 colour;
            glm::vec2 uv;
        };

        constexpr std::array kAttributes {
            render::VertexAttribute { 0, render::VertexFormat::Float3, offsetof(Corner, position) },
            render::VertexAttribute { 1, render::VertexFormat::Float4, offsetof(Corner, colour) },
            render::VertexAttribute { 2, render::VertexFormat::Float2, offsetof(Corner, uv) },
        };
        /// A run of `effect` at `position`, each on numbers of its own; nothing when the install lacks it.
        std::optional<ParkParticles::Shown> start(ParkParticles& particles, std::string_view name, glm::vec3 position, const glm::mat3& axes, bool loops)
        {
            const particles::Effect* effect = particles.library->effect(name);
            if (effect == nullptr)
                return std::nullopt;
            const random::RandomState own = random::seeded(random::Seed { random::next(particles.random) });
            ParkParticles::Shown shown { .run = particles::EffectRun(*effect, own, loops, kRunCapacity), .sprites = { }, .position = position, .effect = std::string(name) };
            shown.run.place(position, axes);
            for (const particles::Particle& kind : effect->particles)
                shown.sprites.push_back(particles.library->sprite(kind.skin));
            return shown;
        }

    } // namespace

    void load_park_particles(render::Device& device, ParkParticles& particles, const std::filesystem::path& root, random::RandomState random)
    {
        particles.random = random;
        auto library = particles::Library::load(root);
        if (!library)
        {
            logging::logger("particles").warning("no particles: {}", library.error());
            return;
        }
        particles.library = std::move(*library);
        for (const auto& [name, image] : particles.library->atlases())
        {
            auto texture = device.create_texture({ .width = image.width, .height = image.height, .pixels = std::as_bytes(std::span(image.pixels)), .label = "particle atlas" });
            if (texture)
                particles.atlases.emplace(name, *texture);
        }
        const render::VertexLayout layout { sizeof(Corner), kAttributes };
        for (auto [pipeline, blend, label] : { std::tuple { &particles.added, render::BlendMode::Add, "particles added" }, std::tuple { &particles.mixed, render::BlendMode::Alpha, "particles mixed" } })
            if (auto made = device.create_pipeline({ .shader = render::Shader::Textured, .layout = layout, .blend = blend, .depth_write = false, .label = label }))
                *pipeline = *made;
    }

    void show_park_particles(ParkParticles& particles, std::span<const park::PlacedEffect> effects)
    {
        particles.shown.clear();
        if (!particles.library)
            return;
        std::size_t missing = 0;
        for (const park::PlacedEffect& placed : effects)
        {
            if (auto shown = start(particles, placed.effect, placed.position, placed.axes, true))
                particles.shown.push_back(std::move(*shown));
            else
                ++missing;
        }
        logging::logger("particles").info("{} particle effects{}", particles.shown.size(), missing > 0 ? std::format(", {} named effects not found", missing) : std::string { });
    }

    bool fire_park_particles(ParkParticles& particles, std::string_view effect, glm::vec3 position, glm::vec3 up)
    {
        if (!particles.library)
            return false;
        const glm::vec3 along = glm::normalize(up);
        const glm::vec3 side = glm::normalize(glm::cross(std::abs(along.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), along));
        auto shown = start(particles, effect, position, glm::mat3(side, along, glm::cross(side, along)), false);
        if (shown)
            particles.fired.push_back(std::move(*shown));
        return shown.has_value();
    }

    void advance_park_particles(ParkParticles& particles, float seconds, glm::vec3 eye, std::size_t budget)
    {
        std::erase_if(particles.fired, [](const ParkParticles::Shown& s) { return s.run.finished(); });
        std::vector<std::pair<float, ParkParticles::Shown*>> near;
        for (auto* list : { &particles.shown, &particles.fired })
            for (ParkParticles::Shown& shown : *list)
            {
                shown.awake = false;
                if (const float d = glm::distance(shown.position, eye); d < kShowDistance)
                    near.emplace_back(d, &shown);
            }
        std::ranges::sort(near, { }, &std::pair<float, ParkParticles::Shown*>::first);
        std::size_t alive = 0;
        for (const auto& [distance, shown] : near)
        {
            if (alive >= budget)
                break;
            shown->run.advance(seconds);
            shown->awake = true;
            alive += shown->run.particles().count();
        }
    }

    void draw_park_particles(render::Device& device, const ParkParticles& particles, const glm::mat4& view_projection, glm::vec3 eye, glm::vec3 target, const render::SceneLight& light)
    {
        if (!particles.library || !particles.added || !particles.mixed)
            return;
        const glm::vec3 forward = glm::normalize(target - eye);
        const glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0, 1, 0), forward));
        const glm::vec3 up = glm::cross(forward, right);
        // shortcut: mixed particles (smoke, spray) take the light from above only, no sun or lamps; upgrade
        // with lit particles if smoke looks flat by day.
        const float dim = std::clamp(std::max({ light.sky.r, light.sky.g, light.sky.b }) + std::max(light.sun_direction.y, 0.0f) * std::max({ light.sun.r, light.sun.g, light.sun.b }), 0.05f, 1.0f);

        // One batch per atlas and blending; mixed ones farthest first.
        struct Quad
        {
            float distance;
            std::array<Corner, 6> corners;
        };
        std::map<std::pair<std::string, bool>, std::vector<Quad>> batches;
        for (const auto* list : { &particles.shown, &particles.fired })
            for (const ParkParticles::Shown& shown : *list)
            {
                if (!shown.awake)
                    continue;
                const particles::ParticleArrays& live = shown.run.particles();
                for (std::size_t i = 0; i < live.count(); ++i)
                {
                    const particles::Sprite* sprite = shown.sprites[live.kind[i]];
                    if (live.age[i] < 0 || sprite == nullptr || sprite->frames.empty())
                        continue;
                    // shortcut: no stretching along the motion, trails or taller drawing yet (VelStretch, BurnLength,
                    // UpScale); upgrade when fireworks are compared with the original.
                    const particles::Look look = particles::look(shown.run.effect(), live, i);
                    // Added sprites (fire, sparks) are only their colour; their opacity curve is left at 0.
                    if (look.size <= 0 || (sprite->blended ? look.colour.a <= 0 : glm::vec3(look.colour) == glm::vec3(0.0f)))
                        continue;
                    const auto& f = sprite->frames[sprite->frame(live.age[i], live.roll[i] / 6.2832f)];
                    const float half = look.size / 2;
                    const glm::vec3 across = (right * std::cos(live.roll[i]) + up * std::sin(live.roll[i])) * half;
                    const glm::vec3 along = (up * std::cos(live.roll[i]) - right * std::sin(live.roll[i])) * half;
                    const glm::vec3 p = live.position[i];
                    const glm::vec4 c = sprite->blended ? glm::vec4(glm::vec3(look.colour) * dim, look.colour.a) : glm::vec4(glm::vec3(look.colour), 1.0f);
                    const Corner a { p - across - along, c, { f[0], f[3] } };
                    const Corner b { p + across - along, c, { f[2], f[3] } };
                    const Corner d { p + across + along, c, { f[2], f[1] } };
                    const Corner e { p - across + along, c, { f[0], f[1] } };
                    batches[{ sprite->atlas, sprite->blended }].push_back({ glm::distance(p, eye), { a, b, d, a, d, e } });
                }
            }
        for (auto& [key, quads] : batches)
        {
            const auto atlas = particles.atlases.find(key.first);
            if (atlas == particles.atlases.end())
                continue;
            if (key.second)
                std::ranges::sort(quads, std::greater { }, &Quad::distance);
            std::vector<Corner> corners;
            corners.reserve(quads.size() * 6);
            for (const Quad& q : quads)
                corners.insert(corners.end(), q.corners.begin(), q.corners.end());
            const auto vertices = device.allocate_transient(std::as_bytes(std::span(corners)));
            if (!vertices)
                continue;
            render::DrawCommand draw;
            draw.pipeline = key.second ? particles.mixed : particles.added;
            draw.vertices = *vertices;
            draw.count = static_cast<std::uint32_t>(corners.size());
            draw.transform = view_projection;
            draw.texture = atlas->second;
            draw.filter = render::Filter::Linear;
            device.draw(draw);
        }
    }

    void destroy_park_particles(render::Device& device, ParkParticles& particles)
    {
        for (const render::PipelineHandle p : { particles.added, particles.mixed })
            if (p)
                device.destroy_pipeline(p);
        for (const auto& [name, texture] : particles.atlases)
            device.destroy_texture(texture);
        particles = { };
    }

} // namespace rct3::scene
