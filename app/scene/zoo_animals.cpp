// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/zoo_animals.hpp"

#include "core/types.hpp"
#include "logging/log.hpp"
#include "park/camera.hpp"
#include "park/item_category.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "simulation/animals.hpp"
#include "simulation/systems.hpp"

#include <array>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string_view>

namespace rct3::scene
{

    namespace
    {
        /// Seconds one animal's motion runs ahead of the one before, so they do not move as one.
        constexpr f32 kMotionSpread = 0.37f;
        /// Of a walk's pace, beyond which an animal is running; over 1 so rounding never makes a walk a run.
        constexpr f32 kRunningOver = 1.1f;
        /// How high the helicopter starts lowering a crate from, and how far above the crate it hangs.
        constexpr units::Distance kDropHeight { 30 };
        constexpr units::Distance kRopeLength { 12 };

        /// Uploads the pictures of the bodies read since the last upload.
        void upload_new_pictures(render::Device& device, park::ParkScene& scene)
        {
            DrawnAnimals& drawn = scene.drawn_animals;
            rct3::park::ground::GroundImages fresh;
            for (const auto& [key, picture] : drawn.library->pictures())
                if (!drawn.textures.contains(key))
                    fresh.emplace(key, picture);
            if (fresh.empty())
                return;
            park::fit_texture_detail(scene, fresh);
            if (auto uploaded = ui::upload_textures(device, fresh))
                drawn.textures.merge(*uploaded);
            else
                logging::logger("park").warning("animal pictures: {}", uploaded.error());
        }

        /// The body `animal` (an Animal, or the Carcass of one) is drawn with, by age and sex.
        template<typename AnimalOrBody> ovl::animal::AnimalAge age_of(const AnimalOrBody& animal)
        {
            using enum ovl::animal::AnimalAge;
            if (animal.adult)
                return animal.male ? AdultMale : AdultFemale;
            return animal.male ? YoungMale : YoungFemale;
        }

        /// The motion of `body` that shows what `animal` is doing, standing at `pose`.
        ovl::animal::AnimalMotion motion_of(const rct3::park::AnimalBody& body, const simulation::Animal& animal, const simulation::Pose& pose)
        {
            using simulation::AnimalJob;
            const bool moving = pose.position != pose.was;
            // A predator after its prey, and the prey, go faster than any animal walks.
            const bool running = glm::distance(pose.position, pose.was) > simulation::kAnimalRates.animal_pace.value * simulation::kTickSeconds * kRunningOver;
            ovl::animal::AnimalMotion motion = ovl::animal::AnimalMotion::Stand;
            if (running && rct3::park::motion_seconds(body, ovl::animal::AnimalMotion::Run) > 0)
                motion = ovl::animal::AnimalMotion::Run;
            else if (moving)
                motion = ovl::animal::AnimalMotion::Walk;
            else if (animal.job == AnimalJob::Feeding)
                motion = animal.hunger.value >= animal.thirst.value ? ovl::animal::AnimalMotion::EatLoop : ovl::animal::AnimalMotion::DrinkLoop;
            else if (animal.job == AnimalJob::Sleeping)
                motion = ovl::animal::AnimalMotion::SleepLoop;
            if (rct3::park::motion_seconds(body, motion) > 0)
                return motion;
            return rct3::park::motion_seconds(body, ovl::animal::AnimalMotion::Stand) > 0 ? ovl::animal::AnimalMotion::Stand : ovl::animal::AnimalMotion::Idle1;
        }
    } // namespace

    void load_zoo_animals(park::ParkScene& scene, const config::game::GameInstall& install)
    {
        constexpr std::string_view kAnimalFolder = "WildAnimals";
        const auto folder = install.find(kAnimalFolder);
        if (!folder)
            return;
        const std::string name = folder->filename().string();
        const std::array<std::string_view, 1> folders { name };
        scene.drawn_animals.library.emplace(install.root(), folders);
    }

    save::model::PlacedObject animal_crate()
    {
        save::model::PlacedObject crate;
        crate.symbol = "Crate01";
        crate.overlay = "WildAnimals/Crates/Crates";
        return crate;
    }

    save::model::PlacedObject animal_helicopter()
    {
        save::model::PlacedObject helicopter;
        helicopter.symbol = "Helicopter";
        helicopter.overlay = "WildAnimals/Helicopter/Helicopter";
        return helicopter;
    }

    void animals_changed(render::Device& device, park::ParkScene& scene)
    {
        DrawnAnimals& drawn = scene.drawn_animals;
        for (save::model::PlacedObject shape : { animal_crate(), animal_helicopter() })
            if (scene.models.contains(rct3::park::object::model_key(shape)))
                park::add_textures(device, scene, rct3::park::moving_meshes(std::span(&shape, 1), scene.models, scene.grid).mesh.batches);
        if (drawn.library)
        {
            for (const auto [e, animal] : scene.sim.registry.view<const simulation::Animal>().each())
                (void)drawn.library->body(animal.species, age_of(animal));
            drawn.library->close_files();
            upload_new_pictures(device, scene);
        }
        drawn.crated.clear();
        drawn.crates = { };
        place_animals(scene, rct3::park::eye(scene.camera, scene.seat));
    }

    void place_animals(park::ParkScene& scene, const glm::vec3& eye)
    {
        DrawnAnimals& drawn = scene.drawn_animals;
        const auto& r = scene.sim.registry;
        drawn.placed.clear();
        drawn.placed_animals.clear();
        std::vector<entt::entity> crated;
        std::vector<glm::mat4> crate_places;
        std::vector<entt::entity> flown;
        std::vector<glm::mat4> helicopter_places;
        const save::model::PlacedObject crate = animal_crate();
        const save::model::PlacedObject helicopter = animal_helicopter();
        const bool helicopters = scene.models.contains(rct3::park::object::model_key(helicopter));
        for (const auto [e, animal, pose] : r.view<const simulation::Animal, const simulation::Pose>().each())
        {
            const simulation::Pose at = simulation::between_steps(pose, scene.sim_time / simulation::kTickSeconds);
            const glm::mat4 world = glm::rotate(glm::translate(glm::mat4(1.0f), at.position), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
            const auto body = animal.job == simulation::AnimalJob::Crated || !drawn.library ? std::nullopt : drawn.library->body(animal.species, age_of(animal));
            if (!body)
            {
                // shortcut: the helicopter hangs still and draws no rope; animate its rotor and draw the rope once
                // model animations play on moving meshes.
                const f32 lift = animal.flying.value / simulation::kAnimalRates.delivery.value * kDropHeight.value;
                const glm::mat4 lifted = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, lift, 0.0f)) * world;
                crated.push_back(e);
                crate_places.push_back(lifted);
                if (animal.flying.value > 0 && helicopters)
                {
                    flown.push_back(e);
                    helicopter_places.push_back(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, kRopeLength.value, 0.0f)) * lifted);
                }
                continue;
            }
            const f32 offset = static_cast<f32>(entt::to_entity(e)) * kMotionSpread;
            drawn.placed.push_back({ .body = &*body, .motion = motion_of(*body, animal, pose), .time = { scene.park_clock + offset }, .world = world });
            drawn.placed_animals.push_back(e);
        }
        if (crated != drawn.crated || flown != drawn.flown)
        {
            std::vector<save::model::PlacedObject> shapes(crated.size(), crate);
            shapes.insert(shapes.end(), flown.size(), helicopter);
            for (usize i = 0; i < shapes.size(); ++i)
                shapes[i].id = i + 1;
            drawn.crates = scene.models.contains(rct3::park::object::model_key(crate)) ? rct3::park::moving_meshes(shapes, scene.models, scene.grid) : rct3::park::MovingMeshes { };
            drawn.crated = std::move(crated);
            drawn.flown = std::move(flown);
        }
        // The bodies of killed animals, on their side.
        // shortcut: a body is its species' standing pose rolled over; draw it lying as the game does once its
        // motions for that are read.
        for (const auto [c, carcass, pose] : r.view<const simulation::Carcass, const simulation::Pose>().each())
        {
            const auto body = drawn.library ? drawn.library->body(carcass.species, age_of(carcass)) : std::nullopt;
            if (!body)
                continue;
            const f32 half_wide = (body->high.x - body->low.x) / 2;
            const glm::mat4 upright = glm::rotate(glm::translate(glm::mat4(1.0f), pose.position + glm::vec3(0.0f, half_wide, 0.0f)), pose.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
            drawn.placed.push_back({ .body = &*body, .motion = ovl::animal::AnimalMotion::Stand, .time = { }, .world = glm::rotate(upright, glm::half_pi<f32>(), glm::vec3(0.0f, 0.0f, 1.0f)) });
        }
        crate_places.insert(crate_places.end(), helicopter_places.begin(), helicopter_places.end());
        rct3::park::move_meshes(drawn.crates, crate_places);
        // Each at the level of detail its distance from `eye` calls for, by the animal options; too far, it is not
        // drawn, though it is still placed, to be clicked and followed.
        // shortcut: every animal near enough is posed on the frame's thread, every frame; fine for the few dozen a
        // zoo holds, move to a worker once parks hold more.
        const rct3::park::object::LevelScale scale = scene.level_scales[static_cast<usize>(rct3::park::DetailKind::Animals)];
        std::vector<rct3::park::PlacedAnimal> near;
        for (rct3::park::PlacedAnimal& animal : drawn.placed)
            if (const auto level = rct3::park::animal_level(glm::distance(eye, glm::vec3(animal.world[3])), scale))
            {
                animal.level = *level;
                near.push_back(animal);
            }
        drawn.mesh = rct3::park::animal_mesh(near, drawn.runs);
    }

} // namespace rct3::scene
