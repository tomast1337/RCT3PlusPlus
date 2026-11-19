// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's wild birds, drawn: ducks on the water and in the air, gulls over the park, each posed every frame
// in the motion that fits what it is doing.
#pragma once

#include "config/game/install.hpp"
#include "park/wild_bird_models.hpp"
#include "render/device.hpp"
#include "simulation/wild_birds.hpp"
#include "ui/ui_textures.hpp"

#include <optional>

namespace rct3::scene
{

    namespace park
    {
        struct ParkScene;
    }

    struct DrawnBirds
    {
        std::optional<rct3::park::BirdModel> duck; ///< None where the install has no duck model
        std::optional<rct3::park::BirdModel> gull;
        std::vector<rct3::park::PlacedBird> placed;
        rct3::park::object::ObjectMesh mesh; ///< As last posed
        ui::UiTextures textures;
    };

    /// Reads the birds' models from `install` and uploads their pictures.
    void load_wild_birds(render::Device& device, park::ParkScene& scene, const config::game::GameInstall& install);

    /// Poses the birds as they are now, at the park's clock.
    void place_wild_birds(park::ParkScene& scene);

    /// What the birds fly and swim over in `scene`.
    [[nodiscard]] simulation::BirdGround bird_ground(const park::ParkScene& scene);

} // namespace rct3::scene
