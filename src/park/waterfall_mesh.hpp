// SPDX-License-Identifier: GPL-3.0-or-later
//
// The water of the park's waterfalls. Each pours from the edge of a higher lake: it leaves the edge flowing
// level and falls ever faster, so seen from the side its sheet is a parabola, flat at the edge and reaching
// the lower lake where the park says it lands. Where it lands, foam lies on the lower lake, and a tall fall
// raises mist there.
//
// The mesh holds no time: the waterfall shader (app/shaders/waterfall.slang) runs the water from the park's
// clock, so the mesh is built again only when the falls, their lakes or the Waterfall detail option change.
#pragma once

#include "park/terrain_mesh.hpp"
#include "save/model/world.hpp"
#include "units/units.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace rct3::park
{

    /// How much of a waterfall's look is drawn: the Waterfall detail option, as its value counts.
    enum class WaterfallDetail : std::uint8_t
    {
        Low,    ///< Sheet and foam, the fewest reads
        Medium, ///< With the lip's glint, rings in the foam, the sun's shadow and one row of mist
        High,   ///< With torn gaps in the sheet, wisps and a second row of mist
        Ultra,  ///< With droplets thrown up, ripples round the foam, the scene bent through the gaps and shadowed mist
    };

    /// The parts of a waterfall, each one draw for all the falls, in the order they are drawn.
    enum class FallPart : std::uint8_t
    {
        Sheet, ///< The falling water
        Foam,  ///< Lying on the lower lake, over the foot of the sheet
        Mist,  ///< Puffs standing where a tall fall lands, and at Ultra the droplets thrown up there
    };

    /// The colours of the lakes a waterfall joins, as the lakes' own vertices carry them.
    struct FallLakes
    {
        glm::vec4 upper { 1.0f };
        glm::vec4 lower { 1.0f };
    };

    /// Where in the mesh's indices a part lies.
    struct FallBatch
    {
        std::uint32_t first = 0;
        std::uint32_t count = 0;
    };

    /// The waterfalls' water. Vertices, by part:
    ///
    /// - Sheet: uv = units across from its middle, seconds since the water left the edge; normal = half its width,
    ///   seconds the whole fall takes, the drop; colour = the lakes' colour, alpha 0 where it starts behind the edge.
    /// - Foam: uv = units across from the middle of where it lands, units out from there; normal = half the width,
    ///   the drop, how far the foam reaches; colour = the lower lake's.
    /// - Mist: position = the middle of a puff, or where a droplet is thrown from; uv = its corner, -1 to 1. A puff's
    ///   normal is its size, a number of its own (0 to 1) and how thick it is (above 0); colour the lower lake's. A
    ///   droplet's normal is its size, its own number and -1; its colour the speed it is thrown at, alpha 1.
    struct WaterfallMesh
    {
        std::vector<GroundVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::array<FallBatch, 3> parts { }; ///< By FallPart
    };

    /// Where the water of `fall` is after running `along` (0 at the edge, 1 where it lands), halfway across.
    [[nodiscard]] glm::vec3 waterfall_point(const save::model::Waterfall& fall, float along);

    /// How long water takes to fall `drop`, from rest.
    [[nodiscard]] units::Seconds fall_seconds(units::Distance drop);

    /// Builds the water of `falls` at `detail` into `mesh`; `lakes` holds each fall's lakes' colours. A fall over
    /// an edge another earlier one already pours over keeps only the part the other leaves, so the two do not
    /// double. Keeps `mesh`'s memory.
    void waterfall_mesh(std::span<const save::model::Waterfall> falls, std::span<const FallLakes> lakes, WaterfallDetail detail, WaterfallMesh& mesh);

} // namespace rct3::park
