// SPDX-License-Identifier: GPL-3.0-or-later
//
// Boats that leave their dock for open water: rowing boats, swans, canoes and the like. A boat sails out over
// the water from tile to tile, picking somewhere ahead each time, and after a while turns for home and makes for
// its dock. It steers as its kind of boat does (ovl::BoatHandling): it turns only so fast, keeps its speed only
// while its mark lies ahead, slows near its last mark and where the water ahead runs out, leans into its turns,
// and keeps to tiles that are wholly under water.
//
// Positions are the park's world x and z. Kept free of the registry so it can be tested on a made-up lake.
#pragma once

#include "math/glm.hpp"
#include "ovl/ride_train.hpp"
#include "park/path_clearance.hpp"
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

    /// The tiles of `grid` that `bodies` cover wholly, both triangles under the surface, but for those where
    /// something of `solids` stands in the water or low over it.
    [[nodiscard]] WaterTiles water_tiles(const save::TerrainGrid& grid, std::span<const save::WaterBody> bodies, const park::SolidTiles& solids = { });

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
        float turning = 0;                ///< Radians a second it turns, positive from +x towards +y
        float lean = 0;                   ///< Radians it rolls towards the inside of its turn
        glm::vec2 was { };                ///< Where it was when the last sail began, to draw it between steps
        glm::vec2 was_heading { 0, 1 };
        float was_lean = 0;
    };

    /// The handling most boats the game ships share.
    inline constexpr ovl::BoatHandling kMostBoats { .turn_most = 1, .turn_gain = 1, .speed_up = 1, .speed_gain = 0.1f, .turn_slow = 10, .brake = 3, .lean_gain = 0.4f, .lean_most = 0.2f };

    /// Where `boat` is drawn `along` (0..1) of the way through the step after its last sail: place, heading
    /// and lean.
    struct BoatPose
    {
        glm::vec2 at { };
        glm::vec2 heading { 0, 1 };
        float lean = 0;
    };
    [[nodiscard]] BoatPose between_sails(const Boat& boat, float along);

    /// Where a boat leaves its dock and where it comes back in.
    struct Dock
    {
        glm::vec2 leave { };
        glm::vec2 heading { 0, 1 }; ///< Out over the water from `leave`
        glm::vec2 enter { };
    };

    /// Sends a docked boat out from `dock`: to roam, or else straight past each of `calls` in turn and home.
    void cast_off(Boat& boat, const Dock& dock, bool roams = true, std::span<const glm::vec2> calls = { });

    /// Sails `boat` for `seconds`, handling as `handling` says, at up to `top` m/s; after `roam` seconds out it
    /// heads home. It keeps clear of the others of `fleet` (which may hold `boat` itself), or, a boat that
    /// `bumps`, bounces off them. True once it docks.
    bool sail(Boat& boat, const WaterTiles& water, const Dock& dock, const ovl::BoatHandling& handling, float top, float roam, float seconds, random::RandomState& dice, std::span<const Boat> fleet = { }, bool bumps = false);

} // namespace rct3::simulation
