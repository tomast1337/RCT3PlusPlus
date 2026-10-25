// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/zoo_animals.hpp"

#include "logging/log.hpp"
#include "scene/park_scene.hpp"
#include "scene/park_setup.hpp"
#include "simulation/animals.hpp"
#include "simulation/systems.hpp"

#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include <string_view>

namespace rct3::scene
{

    namespace
    {
        /// Seconds one animal's motion runs ahead of the one before, so they do not move as one.
        constexpr float kMotionSpread = 0.37f;

        /// Uploads the pictures of the bodies read since the last upload.
        void upload_new_pictures(render::Device& device, ParkScene& scene)
        {
            DrawnAnimals& drawn = scene.drawn_animals;
            park::GroundImages fresh;
            for (const auto& [key, picture] : drawn.library->pictures())
                if (!drawn.textures.contains(key))
                    fresh.emplace(key, picture);
            if (fresh.empty())
                return;
            fit_texture_detail(scene, fresh);
            if (auto uploaded = ui::upload_textures(device, fresh))
                drawn.textures.merge(*uploaded);
            else
                logging::logger("park").warning("animal pictures: {}", uploaded.error());
        }

        /// The motion of `body` that shows what `animal` is doing, standing at `pose`.
        ovl::AnimalMotion motion_of(const park::AnimalBody& body, const simulation::Animal& animal, const simulation::Pose& pose)
        {
            using simulation::AnimalJob;
            const bool moving = pose.position != pose.was;
            ovl::AnimalMotion motion = ovl::AnimalMotion::Stand;
            if (moving)
                motion = ovl::AnimalMotion::Walk;
            else if (animal.job == AnimalJob::Feeding)
                motion = animal.hunger.value >= animal.thirst.value ? ovl::AnimalMotion::EatLoop : ovl::AnimalMotion::DrinkLoop;
            else if (animal.job == AnimalJob::Sleeping)
                motion = ovl::AnimalMotion::SleepLoop;
            if (park::motion_seconds(body, motion) > 0)
                return motion;
            return park::motion_seconds(body, ovl::AnimalMotion::Stand) > 0 ? ovl::AnimalMotion::Stand : ovl::AnimalMotion::Idle1;
        }
    } // namespace

    void load_zoo_animals(ParkScene& scene, const config::GameInstall& install)
    {
        constexpr std::string_view kAnimalFolder = "WildAnimals";
        const auto folder = install.find(kAnimalFolder);
        if (!folder)
            return;
        const std::string name = folder->filename().string();
        const std::array<std::string_view, 1> folders { name };
        scene.drawn_animals.library.emplace(install.root(), folders);
    }

    save::PlacedObject animal_crate()
    {
        save::PlacedObject crate;
        crate.symbol = "Crate01";
        crate.overlay = "WildAnimals/Crates/Crates";
        return crate;
    }

    void animals_changed(render::Device& device, ParkScene& scene)
    {
        DrawnAnimals& drawn = scene.drawn_animals;
        save::PlacedObject crate = animal_crate();
        if (scene.models.contains(park::model_key(crate)))
            add_textures(device, scene, park::moving_meshes(std::span(&crate, 1), scene.models, scene.grid).mesh.batches);
        if (drawn.library)
        {
            for (const auto [e, animal] : scene.sim.registry.view<const simulation::Animal>().each())
                (void)drawn.library->body(animal.species, animal.adult, animal.male);
            drawn.library->close_files();
            upload_new_pictures(device, scene);
        }
        drawn.crated.clear();
        drawn.crates = { };
        place_animals(scene);
    }

    void place_animals(ParkScene& scene)
    {
        DrawnAnimals& drawn = scene.drawn_animals;
        const auto& r = scene.sim.registry;
        drawn.placed.clear();
        std::vector<entt::entity> crated;
        std::vector<glm::mat4> crate_places;
        for (const auto [e, animal, pose] : r.view<const simulation::Animal, const simulation::Pose>().each())
        {
            const simulation::Pose at = simulation::between_steps(pose, scene.sim_time / simulation::kTickSeconds);
            const glm::mat4 world = glm::rotate(glm::translate(glm::mat4(1.0f), at.position), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
            const auto body = animal.job == simulation::AnimalJob::Crated || !drawn.library ? std::nullopt : drawn.library->body(animal.species, animal.adult, animal.male);
            if (!body)
            {
                crated.push_back(e);
                crate_places.push_back(world);
                continue;
            }
            const float offset = static_cast<float>(entt::to_entity(e)) * kMotionSpread;
            drawn.placed.push_back({ .body = &*body, .motion = motion_of(*body, animal, pose), .time = scene.park_clock + offset, .world = world });
        }
        if (crated != drawn.crated)
        {
            const save::PlacedObject crate = animal_crate();
            std::vector<save::PlacedObject> shapes(crated.size(), crate);
            for (std::size_t i = 0; i < shapes.size(); ++i)
                shapes[i].id = i + 1;
            drawn.crates = scene.models.contains(park::model_key(crate)) ? park::moving_meshes(shapes, scene.models, scene.grid) : park::MovingMeshes { };
            drawn.crated = std::move(crated);
        }
        park::move_meshes(drawn.crates, crate_places);
        // shortcut: every animal is posed on the frame's thread at its finest level, every frame; fine for the
        // few dozen a zoo holds, move to a worker and pick levels by distance as people do once parks hold more.
        drawn.mesh = park::animal_mesh(drawn.placed, drawn.runs);
    }

} // namespace rct3::scene
