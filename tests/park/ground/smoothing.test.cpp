// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/smoothing.hpp"

#include <cmath>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;
using namespace rct3::park::ground;

namespace
{
    /// Flat at height 2, `size` tiles each way, with a spike at corner point (5, 5).
    save::model::TerrainGrid spiked(int size)
    {
        save::model::TerrainGrid g;
        g.width = size;
        g.height = size;
        g.cells.resize(static_cast<std::size_t>(size * size));
        for (save::model::TerrainCell& c : g.cells)
            c.corners = { 2, 2, 2, 2 };
        g.at(5, 5).corners[0] = g.at(4, 5).corners[1] = g.at(5, 4).corners[2] = g.at(4, 4).corners[3] = 6;
        return g;
    }

    const auto anywhere = [](save::model::Tile) { return true; };
} // namespace

TEST_CASE("averaging pulls a spike down, a little each pass, and only inside the reach")
{
    save::model::TerrainGrid g = spiked(10);
    const float moved = smooth_ground(g, GroundSmoothing::Average, { 5, 5 }, 2, 0.5f, 0, 1, anywhere);
    CHECK(moved > 0);
    CHECK(g.at(5, 5).corners[0] == doctest::Approx(4)); // Half the way to its neighbours' 2
    CHECK(g.at(4, 4).corners[3] == doctest::Approx(4)); // Shared
    save::model::TerrainGrid far = spiked(10);
    CHECK(smooth_ground(far, GroundSmoothing::Average, { 1, 1 }, 1, 0.5f, 0, 1, anywhere) == 0);
}

TEST_CASE("levelling goes to the target, a pad to one whole unit; random stays within half a unit")
{
    save::model::TerrainGrid g = spiked(10);
    smooth_ground(g, GroundSmoothing::Level, { 5, 5 }, 3, 1, 3, 1, anywhere);
    CHECK(g.at(5, 5).corners[0] == doctest::Approx(3));
    CHECK(g.at(6, 6).corners[0] == doctest::Approx(3));

    // A pad: the whole brush goes to the one whole unit of the height it was levelled from, slopes and all.
    save::model::TerrainGrid w = spiked(10);
    for (int c = 0; c < 10; ++c)
        for (int r = 0; r < 10; ++r)
            w.at(c, r).corners = { c * 0.7f, (c + 1) * 0.7f, c * 0.7f, (c + 1) * 0.7f };
    CHECK(whole_level(3.4f) == 3);
    CHECK(whole_level(3.5f) == 4);
    CHECK(whole_level(-0.4f) == 0);
    smooth_ground(w, GroundSmoothing::Level, { 5, 5 }, 2, 1, whole_level(3.4f), 1, anywhere);
    for (int c = 4; c <= 5; ++c)
        for (const float h : w.at(c, 5).corners)
            CHECK(h == doctest::Approx(3));

    save::model::TerrainGrid r = spiked(10);
    smooth_ground(r, GroundSmoothing::Random, { 2, 2 }, 2, 1, 0, 7, anywhere);
    bool changed = false;
    for (const save::model::TerrainCell& c : r.cells)
        for (const float h : c.corners)
        {
            CHECK((std::abs(h - 2) <= 0.5f || h == 6));
            changed = changed || (h != 2 && h != 6);
        }
    CHECK(changed);
}

TEST_CASE("averaging and levelling move both sides of a cliff")
{
    save::model::TerrainGrid g = spiked(10);
    g.at(4, 5).corners[1] = g.at(5, 4).corners[2] = g.at(4, 4).corners[3] = 2;                         // No spike
    g.at(5, 5).corners = { 6, 6, 6, 6 };                                                               // A raised tile with cliffs four units high round it
    g.at(7, 5).corners[0] = g.at(6, 5).corners[1] = g.at(7, 4).corners[2] = g.at(6, 4).corners[3] = 4; // A bump at the cliff's foot
    smooth_ground(g, GroundSmoothing::Average, { 6, 5.5f }, 1.2f, 0.5f, 0, 1, anywhere);
    for (const float h : g.at(5, 5).corners)
        CHECK(h == doctest::Approx(6));                     // The flat top is its own ground: the lower ground beside it does not pull it
    CHECK(g.at(6, 5).corners[0] == doctest::Approx(2.25f)); // The foot follows the bump beside it, not the top
    CHECK(g.at(6, 5).corners[1] < 4);                       // The bump on the lower ground wears down
    CHECK(g.at(6, 5).corners[1] == doctest::Approx(g.at(7, 5).corners[0]));

    save::model::TerrainGrid l = spiked(10);
    l.at(5, 5).corners = { 6, 6, 6, 6 };
    smooth_ground(l, GroundSmoothing::Level, { 5.5f, 5.5f }, 2, 1, 3, 1, anywhere);
    CHECK(l.at(5, 5).corners[3] == doctest::Approx(3)); // Both sides reach the level
    CHECK(l.at(6, 6).corners[0] == doctest::Approx(3));
}

TEST_CASE("joining edges takes cliffs away; squaring makes them, each tile flat at a whole unit")
{
    save::model::TerrainGrid g = spiked(10);
    g.at(5, 5).corners = { 6, 6, 6, 6 }; // A raised tile with cliffs round it
    smooth_ground(g, GroundSmoothing::JoinEdges, { 5.5f, 5.5f }, 2, 1, 0, 1, anywhere);
    CHECK(g.at(5, 5).corners[1] == doctest::Approx(g.at(6, 5).corners[0]));
    CHECK(g.at(5, 5).corners[3] == doctest::Approx(g.at(6, 6).corners[0]));

    save::model::TerrainGrid s = spiked(10);
    smooth_ground(s, GroundSmoothing::TileEdges, { 5, 5 }, 1, 1, 0, 1, anywhere);
    for (const float h : s.at(5, 5).corners)
        CHECK(h == doctest::Approx(3)); // The mean of 6, 2, 2, 2 to the nearest unit
    CHECK(s.at(4, 4).corners[0] == doctest::Approx(3));

    // A weaker pass goes part of the way, and the cliff closes over several.
    save::model::TerrainGrid eased = spiked(10);
    eased.at(5, 5).corners = { 6, 6, 6, 6 };
    smooth_ground(eased, GroundSmoothing::JoinEdges, { 5.5f, 5.5f }, 2, 0.5f, 0, 1, anywhere);
    const float gap = eased.at(5, 5).corners[1] - eased.at(6, 5).corners[0];
    CHECK(gap > 0);
    CHECK(gap < 4);
    for (int pass = 0; pass < 20; ++pass)
        smooth_ground(eased, GroundSmoothing::JoinEdges, { 5.5f, 5.5f }, 2, 0.5f, 0, 1, anywhere);
    CHECK(eased.at(5, 5).corners[1] == doctest::Approx(eased.at(6, 5).corners[0]));

    save::model::TerrainGrid held = spiked(10);
    smooth_ground(held, GroundSmoothing::TileEdges, { 5, 5 }, 1, 1, 0, 1, [](save::model::Tile) { return false; });
    CHECK(held.at(5, 5).corners[0] == 6);
}
