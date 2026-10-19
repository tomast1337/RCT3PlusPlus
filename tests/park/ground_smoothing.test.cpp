// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_smoothing.hpp"

#include <cmath>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    /// Flat at height 2, `size` tiles each way, with a spike at corner point (5, 5).
    save::TerrainGrid spiked(int size)
    {
        save::TerrainGrid g;
        g.width = size;
        g.height = size;
        g.cells.resize(static_cast<std::size_t>(size * size));
        for (save::TerrainCell& c : g.cells)
            c.corners = { 2, 2, 2, 2 };
        g.at(5, 5).corners[0] = g.at(4, 5).corners[1] = g.at(5, 4).corners[2] = g.at(4, 4).corners[3] = 6;
        return g;
    }

    const auto anywhere = [](int, int) { return true; };
} // namespace

TEST_CASE("averaging pulls a spike down, a little each pass, and only inside the reach")
{
    save::TerrainGrid g = spiked(10);
    const float moved = smooth_ground(g, GroundSmoothing::Average, { 5, 5 }, 2, 0.5f, 0, 1, anywhere);
    CHECK(moved > 0);
    CHECK(g.at(5, 5).corners[0] == doctest::Approx(4)); // Half the way to its neighbours' 2
    CHECK(g.at(4, 4).corners[3] == doctest::Approx(4)); // Shared
    save::TerrainGrid far = spiked(10);
    CHECK(smooth_ground(far, GroundSmoothing::Average, { 1, 1 }, 1, 0.5f, 0, 1, anywhere) == 0);
}

TEST_CASE("levelling goes to the target; whole units round; random stays within half a unit")
{
    save::TerrainGrid g = spiked(10);
    smooth_ground(g, GroundSmoothing::Level, { 5, 5 }, 3, 1, 3, 1, anywhere);
    CHECK(g.at(5, 5).corners[0] == doctest::Approx(3));
    CHECK(g.at(6, 6).corners[0] == doctest::Approx(3));

    save::TerrainGrid w = spiked(10);
    w.at(2, 2).corners = { 2.3f, 2.3f, 2.3f, 2.3f }; // Cliffs round it: held
    w.at(7, 7).corners[0] = w.at(6, 7).corners[1] = w.at(7, 6).corners[2] = w.at(6, 6).corners[3] = 2.4f;
    smooth_ground(w, GroundSmoothing::WholeUnits, { 7, 7 }, 1, 1, 0, 1, anywhere);
    CHECK(w.at(7, 7).corners[0] == doctest::Approx(2));

    save::TerrainGrid r = spiked(10);
    smooth_ground(r, GroundSmoothing::Random, { 2, 2 }, 2, 1, 0, 7, anywhere);
    bool changed = false;
    for (const save::TerrainCell& c : r.cells)
        for (const float h : c.corners)
        {
            CHECK((std::abs(h - 2) <= 0.5f || h == 6));
            changed = changed || (h != 2 && h != 6);
        }
    CHECK(changed);
}

TEST_CASE("joining edges takes cliffs away; squaring makes them, each tile flat at a whole unit")
{
    save::TerrainGrid g = spiked(10);
    g.at(5, 5).corners = { 6, 6, 6, 6 }; // A raised tile with cliffs round it
    smooth_ground(g, GroundSmoothing::JoinEdges, { 5.5f, 5.5f }, 2, 1, 0, 1, anywhere);
    CHECK(g.at(5, 5).corners[1] == doctest::Approx(g.at(6, 5).corners[0]));
    CHECK(g.at(5, 5).corners[3] == doctest::Approx(g.at(6, 6).corners[0]));

    save::TerrainGrid s = spiked(10);
    smooth_ground(s, GroundSmoothing::TileEdges, { 5, 5 }, 1, 1, 0, 1, anywhere);
    for (const float h : s.at(5, 5).corners)
        CHECK(h == doctest::Approx(3)); // The mean of 6, 2, 2, 2 to the nearest unit
    CHECK(s.at(4, 4).corners[0] == doctest::Approx(3));

    save::TerrainGrid held = spiked(10);
    smooth_ground(held, GroundSmoothing::TileEdges, { 5, 5 }, 1, 1, 0, 1, [](int, int) { return false; });
    CHECK(held.at(5, 5).corners[0] == 6);
}
