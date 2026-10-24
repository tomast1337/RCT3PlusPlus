// SPDX-License-Identifier: GPL-3.0-or-later
//
// The water of the park's waterfalls. Each pours from the edge of a higher lake: it leaves the edge flowing
// level and falls ever faster, so seen from the side its sheet is a parabola, flat at the edge and reaching
// the lower lake where the park says it lands. Over the sheet lies a second sheet of streaks at its sides;
// where it lands, foam lies on the lower lake and froth stands up from it, both fading out at their edges. All the pictures scroll the way
// the water runs, so the mesh is built again for the time it shows.
#pragma once

#include "park/object_mesh.hpp"
#include "save/model/world.hpp"

#include <array>
#include <span>
#include <string_view>

namespace rct3::park
{

    /// The overlay holding the waterfalls' pictures: seen in the game's install.
    inline constexpr std::string_view kWaterfallFile = "Waterfall/Waterfall.common.ovl";

    /// One of the waterfalls' pictures: its texture's name in the overlay and its key among the scene's pictures.
    struct WaterfallPicture
    {
        std::string_view texture { };
        std::string_view key { };
    };

    /// The sheet, the streaks at its sides, the foam on the lower lake and the froth standing up from it, in the
    /// order the mesh's batches come in.
    // shortcut: the overlay's "B" variant of each picture is left out (drawn plainly it shows black), use it once
    // we know how it is meant to be laid over its partner.
    inline constexpr std::array<WaterfallPicture, 4> kWaterfallPictures { {
        { "Waterfall2", "rct3pp:waterfall-sheet" },
        { "WaterfallSides", "rct3pp:waterfall-sides" },
        { "WaterfallFloor", "rct3pp:waterfall-foam" },
        { "WaterfallFloorFroth", "rct3pp:waterfall-froth" },
    } };

    /// Where the water of `fall` is after running `along` (0 at the edge, 1 where it lands), halfway across.
    [[nodiscard]] glm::vec3 waterfall_point(const save::Waterfall& fall, float along);

    /// Builds the water of `falls` into `mesh`, its pictures scrolled for `seconds`; one see-through batch per
    /// picture of kWaterfallPictures that some fall uses. Keeps `mesh`'s memory.
    void waterfall_mesh(std::span<const save::Waterfall> falls, float seconds, ObjectMesh& mesh);

} // namespace rct3::park
