// SPDX-License-Identifier: GPL-3.0-or-later
//
// Waves in the pools: those the wave machines make while they run, and those the player draws on the water with
// the pointer. Each is a wave over one body of water (its squares joined side to side at one surface), kept to the
// box round it; the water's surface and the swimmers floating on it follow them all. The shapes are ours:
//
// A wave machine sends a front of crests out from its own stretch of wall, one square wide, at a steady speed c:
//
//   height(d, t) = A * sin(w * (t - d / c)) * exp(-d / reach) * across,  d = distance out from the wall
//
// counted only where the water at d left the wall while the machine ran. The front widens a little as it goes,
// lower for it, and fades out at its sides (`across`); it dies away well before the far side of a big pool,
// and is gone by `cut`. Each machine makes its own. The machines of a complex run for `on` seconds out of every
// `every`, all in step, from the park clock's start.
//
// A line drawn on the water along a side of the grid sends a short train of crests the way it was drawn, across
// the whole body of water, dying down as it goes. A line drawn aslant does nothing. A circle drawn round and round
// turns into a whirlpool there a while: a dip in the water with arms that turn the way the circle was drawn.
//
// Where someone dives or drops into the water, rings spread out from there over its surface, a few crests
// round a ring that widens at a steady speed, falling as it ages and as it widens:
//
//   height(r, t) = A * exp(-t / fade) / (1 + c * t / spread) * exp(-(x / width)^2) * cos(2 pi x / length),  x = r - c * t
//
// on that water alone, not on water stacked over or under it.
//
// All of them die down near the walls of their water and stay under the rim there. The renderer is handed each wave (pool_wave_texels) and works
// out the same heights itself: it moves the surface up and down by them, so the swimmers floating there ride them
// as they show.
#pragma once

#include "math/glm.hpp"
#include "park/pools/pools.hpp"
#include "park/water/ripples.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rct3::park::pools
{

    /// The most waves the water shows at once, wave machines' and drawn together.
    inline constexpr std::size_t kMaxPoolWaves = water::kMaxRipples;

    /// How the waves look and behave. Lengths in world units (about a metre), times in seconds; all chosen by eye.
    struct PoolWaveLook
    {
        float crest = 0.05f;        ///< A wave machine's crest height for each step of its height setting
        float speed = 3.0f;         ///< Of a wave machine's crests, out from its wall
        float reach = 8.0f;         ///< A wave machine's crests fall to a third (1/e) over this distance
        float cut = 14.0f;          ///< And are gone this far from its wall
        float spread = 0.25f;       ///< Its front widens by this much each side for each unit out
        float front_soft = 0.75f;   ///< Over this far its front fades out at each side
        float ramp = 2.0f;          ///< Seconds a wave machine takes to start and to stop
        float line_height = 0.3f;   ///< Of a drawn line's crests at first
        float line_speed = 3.5f;    ///< Of a drawn line's crests
        float line_length = 2.5f;   ///< From one of a drawn line's crests to the next
        float line_width = 2.5f;    ///< Of a drawn line's train of crests
        float line_fade = 4.0f;     ///< Seconds a drawn line's crests take to fall to a third
        float line_life = 16.0f;    ///< Seconds before a drawn line's crests are gone
        float whirl_depth = 0.6f;   ///< Of a whirlpool's dip in the middle
        float whirl_life = 10.0f;   ///< Seconds a whirlpool lasts, its last few dying down
        float whirl_turn = 3.0f;    ///< Radians a second its arms turn
        float whirl_arms = 0.6f;    ///< How deep its arms cut into the dip, at its radius
        float whirl_wind = 3.0f;    ///< Radians its arms wind round for each radius out
        float edge = 1.0f;          ///< Waves die down over this far from their water's walls
        float rim_room = 0.08f;     ///< At a wall the water rises at most this far, under the rim (kPoolBrim below its top)
        float rim_rise = 0.3f;      ///< And for each world unit out from the wall, this much more
        float least_radius = 1.0f;  ///< Of a whirlpool, however small the circle drawn
        float most_radius = 4.0f;   ///< Of a whirlpool, however big the circle drawn
        float least_stroke = 1.5f;  ///< The shortest drawn line or circle that makes a wave, along the pointer's way
        float straight = 0.8f;      ///< A line runs at least this share of the way the pointer went, end to end
        float aslant = 0.3f;        ///< Radians a line may lean off a side of the grid and still make a wave
        float round = 0.4f;         ///< A circle's box is at least this share as wide as it is long
        float least_turning = 5.0f; ///< Radians the pointer turns round a circle, all one way, to make a whirlpool
        float ring_height = 0.14f;  ///< Of the rings where someone falls in at full strength, at first
        float ring_speed = 1.6f;    ///< Of the rings, outwards
        float ring_length = 0.7f;   ///< From one of a ring's crests to the next
        float ring_width = 0.6f;    ///< Of the train of crests round a ring
        float ring_fade = 2.0f;     ///< Seconds the rings take to fall to a third
        float ring_spread = 2.0f;   ///< The rings fall to half as they widen to this radius
        float ring_life = 5.0f;     ///< Seconds before the rings are gone
    };
    inline constexpr PoolWaveLook kPoolWaveLook { };

    enum class PoolWaveKind : std::uint8_t
    {
        None,
        Machine,   ///< A wave machine's, while the park runs
        Line,      ///< A line drawn on the water
        Whirlpool, ///< A circle drawn on the water
        Ring,      ///< Rings where someone fell into the water
    };

    struct PoolWave
    {
        PoolWaveKind kind = PoolWaveKind::None;
        glm::vec2 from { 0.0f };    ///< World x and z: a machine's wall, where a line began, or a whirlpool's middle
        glm::vec2 heading { 0.0f }; ///< The way its crests go, a side of the grid; none for a whirlpool
        glm::vec4 bounds { 0.0f };  ///< The box round its body of water: least x and z, most x and z
        float start = 0;            ///< The park clock when a line or a whirlpool was drawn
        float radius = 0;           ///< A whirlpool's; half a wave machine's front where it leaves the wall
        float spin = 0;             ///< A whirlpool's way round: 1 or -1
        float level = 0;            ///< Rings': their water's surface, world height
        float strength = 0;         ///< Rings': 0 to 1, how hard the fall into the water was
        PoolWaves settings { };     ///< A machine's complex's
    };

    /// The waves of the wave machines among `map`'s extras and `scenery` (extras parks from the game placed as
    /// scenery), each with the settings of the complex it stands in (its changing rooms'; the first one found).
    [[nodiscard]] std::vector<PoolWave> machine_waves(const PoolMap& map, std::span<const PoolExtra> scenery, const save::model::TerrainGrid& grid);

    /// What the pointer drew on the pool water at `points` (world x, world z, seconds), in the order it went there,
    /// made into a wave starting at `clock`: a line along a side of the grid or a circle. None for anything
    /// else, such as a click or a line drawn aslant, or when it did not start on a pool's water.
    [[nodiscard]] std::optional<PoolWave> drawn_wave(const PoolMap& map, const save::model::TerrainGrid& grid, std::span<const glm::vec3> points, float clock, const PoolWaveLook& look = kPoolWaveLook);

    /// Whether the water at world x and z `at` is a pool's.
    [[nodiscard]] bool on_pool_water(const PoolMap& map, const save::model::TerrainGrid& grid, glm::vec2 at);

    /// Rings spreading from world point `at`, where someone fell into the water at `clock` (or will then) with
    /// `strength` from 0 to 1. None unless `at` is over a pool's open water, past its coping, at about its surface.
    [[nodiscard]] std::optional<PoolWave> entry_rings(const PoolMap& map, const save::model::TerrainGrid& grid, glm::vec3 at, float strength, float clock);

    /// How high the leading crest of `rings` stands at `clock`: 0 before they start and once they are gone.
    [[nodiscard]] float ring_crest(const PoolWave& rings, float clock, const PoolWaveLook& look = kPoolWaveLook);

    /// Drops the drawn waves and rings that have died away by `clock`.
    void drop_spent_waves(std::vector<PoolWave>& waves, float clock, const PoolWaveLook& look = kPoolWaveLook);

    /// How far the water at world x and z `at` stands above its level at `clock`, from all `waves` together; given
    /// the water's world height `level`, rings on water stacked over or under it leave it be.
    [[nodiscard]] float pool_wave_height(std::span<const PoolWave> waves, glm::vec2 at, float clock, std::optional<float> level = std::nullopt, const PoolWaveLook& look = kPoolWaveLook);

    /// How far the surface rises for waves of `height`, `room` world units from its nearest wall (pool_wall_room):
    /// dying down near the wall and kept under its rim.
    [[nodiscard]] float pool_wave_lift(float height, float room, const PoolWaveLook& look = kPoolWaveLook);

    /// What the renderer reads, four rows of kMaxPoolWaves, after the ripples' two: each wave's `from` and
    /// heading; its kind, crest height (world units), start and spin (rings: their level); its bounds; its frequency, on and every
    /// seconds and radius. The first kMaxPoolWaves waves are kept.
    using PoolWaveTexels = std::array<glm::vec4, 4 * kMaxPoolWaves>;
    [[nodiscard]] PoolWaveTexels pool_wave_texels(std::span<const PoolWave> waves, const PoolWaveLook& look = kPoolWaveLook);

} // namespace rct3::park::pools
