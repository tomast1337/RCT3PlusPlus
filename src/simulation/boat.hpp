// SPDX-License-Identifier: GPL-3.0-or-later
//
// Boats that leave their dock for open water: rowing boats, swans, canoes and the like. A boat sails out over
// the water from tile to tile, picking somewhere ahead each time, and after a while turns for home and makes for
// its dock. It steers as a boat does: it turns only so fast, slows in a hard turn and near its last mark, and
// keeps to tiles that are wholly under water.
//
// Positions are the park's world x and z. Kept free of the registry so it can be tested on a made-up lake.
#pragma once

#include "math/glm.hpp"
#include "random/generator.hpp"
#include "save/model/world.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rct3::simulation
{

    /// Which tiles a boat can float on, and the surface's height there.
    struct WaterTiles
    {
        int cols = 0;
        int rows = 0;
        float origin_x = 0;
        float origin_z = 0;
        float tile_size = 4;
        std::vector<std::optional<float>> level { }; ///< Row-major; none where a boat cannot go

        [[nodiscard]] std::optional<float> at(int col, int row) const;
        [[nodiscard]] glm::ivec2 tile_of(glm::vec2 at) const;
        [[nodiscard]] glm::vec2 middle(glm::ivec2 tile) const;
    };

    /// The tiles of `grid` that `bodies` cover wholly, both triangles under the surface.
    [[nodiscard]] WaterTiles water_tiles(const save::TerrainGrid& grid, std::span<const save::WaterBody> bodies);

    /// The tiles from `from` to `to` over water, each beside the last (corners only where both sides are water
    /// too), `to` last and `from` left out. None when no way joins them.
    [[nodiscard]] std::optional<std::vector<glm::ivec2>> water_route(const WaterTiles& water, glm::ivec2 from, glm::ivec2 to);

    enum class Sailing : std::uint8_t
    {
        Out,    ///< Roaming the water
        Home,   ///< Making for the dock
        Docked, ///< Back, its riders getting off
    };

    struct Boat
    {
        glm::vec2 at { };
        glm::vec2 heading { 0, 1 }; ///< Unit
        float speed = 0;            ///< m/s
        Sailing state = Sailing::Docked;
        std::vector<glm::vec2> marks { }; ///< Still to pass, nearest first
        std::vector<glm::vec2> calls { }; ///< Heading home: the docks still to make for, its own last
        float out = 0;                    ///< Seconds since it left the dock
        float on_mark = 0;                ///< Seconds spent making for the nearest mark
    };

    /// Where a boat leaves its dock and where it comes back in.
    struct Dock
    {
        glm::vec2 leave { };
        glm::vec2 heading { 0, 1 }; ///< Out over the water from `leave`
        glm::vec2 enter { };
    };

    /// Sends a docked boat out from `dock`: to roam, or else straight past each of `calls` in turn and home.
    void cast_off(Boat& boat, const Dock& dock, bool roams = true, std::span<const glm::vec2> calls = { });

    /// Sails `boat` for `seconds` at up to `top` m/s; after `roam` seconds out it heads home. True once it docks.
    bool sail(Boat& boat, const WaterTiles& water, const Dock& dock, float top, float roam, float seconds, random::RandomState& dice);

} // namespace rct3::simulation
