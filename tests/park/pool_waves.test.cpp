// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/pool_waves.hpp"

#include "park/terrain_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <numbers>

using namespace rct3;
using namespace rct3::park;

namespace
{

    /// A pool of 6 by 4 water squares ringed by paved squares, its corner at (0, 0), with a changing room on its
    /// left; squares 4 world units wide from the world's origin.
    PoolMap ringed()
    {
        PoolMap map;
        for (std::int32_t row = 0; row < 6; ++row)
            for (std::int32_t col = 0; col < 8; ++col)
            {
                const bool edge = col == 0 || row == 0 || col == 7 || row == 5;
                map.squares.push_back({ .col = col, .row = row, .kind = edge ? PoolKind::Tile : PoolKind::Water, .surface = 4, .depth = 2 });
            }
        map.rooms.push_back({ .col = -1, .row = 2, .turn = 1, .waves = { .height = 10, .frequency = 4, .on = 20, .every = 60 } });
        return map;
    }

    save::TerrainGrid grid()
    {
        return { .width = 16, .height = 16, .tile_size_x = 4, .tile_size_z = 4 };
    }

    /// The pointer's way through `at`, a second apart.
    std::vector<glm::vec3> drawn(std::span<const glm::vec2> at)
    {
        std::vector<glm::vec3> points;
        for (const glm::vec2 p : at)
            points.push_back({ p.x, p.y, static_cast<float>(points.size()) });
        return points;
    }

} // namespace

TEST_CASE("pool waves: a wave machine sends crests out from its wall, with its complex's settings")
{
    PoolMap map = ringed();
    // Against the left wall of the water square at (1, 2): turn 3 is the column before.
    map.extras.push_back({ .col = 1, .row = 2, .turn = 3, .fit = pool_extra_fit("WaveMachine UnderWaterWall") });
    map.extras.push_back({ .col = 3, .row = 0, .turn = 0, .fit = pool_extra_fit("CentralSolid Light") }); // Not a machine
    const std::vector<PoolWave> waves = machine_waves(map, { }, grid());
    REQUIRE(waves.size() == 1);
    CHECK(waves[0].kind == PoolWaveKind::Machine);
    CHECK(waves[0].from == glm::vec2 { 4, 10 });
    CHECK(waves[0].heading == glm::vec2 { 1, 0 });
    CHECK(waves[0].bounds == glm::vec4 { 4, 4, 28, 20 });
    CHECK(waves[0].radius == 2); // Half its square, along the wall
    CHECK(waves[0].settings.height == 10);
    CHECK(waves[0].settings.every == 60);
}

TEST_CASE("pool waves: the machines run for their time out of every period, and the crests go on out after")
{
    PoolMap map = ringed();
    map.extras.push_back({ .col = 1, .row = 2, .turn = 3, .fit = pool_extra_fit("WaveMachine UnderWaterWall") });
    const std::vector<PoolWave> waves = machine_waves(map, { }, grid());
    const auto largest = [&](glm::vec2 at, float from) {
        float most = 0;
        for (int i = 0; i < 100; ++i)
            most = std::max(most, std::abs(pool_wave_height(waves, at, from + static_cast<float>(i) * 0.05f)));
        return most;
    };
    const glm::vec2 near { 5, 10 };
    CHECK(largest(near, 8) > 0.05f);       // Running: 0 to 20 s of every 60
    CHECK(largest(near, 30) == 0.0f);      // Stopped, the last crests gone by
    CHECK(largest(near, 68) > 0.05f);      // Running again
    CHECK(largest({ 19, 10 }, 0) == 0.0f); // The first crests have not got this far yet
    CHECK(largest({ 17, 10 }, 24) > 0.0f); // Just after the machine stops, the last are still on their way
}

TEST_CASE("pool waves: a wave machine's front is its own square wide, widens a little and dies out on its way")
{
    PoolMap map = ringed();
    map.extras.push_back({ .col = 1, .row = 2, .turn = 3, .fit = pool_extra_fit("WaveMachine UnderWaterWall") });
    std::vector<PoolWave> waves = machine_waves(map, { }, grid());
    const auto largest = [&](glm::vec2 at) {
        float most = 0;
        for (int i = 0; i < 200; ++i)
            most = std::max(most, std::abs(pool_wave_height(waves, at, 6 + static_cast<float>(i) * 0.05f)));
        return most;
    };
    CHECK(largest({ 6, 10 }) > largest({ 14, 10 })); // Lower further out
    CHECK(largest({ 27, 10 }) == 0.0f);              // Gone before the far side
    CHECK(largest({ 4.5f, 13.5f }) == 0.0f);         // Beside the machine, off its square
    CHECK(largest({ 12, 13.5f }) > 0.0f);            // Further out, the front has widened to there
    CHECK(largest({ 6, 18 }) == 0.0f);               // Nothing along the rest of the wall
    // A second machine along the wall makes its own front there.
    map.extras.push_back({ .col = 1, .row = 4, .turn = 3, .fit = pool_extra_fit("WaveMachine UnderWaterWall") });
    waves = machine_waves(map, { }, grid());
    REQUIRE(waves.size() == 2);
    CHECK(waves[1].from == glm::vec2 { 4, 18 });
    CHECK(largest({ 6, 18 }) > 0.05f);
}

TEST_CASE("pool waves: a line drawn along a side of the grid sends a train of crests that way")
{
    const PoolMap map = ringed();
    const auto wave = drawn_wave(map, grid(), drawn(std::array { glm::vec2 { 6, 10 }, glm::vec2 { 7, 10.2f }, glm::vec2 { 8, 10.1f }, glm::vec2 { 9, 10 } }), 50);
    REQUIRE(wave);
    CHECK(wave->kind == PoolWaveKind::Line);
    CHECK(wave->heading == glm::vec2 { 1, 0 });
    CHECK(wave->start == 50);
    const std::array lone { *wave };
    // Two seconds on, the crests are some way along; behind them the water is flat again.
    CHECK(std::abs(pool_wave_height(lone, { 6 + kPoolWaveLook.line_speed * 2, 10 }, 52)) > 0.02f);
    CHECK(std::abs(pool_wave_height(lone, { 6, 10 }, 52)) < 1e-3f);
    std::vector<PoolWave> kept { *wave };
    drop_spent_waves(kept, 50 + kPoolWaveLook.line_life + 1);
    CHECK(kept.empty());
}

TEST_CASE("pool waves: lines drawn aslant, clicks and strokes off the water make no wave")
{
    const PoolMap map = ringed();
    CHECK_FALSE(drawn_wave(map, grid(), drawn(std::array { glm::vec2 { 6, 6 }, glm::vec2 { 7, 7 }, glm::vec2 { 8, 8 }, glm::vec2 { 9, 9 } }), 0));
    CHECK_FALSE(drawn_wave(map, grid(), drawn(std::array { glm::vec2 { 6, 10 }, glm::vec2 { 6.1f, 10 }, glm::vec2 { 6.2f, 10 } }), 0));
    CHECK_FALSE(drawn_wave(map, grid(), drawn(std::array { glm::vec2 { 1, 10 }, glm::vec2 { 2, 10 }, glm::vec2 { 3, 10 }, glm::vec2 { 6, 10 } }), 0)); // From the paving
}

TEST_CASE("pool waves: a circle drawn round makes a whirlpool turning the way it went")
{
    const PoolMap map = ringed();
    std::vector<glm::vec2> way;
    for (int i = 0; i <= 24; ++i)
    {
        const float a = static_cast<float>(i) * std::numbers::pi_v<float> / 8; // One and a half turns
        way.push_back(glm::vec2 { 14, 12 } + 2.0f * glm::vec2 { std::cos(a), std::sin(a) });
    }
    const auto wave = drawn_wave(map, grid(), drawn(way), 10);
    REQUIRE(wave);
    CHECK(wave->kind == PoolWaveKind::Whirlpool);
    CHECK(wave->from.x == doctest::Approx(14).epsilon(0.05));
    CHECK(wave->from.y == doctest::Approx(12).epsilon(0.05));
    CHECK(wave->radius == doctest::Approx(2).epsilon(0.05));
    CHECK(wave->spin == 1);
    std::ranges::reverse(way);
    CHECK(drawn_wave(map, grid(), drawn(way), 10)->spin == -1);
    const std::array lone { *wave };
    CHECK(pool_wave_height(lone, { 14, 12 }, 13) < -0.1f); // A dip in the middle
    CHECK(pool_wave_height(lone, { 14, 12 }, 10 + kPoolWaveLook.whirl_life + 1) == 0.0f);
}

TEST_CASE("pool waves: the renderer gets each wave's place, shape and timing")
{
    PoolWave w { .kind = PoolWaveKind::Machine, .from = { 1, 2 }, .heading = { 0, -1 }, .bounds = { 0, 0, 8, 8 }, .settings = { .height = 10, .frequency = 4, .on = 20, .every = 60 } };
    const PoolWaveTexels texels = pool_wave_texels(std::array { w });
    CHECK(texels[0] == glm::vec4 { 1, 2, 0, -1 });
    CHECK(texels[kMaxPoolWaves].x == 1);
    CHECK(texels[kMaxPoolWaves].y == doctest::Approx(kPoolWaveLook.crest * 10));
    CHECK(texels[2 * kMaxPoolWaves] == glm::vec4 { 0, 0, 8, 8 });
    CHECK(texels[3 * kMaxPoolWaves] == glm::vec4 { 4, 20, 60, 0 });
    CHECK(texels[1].x == 0); // No wave in the second place
}

TEST_CASE("pool waves: the surface they lift dies down at a wall and stays under the rim")
{
    const save::TerrainGrid g = grid();
    // Water on the right and below only: walls on the left and above, and the corner square up and to the left.
    const auto water = [](int dc, int dr) { return dc >= 0 && dr >= 0; };
    CHECK(pool_wall_room(g, { 0, 2 }, water) == 0);
    CHECK(pool_wall_room(g, { 1, 3 }, water) == doctest::Approx(1));
    CHECK(pool_wall_room(g, { 2, 2 }, [](int, int) { return true; }) == 4); // Water all round: a square at most
    CHECK(pool_wave_lift(1, 0) == 0);
    CHECK(pool_wave_lift(1, 0.1f) <= kPoolWaveLook.rim_room + kPoolWaveLook.rim_rise * 0.1f);
    CHECK(pool_wave_lift(0.1f, 4) == doctest::Approx(0.1f));
    CHECK(pool_wave_lift(-0.5f, 4) == doctest::Approx(-0.5f)); // Troughs are not held up
}
