// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_water.hpp"

#include "park/object_pick.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace rct3::scene
{

    namespace
    {
        constexpr std::uint32_t kWaveSize = 128;
        constexpr int kWaveTrains = 14;

        /// A height picture that tiles: a sum of waves each fitting a whole number of times across it, running in
        /// scattered directions, longer ones stronger, as on open water.
        std::vector<std::byte> wave_picture()
        {
            struct Train
            {
                float kx, ky, phase, amplitude;
            };
            std::vector<Train> trains;
            std::uint32_t state = 20261016u; // Any seed; fixed so every load looks the same
            const auto next = [&state] {
                state = state * 1664525u + 1013904223u;
                return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
            };
            const float tau = 2 * std::numbers::pi_v<float>;
            for (int i = 0; i < kWaveTrains; ++i)
            {
                const float angle = next() * tau;
                const float cycles = 1 + std::floor(next() * 7);
                const float kx = std::round(std::cos(angle) * cycles);
                const float ky = std::round(std::sin(angle) * cycles);
                if (kx == 0 && ky == 0)
                    continue;
                trains.push_back({ kx, ky, next() * tau, 1 / std::hypot(kx, ky) });
            }
            std::vector<float> height(std::size_t { kWaveSize } * kWaveSize);
            for (std::uint32_t y = 0; y < kWaveSize; ++y)
            {
                for (std::uint32_t x = 0; x < kWaveSize; ++x)
                {
                    float h = 0;
                    for (const Train& t : trains)
                        h += t.amplitude * std::sin(tau * (t.kx * static_cast<float>(x) + t.ky * static_cast<float>(y)) / kWaveSize + t.phase);
                    height[y * kWaveSize + x] = h;
                }
            }
            const auto [low, high] = std::ranges::minmax(height);
            std::vector<std::byte> pixels(height.size() * 4);
            for (std::size_t i = 0; i < height.size(); ++i)
            {
                const auto v = static_cast<std::byte>(std::lround(255 * (height[i] - low) / std::max(high - low, 1e-6f)));
                pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = v;
                pixels[i * 4 + 3] = std::byte { 255 };
            }
            return pixels;
        }

        /// Draws, with the eye under `water.eye_in`, one quad just past the near plane over the whole view, in
        /// which the water shader fogs the copied scene by the water each line of sight crosses.
        void fog_under_water(render::Device& device, const ParkWater& water, const glm::mat4& transform, const render::SceneLight& light, render::WaterView view)
        {
            const glm::mat4 back = glm::inverse(transform);
            std::array<park::GroundVertex, 6> quad;
            constexpr std::array<glm::vec2, 6> kCorners { glm::vec2 { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };
            for (std::size_t i = 0; i < quad.size(); ++i)
            {
                const glm::vec4 near = back * glm::vec4(kCorners[i], 0.0f, 1.0f);
                // Half again as far as the near plane, so the depth test keeps the quad in front of everything.
                quad[i] = { .position = view.eye + (glm::vec3(near) / near.w - view.eye) * 1.5f, .colour = water.eye_in->colour, .uv = { water.eye_in->level, 0.0f } };
            }
            const auto vertices = device.allocate_transient(std::as_bytes(std::span(quad)));
            if (!vertices)
                return;
            render::DrawCommand draw;
            draw.pipeline = water.shaded;
            draw.vertices = *vertices;
            draw.count = static_cast<std::uint32_t>(quad.size());
            draw.transform = transform;
            draw.texture = water.waves;
            draw.filter = render::Filter::Linear;
            draw.address = render::AddressMode::Repeat;
            draw.light = light;
            draw.extra_textures = { render::TextureHandle { }, water.behind, device.depth_texture(water.behind), water.ripple_texture };
            draw.water = view;
            draw.water.underwater = true;
            device.draw(draw);
        }

        std::span<const std::byte> ripple_bytes(const park::RippleTexels& texels)
        {
            return std::as_bytes(std::span(texels));
        }

        /// Makes `texture` a target of `size`, again if its size changed. False if it could not.
        bool fit_target(render::Device& device, render::TextureHandle& texture, render::Extent& made, render::Extent size, bool readable_depth, std::string_view label)
        {
            if (texture && made.width == size.width && made.height == size.height)
                return true;
            if (texture)
                device.destroy_texture(texture);
            texture = { };
            render::TextureDesc desc;
            desc.width = size.width;
            desc.height = size.height;
            desc.target = true;
            desc.readable_depth = readable_depth;
            desc.label = label;
            auto created = device.create_texture(desc);
            if (!created)
                return false;
            texture = *created;
            made = size;
            return true;
        }
    } // namespace

    WaterLook water_look(const config::GameOptions& options)
    {
        using config::GameOption;
        return {
            .reflection = options.flag(GameOption::ReflectiveWater),
            .refraction = options.flag(GameOption::Refraction),
            .waves = options.flag(GameOption::WaterBumps),
            .reflect_background = options.flag(GameOption::ReflectBackground),
            .reflect_terrain = options.flag(GameOption::ReflectTerrain),
            .reflect_scenery = options.flag(GameOption::ReflectScenery),
            .reflect_people = options.flag(GameOption::ReflectPeople),
        };
    }

    std::expected<void, std::string> create_park_water(render::Device& device, ParkWater& water, std::span<const park::WaterVertex> mesh, const render::VertexLayout& layout)
    {
        water.surface = park::water_surface(mesh);
        if (water.surface.vertices.empty())
            return { };
        auto vertices = device.create_buffer({ .usage = render::BufferUsage::Vertex, .data = std::as_bytes(std::span(water.surface.vertices)), .label = "water" });
        if (!vertices)
            return std::unexpected(vertices.error());
        water.vertices = *vertices;
        auto simple = device.create_pipeline({ .shader = render::Shader::Lit, .layout = layout, .blend = render::BlendMode::Alpha, .depth_write = false, .label = "water" });
        if (!simple)
            return std::unexpected(simple.error());
        water.simple = *simple;

        const std::vector<std::byte> waves = wave_picture();
        auto picture = device.create_texture({ .width = kWaveSize, .height = kWaveSize, .pixels = waves, .label = "water waves" });
        if (!picture)
            return std::unexpected(picture.error());
        water.waves = *picture;
        constexpr std::array kWhite { std::byte { 255 }, std::byte { 255 }, std::byte { 255 }, std::byte { 255 } };
        auto white = device.create_texture({ .width = 1, .height = 1, .pixels = kWhite, .label = "water white" });
        if (!white)
            return std::unexpected(white.error());
        water.white = *white;
        const park::RippleTexels none = park::ripple_texels(water.ripples);
        auto ripples = device.create_texture({ .width = park::kMaxRipples, .height = 2, .format = render::TextureFormat::Rgba32Float, .pixels = ripple_bytes(none), .label = "water ripples" });
        if (!ripples)
            return std::unexpected(ripples.error());
        water.ripple_texture = *ripples;
        // Without it the simple surface is drawn instead; that is no failure.
        if (auto shaded = device.create_pipeline({ .shader = render::Shader::Water, .layout = layout, .blend = render::BlendMode::Alpha, .depth_write = false, .label = "water shaded" }))
            water.shaded = *shaded;
        return { };
    }

    void destroy_park_water(render::Device& device, ParkWater& water)
    {
        if (water.vertices)
            device.destroy_buffer(water.vertices);
        for (const render::PipelineHandle p : { water.shaded, water.simple })
        {
            if (p)
                device.destroy_pipeline(p);
        }
        for (const render::TextureHandle t : { water.waves, water.white, water.ripple_texture, water.behind, water.reflections[0], water.reflections[1] })
        {
            if (t)
                device.destroy_texture(t);
        }
        water = { };
    }

    void advance_park_water(ParkWater& water, float seconds)
    {
        park::advance_ripples(water.ripples, seconds);
    }

    bool ripple_park_water(ParkWater& water, const park::Ray& ray, const save::TerrainGrid& grid)
    {
        const auto hit = park::water_hit(ray, water.surface);
        if (!hit)
            return false;
        // Ground standing above the water, between it and the eye, hides it.
        if (const auto ground = park::ground_hit(ray, grid, *hit); ground && *ground < *hit - 1e-3f)
            return false;
        const glm::vec3 at = ray.origin + ray.direction * *hit;
        park::start_ripple(water.ripples, { at.x, at.z });
        return true;
    }

    void prepare_park_water(render::Device& device, ParkWater& water, const glm::mat4& view_projection, glm::vec3 eye, render::Color sky, const MirroredDraw& mirrored)
    {
        water.levels.clear();
        water.have_behind = false;
        water.eye_in.reset();
        if (!water.vertices || !water.shaded)
            return;
        water.eye_in = park::water_above(water.surface, eye);
        device.end_pass();
        const park::RippleTexels texels = park::ripple_texels(water.ripples);
        device.update_texture(water.ripple_texture, ripple_bytes(texels));
        const render::Extent screen = device.extent();
        // Under water, the scene is fogged from its copy even where the options leave refraction out.
        if (water.look.refraction || water.eye_in)
            fit_target(device, water.behind, water.behind_size, screen, true, "water behind");
        if (water.look.reflection)
        {
            const render::Extent half { std::max(screen.width / 2, 1u), std::max(screen.height / 2, 1u) };
            for (const float level : park::reflection_levels(water.surface, view_projection, eye, kWaterReflections))
            {
                const std::size_t i = water.levels.size();
                if (!fit_target(device, water.reflections[i], water.reflection_sizes[i], half, false, "water reflection"))
                    break;
                water.levels.push_back(level);
                render::PassDesc into;
                into.target = water.reflections[i];
                into.clear_color = sky;
                device.begin_pass(into);
                mirrored(park::reflection_transform(view_projection, level));
                device.end_pass();
            }
        }
        render::PassDesc back;
        back.clear = false;
        device.begin_pass(back);
    }

    void draw_park_water(render::Device& device, ParkWater& water, const glm::mat4& transform, const render::SceneLight& light, glm::vec3 eye, float time, bool shaded)
    {
        if (!water.vertices)
            return;
        shaded = shaded && water.shaded;
        if (shaded && (water.look.refraction || water.eye_in) && water.behind)
        {
            device.end_pass();
            water.have_behind = device.copy_screen(water.behind);
            render::PassDesc back;
            back.clear = false;
            device.begin_pass(back);
        }
        const auto shaded_view = [&](bool reflection) {
            return render::WaterView {
                .eye = eye,
                .time = time,
                .near = park::kNear,
                .far = park::kFar,
                .reflection = reflection,
                .refraction = water.have_behind,
                .waves = water.look.waves,
                .foam = true,
                .ripples = park::live_ripples(water.ripples) > 0,
            };
        };
        if (shaded && water.eye_in && water.have_behind)
            fog_under_water(device, water, transform, light, shaded_view(false));
        for (const park::WaterBatch& batch : water.surface.batches)
        {
            render::DrawCommand draw;
            draw.pipeline = shaded ? water.shaded : water.simple;
            draw.vertices = { water.vertices };
            draw.first = batch.first;
            draw.count = batch.count;
            draw.transform = transform;
            draw.texture = water.waves;
            draw.filter = render::Filter::Linear;
            draw.address = render::AddressMode::Repeat;
            draw.light = light;
            if (shaded)
            {
                const auto reflection = park::reflection_for(batch.level, water.levels);
                draw.extra_textures = { reflection ? water.reflections[*reflection] : render::TextureHandle { }, water.have_behind ? water.behind : render::TextureHandle { }, water.have_behind ? device.depth_texture(water.behind) : render::TextureHandle { }, water.ripple_texture };
                draw.water = shaded_view(reflection.has_value());
            }
            else
            {
                // The simple surface is lit as facing up and tinted by the picture: a plain white one.
                draw.texture = water.white;
            }
            device.draw(draw);
        }
    }

} // namespace rct3::scene
