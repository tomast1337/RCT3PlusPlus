// SPDX-License-Identifier: GPL-3.0-or-later
//
// The water of the park's waterfalls. Each pours from the edge of a higher lake: it leaves the edge flowing
// level and falls ever faster, so seen from the side its sheet is a parabola, flat at the edge and reaching
// the lower lake where the park says it lands. Over the sheet lies a second sheet of streaks at its sides;
// where it lands, foam lies on the lower lake and froth stands up from it, both fading out at their edges. All the pictures scroll the way
// the water runs, so the mesh is built again for the time it shows. Each is drawn over a second picture of its
// own, running at another speed (waterfall_layer_scroll), so the water shimmers rather than slides.
#pragma once

#include "park/object_mesh.hpp"
#include "save/model/world.hpp"

#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::park
{

    /// The overlay holding the waterfalls' pictures: seen in the game's install.
    inline constexpr std::string_view kWaterfallFile = "Waterfall/Waterfall.common.ovl";

    /// One of the waterfalls' pictures: its texture's name in the overlay and its key among the scene's pictures,
    /// and the same of the second picture drawn with it.
    struct WaterfallPicture
    {
        std::string_view texture { };
        std::string_view key { };
        std::string_view layer_texture { };
        std::string_view layer_key { };
        bool multiplied = false; ///< The second picture multiplies the first; otherwise it is added, as a glint
        float layer_lag = 0;     ///< Pictures a second the second picture runs behind the first.
    };

    /// The sheet, the streaks at its sides, the foam on the lower lake and the froth standing up from it, in the
    /// order the mesh's batches come in.
    inline constexpr std::array<WaterfallPicture, 4> kWaterfallPictures { {
        { "Waterfall2", "rct3pp:waterfall-sheet", "Waterfall2B", "rct3pp:waterfall-sheet-b", false, 0.3f },
        { "WaterfallSides", "rct3pp:waterfall-sides", "WaterfallSidesB", "rct3pp:waterfall-sides-b", false, 0.4f },
        { "WaterfallFloor", "rct3pp:waterfall-foam", "WaterfallFloorB", "rct3pp:waterfall-foam-b", true, 0.2f },
        { "WaterfallFloorFroth", "rct3pp:waterfall-froth", "WaterfallFloorFrothB", "rct3pp:waterfall-froth-b", false, -0.4f },
    } };

    /// How far, from 0 up to 1, the first picture of `picture` is moved along v past its second after `seconds`:
    /// the mesh's coordinates are where the second is read, and the draw moves the first on by this.
    [[nodiscard]] float waterfall_layer_scroll(const WaterfallPicture& picture, float seconds);

    /// Where the water of `fall` is after running `along` (0 at the edge, 1 where it lands), halfway across.
    [[nodiscard]] glm::vec3 waterfall_point(const save::Waterfall& fall, float along);

    /// The particle effect that sprays where the water lands: a file of the game's particle effects.
    inline constexpr std::string_view kWaterfallSpray = "WhiteWaterRapidsVapour";

    /// The spray of `falls`: kWaterfallSpray along the line where each one's water lands, one for every few units
    /// of its width, rising; `object` is the fall's index.
    [[nodiscard]] std::vector<PlacedEffect> waterfall_spray(std::span<const save::Waterfall> falls);

    /// Builds the water of `falls` into `mesh`, its pictures scrolled for `seconds`; one see-through batch per
    /// picture of kWaterfallPictures that some fall uses. Keeps `mesh`'s memory.
    void waterfall_mesh(std::span<const save::Waterfall> falls, float seconds, ObjectMesh& mesh);

} // namespace rct3::park
