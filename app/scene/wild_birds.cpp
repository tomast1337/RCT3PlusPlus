// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/wild_birds.hpp"

#include "core/types.hpp"
#include "logging/log.hpp"
#include "park/water/surface.hpp"
#include "random/generator.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "simulation/systems.hpp"

#include <cmath>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>

namespace rct3::scene
{

    namespace
    {
        /// Seconds one bird's motion runs ahead of the one before, so a flock does not flap as one.
        constexpr f32 kMotionSpread = 0.29f;
        /// A standing gull pecks for this many seconds out of every cycle of the next. Chosen by eye: a gull
        /// that pecks all the time looks busy, one that never does looks stuffed.
        constexpr f32 kPeckSeconds = 2.0f;
        constexpr f32 kPeckCycle = 9.0f;
        /// Water is looked for at this many random points when a flock wants a place to land.
        constexpr i32 kWaterTries = 400;

        rct3::park::BirdMotion motion_of(const simulation::WildBird& bird, f32 clock)
        {
            using rct3::park::BirdMotion;
            const bool duck = bird.kind == simulation::BirdKind::Duck;
            switch (bird.job)
            {
                case simulation::BirdJob::Flying:
                    return BirdMotion::Fly;
                case simulation::BirdJob::Landing:
                    return duck ? BirdMotion::Land : BirdMotion::LandGround;
                case simulation::BirdJob::TakingOff:
                    return duck ? BirdMotion::TakeOff : BirdMotion::TakeOffGround;
                case simulation::BirdJob::Resting:
                    if (duck)
                        return BirdMotion::Swim;
                    return std::fmod(clock, kPeckCycle) < kPeckSeconds ? BirdMotion::Peck : BirdMotion::Stand;
            }
            return BirdMotion::Fly;
        }
    } // namespace

    void load_wild_birds(render::Device& device, park::ParkScene& scene, const config::game::GameInstall& install)
    {
        DrawnBirds& drawn = scene.drawn_birds;
        const auto duck = install.find("Animals/Duck/Duck.common.ovl");
        const auto gull = install.find("Animals/Seagull/Seagull.common.ovl");
        if (duck)
            drawn.duck = rct3::park::read_bird_model(*duck, "Body_D01_L1", "Duck");
        if (gull)
            drawn.gull = rct3::park::read_bird_model(*gull, "Body_SG01_L1", "Seagull");
        rct3::park::ground::GroundImages images;
        for (const auto* model : { &drawn.duck, &drawn.gull })
            if (*model)
                images.emplace((*model)->picture_key, (*model)->picture);
        if (images.empty())
            return;
        park::fit_texture_detail(scene, images);
        if (auto uploaded = ui::upload_textures(device, images))
            drawn.textures.merge(*uploaded);
        else
            logging::logger("park").warning("bird pictures: {}", uploaded.error());
    }

    void place_wild_birds(park::ParkScene& scene)
    {
        DrawnBirds& drawn = scene.drawn_birds;
        drawn.placed.clear();
        for (const auto [e, bird, pose] : scene.sim.registry.view<const simulation::WildBird, const simulation::Pose>().each())
        {
            const auto& model = bird.kind == simulation::BirdKind::Duck ? drawn.duck : drawn.gull;
            if (!model)
                continue;
            const simulation::Pose at = simulation::between_steps(pose, scene.sim_time / simulation::kTickSeconds);
            const f32 offset = static_cast<f32>(entt::to_entity(e)) * kMotionSpread;
            const f32 clock = scene.park_clock + offset;
            const rct3::park::BirdMotion motion = motion_of(bird, clock);
            const bool once = !rct3::park::repeats(motion);
            drawn.placed.push_back({ .model = &*model, .motion = motion, .time = { once ? bird.busy.value : clock }, .palette = bird.palette, .world = glm::rotate(glm::translate(glm::mat4(1.0f), at.position), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f)) });
        }
        // shortcut: every bird is posed on the frame's thread, every frame; fine for the few dozen that come, move
        // to a worker if parks ever hold hundreds.
        drawn.mesh = rct3::park::bird_mesh(drawn.placed);
    }

    simulation::BirdGround bird_ground(const park::ParkScene& scene)
    {
        const save::model::TerrainGrid& g = scene.grid;
        const auto water_at = [&scene, &g](glm::vec2 p) -> std::optional<f32> {
            const f32 floor = g.height_at(p.x, p.y).value_or(0.0f);
            const auto above = rct3::park::water::water_above(scene.water.surface, { p.x, floor - 1000.0f, p.y });
            return above ? std::optional { above->level } : std::nullopt;
        };
        const glm::vec2 low { g.origin_x, g.origin_z };
        const glm::vec2 high = low + glm::vec2 { static_cast<f32>(g.width) * g.tile_size_x, static_cast<f32>(g.height) * g.tile_size_z };
        return {
            .height = [&g](glm::vec2 p) { return g.height_at(p.x, p.y).value_or(0.0f); },
            .water = water_at,
            .water_spot = [water_at, low, high](random::RandomState& dice) -> std::optional<glm::vec2> {
                for (i32 i = 0; i < kWaterTries; ++i)
                {
                    const glm::vec2 p { static_cast<f32>(random::between(dice, static_cast<f64>(low.x), static_cast<f64>(high.x))), static_cast<f32>(random::between(dice, static_cast<f64>(low.y), static_cast<f64>(high.y))) };
                    if (water_at(p))
                        return p;
                }
                return std::nullopt;
            },
            .low = low,
            .high = high,
        };
    }

} // namespace rct3::scene
