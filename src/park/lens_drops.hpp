// SPDX-License-Identifier: GPL-3.0-or-later
//
// Water on the camera's lens: drops that land on it as the eye comes up out of water, rides through a splash or
// looks out into the rain, and then cling, slide and run off. Small drops hold on where they land; bigger ones,
// and any the air pushes hard enough, run down the lens the way down lies on screen, blown by the wind, thrown
// by the view turning and pushed out from the middle as the eye flies forward. Drops that touch run together.
// Thin sheets of water run off the whole lens at once after a soaking.
//
// Screen space: the screen's height is 1 and its width the aspect ratio, x to the right and y down. Looks only;
// nothing here is saved.
#pragma once

#include "random/generator.hpp"

#include <cstddef>
#include <glm/vec2.hpp>
#include <vector>

namespace rct3::park
{

    inline constexpr std::size_t kMostLensDrops = 96; ///< Drops on the lens at once
    inline constexpr std::size_t kMostLensSheets = 6; ///< Sheets of water at once
    inline constexpr std::size_t kSoakingDrops = 80;  ///< Drops a soaking leaves, coming up out of water
    inline constexpr std::size_t kSoakingSheets = 4;

    struct LensDrop
    {
        glm::vec2 at { };
        glm::vec2 velocity { }; ///< Screen heights a second
        float radius = 0;       ///< Screen heights
        float clinging = 0;     ///< Seconds it holds still before it may slide again
        float age = 0;          ///< Seconds on the lens
        float life = 0;         ///< Seconds it lasts before it has dried away
        bool sheet = false;     ///< A wide thin sheet of water rather than a drop
    };

    struct LensWater
    {
        std::vector<LensDrop> drops;
        random::RandomState dice = random::stream({ }, random::RandomStream::Effects);
    };

    /// What pushes the water on the lens this frame, in screen heights a second unless said.
    struct LensPush
    {
        float aspect = 1;        ///< The screen's width over its height
        glm::vec2 down { 0, 1 }; ///< Down in the world as seen on screen; shorter the more the view looks down
        glm::vec2 wind { };      ///< The wind across the screen
        glm::vec2 turn { };      ///< How fast the world sweeps across the screen as the view turns
        float onrush = 0;        ///< How fast the eye flies forward, in world units a second
    };

    /// Lands `drops` drops and `sheets` sheets at random over the lens, as many as there is room for.
    void wet_lens(LensWater& water, std::size_t drops, std::size_t sheets, float aspect);
    /// Moves the water on the lens on by `seconds`: drops slide, run together, dry and run off the screen.
    void run_lens_water(LensWater& water, const LensPush& push, float seconds);

} // namespace rct3::park
