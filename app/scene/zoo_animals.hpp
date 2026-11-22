// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's animals, drawn. One out of its crate is drawn with its species' own model, posed every frame in
// the motion that fits what it is doing: walking, eating or drinking at the troughs, asleep, or standing. One
// still in its crate, or of a species whose model cannot be read, is drawn as the crate, which a helicopter lowers
// from the sky when the animal was just bought.
#pragma once

#include "config/game/install.hpp"
#include "park/animal_mesh.hpp"
#include "park/moving_meshes.hpp"
#include "render/device.hpp"
#include "save/model/world.hpp"
#include "simulation/entt.hpp"
#include "ui/ui_textures.hpp"

#include <optional>
#include <vector>

namespace rct3::scene
{

    namespace park
    {
        struct ParkScene;
    }

    struct DrawnAnimals
    {
        std::optional<rct3::park::AnimalLibrary> library;
        std::vector<rct3::park::PlacedAnimal> placed; ///< Those drawn with their models, as last posed, then the bodies of killed animals; bodies point into `library`
        std::vector<entt::entity> placed_animals { }; ///< By placed, its animal
        rct3::park::AnimalRuns runs { };
        rct3::park::object::ObjectMesh mesh; ///< As last posed
        ui::UiTextures textures;
        rct3::park::MovingMeshes crates;      ///< The animals still in their crates, then the helicopters lowering them
        std::vector<entt::entity> crated { }; ///< By crate, its animal
        std::vector<entt::entity> flown { };  ///< By helicopter, its animal; none while the helicopter's model is not loaded
    };

    /// Finds the animals' models in `install`, to read as animals come.
    void load_zoo_animals(park::ParkScene& scene, const config::game::GameInstall& install);

    /// The crate animals come in, as an object to load the model of.
    [[nodiscard]] save::model::PlacedObject animal_crate();

    /// The helicopter that lowers a bought animal's crate, as an object to load the model of.
    [[nodiscard]] save::model::PlacedObject animal_helicopter();

    /// Reads the models of animals that came since and uploads their pictures, and the crate's; the crate's
    /// model must be loaded.
    void animals_changed(render::Device& device, park::ParkScene& scene);

    /// Poses the animals and their crates as they are now, at the park's clock, each animal at the level of detail
    /// its distance from `eye` calls for.
    void place_animals(park::ParkScene& scene, const glm::vec3& eye);

} // namespace rct3::scene
