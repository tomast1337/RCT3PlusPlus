// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_route.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid owned_grid(int width, int height)
    {
        save::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        for (save::TerrainCell& c : g.cells)
            c.flags = static_cast<std::uint8_t>(save::TileFlag::Owned);
        return g;
    }

    save::TerrainCell& cell(save::TerrainGrid& g, int col, int row)
    {
        return g.cells[static_cast<std::size_t>(row * g.width + col)];
    }

    /// A ridge across every row at column `col`, `height` high on its flat top.
    void ridge(save::TerrainGrid& g, int col, float height)
    {
        for (int row = 0; row < g.height; ++row)
            cell(g, col, row).corners = { height, height, height, height };
    }

    FootpathBuilder started_at(PathSpot spot, bool cut = false)
    {
        FootpathBuilder b;
        start_at(b, spot);
        b.cut_into_ground = cut;
        return b;
    }

    bool joined(const std::vector<RouteStep>& route)
    {
        for (std::size_t i = 1; i < route.size(); ++i)
        {
            const PathSpot a = route[i - 1].piece.spot;
            const PathSpot b = route[i].piece.spot;
            if (std::abs(a.col - b.col) + std::abs(a.row - b.row) != 1 || route[i - 1].piece.end != route[i].piece.start)
                return false;
        }
        return true;
    }

    /// No two pieces of `route` on one tile come within kPathHeadroom of each other.
    bool keeps_room(const std::vector<RouteStep>& route)
    {
        for (std::size_t i = 0; i < route.size(); ++i)
        {
            for (std::size_t j = i + 1; j < route.size(); ++j)
            {
                const NextPiece& a = route[i].piece;
                const NextPiece& b = route[j].piece;
                if (a.spot == b.spot && std::min(a.start, a.end) < std::max(b.start, b.end) + kPathHeadroom && std::min(b.start, b.end) < std::max(a.start, a.end) + kPathHeadroom)
                    return false;
            }
        }
        return true;
    }
} // namespace

TEST_CASE("a route over flat ground lies on the ground, short and with one turn")
{
    const save::TerrainGrid g = owned_grid(10, 10);
    const auto route = find_route(started_at({ 1, 1 }), { 6, 4 }, g, { }, 1000);
    REQUIRE(route);
    CHECK(route->size() == 9); // The start tile and eight more
    CHECK(route->front().piece.spot == PathSpot { 1, 1 });
    CHECK(route->back().piece.spot == PathSpot { 6, 4 });
    CHECK(joined(*route));
    CHECK(std::ranges::all_of(*route, [](const RouteStep& s) { return s.piece.ground; }));
    std::size_t turns = 0;
    for (std::size_t i = 2; i < route->size(); ++i)
        turns += (*route)[i].heading != (*route)[i - 1].heading;
    CHECK(turns == 1);
    CHECK(route_price(*route, started_at({ 1, 1 }), g, { }, 1000) == 9000);
}

TEST_CASE("a route goes round land the park does not own")
{
    save::TerrainGrid g = owned_grid(8, 6);
    for (int row = 0; row < 5; ++row)
        cell(g, 4, row).flags = 0;
    const auto route = find_route(started_at({ 1, 0 }), { 7, 0 }, g, { }, 1000);
    REQUIRE(route);
    CHECK(joined(*route));
    CHECK(std::ranges::any_of(*route, [](const RouteStep& s) { return s.piece.spot == PathSpot { 4, 5 }; }));
}

TEST_CASE("across a ridge a route climbs over it, or cuts through when cutting is on")
{
    save::TerrainGrid g = owned_grid(12, 3);
    ridge(g, 5, 2);
    ridge(g, 6, 2);

    const auto over = find_route(started_at({ 1, 1 }), { 10, 1 }, g, { }, 1000);
    REQUIRE(over);
    CHECK(joined(*over));
    CHECK(std::ranges::any_of(*over, [](const RouteStep& s) { return s.slope != PathSlope::Flat; }));
    CHECK(std::ranges::none_of(*over, [](const RouteStep& s) { return s.piece.cut; }));
    CHECK(over->back().piece.ground);

    const auto through = find_route(started_at({ 1, 1 }, true), { 10, 1 }, g, { }, 1000);
    REQUIRE(through);
    CHECK(joined(*through));
    CHECK(std::ranges::any_of(*through, [](const RouteStep& s) { return s.piece.cut; }));
    const auto sloping = [](const std::vector<RouteStep>& route) { return std::ranges::count_if(route, [](const RouteStep& s) { return s.slope != PathSlope::Flat; }); };
    CHECK(sloping(*through) < sloping(*over));
}

TEST_CASE("under a mountain a route tunnels rather than climb over or cut a trench")
{
    save::TerrainGrid g = owned_grid(14, 3);
    for (int col = 4; col <= 9; ++col)
        ridge(g, col, 12);
    for (const bool cut : { false, true })
    {
        const auto route = find_route(started_at({ 1, 1 }, cut), { 12, 1 }, g, { }, 1000);
        REQUIRE(route);
        CHECK(joined(*route));
        CHECK(std::ranges::count_if(*route, [](const RouteStep& s) { return s.piece.tunnel; }) == 6);
        CHECK(std::ranges::none_of(*route, [](const RouteStep& s) { return s.piece.cut; }));
    }
}

TEST_CASE("a route to a raised path ends on it, not on the ground under it")
{
    const save::TerrainGrid g = owned_grid(10, 3);
    save::PathTile raised = ground_path({ 7, 1 }, "Old", 9);
    raised.kind = save::PathKind::Flying;
    raised.elevation = save::PathElevation { };
    raised.elevation->quantised_height = 4;
    const std::vector<save::PathTile> paths { raised };
    const auto route = find_route(started_at({ 1, 1 }), { 7, 1 }, g, paths, 1000, 4);
    REQUIRE(route);
    CHECK(joined(*route));
    CHECK(route->back().piece.spot == PathSpot { 7, 1 });
    CHECK(route->back().piece.end == 4);
    CHECK_FALSE(route->back().piece.ground);
    CHECK(std::ranges::any_of(*route, [](const RouteStep& s) { return s.slope != PathSlope::Flat; }));
    // Without the height, the ground under it will do.
    const auto ground = find_route(started_at({ 1, 1 }), { 7, 1 }, g, paths, 1000);
    REQUIRE(ground);
    CHECK(ground->back().piece.end == 0);
}

TEST_CASE("a route goes on from the trail's end and walks over paths already there")
{
    const save::TerrainGrid g = owned_grid(8, 3);
    FootpathBuilder b = started_at({ 0, 1 });
    b.heading = 1;
    b.trail.push_back({ { 0, 1 }, 7, 0 });
    const std::vector<save::PathTile> paths { ground_path({ 0, 1 }, "Old", 7), ground_path({ 3, 1 }, "Old", 8) };
    const auto route = find_route(b, { 5, 1 }, g, paths, 1000);
    REQUIRE(route);
    CHECK(route->size() == 5);
    CHECK(route->front().piece.spot == PathSpot { 1, 1 });
    CHECK(route_price(*route, b, g, paths, 1000) == 4000); // The path at column 3 is walked over
    CHECK(find_route(b, { 0, 1 }, g, paths, 1000)->empty());
    CHECK_FALSE(find_route(FootpathBuilder { }, { 5, 1 }, g, paths, 1000));
    CHECK_FALSE(find_route(b, { 9, 1 }, g, paths, 1000)); // Off the map
}

TEST_CASE("a route climbing round to a path over its own start keeps room over its own pieces")
{
    const save::TerrainGrid g = owned_grid(5, 3);
    for (const std::int32_t height : { 1, 2, 3, 4 })
    {
        save::PathTile raised = ground_path({ 2, 1 }, "Old", 9);
        raised.kind = save::PathKind::Flying;
        raised.elevation = save::PathElevation { };
        raised.elevation->quantised_height = height;
        const std::vector<save::PathTile> paths { raised };
        const auto route = find_route(started_at({ 1, 1 }), { 2, 1 }, g, paths, 1000, height);
        if (!route)
            continue;
        CHECK(joined(*route));
        CHECK(keeps_room(*route));
    }
}

TEST_CASE("a queue route bridging a path leaves room to walk under it")
{
    const save::TerrainGrid g = owned_grid(11, 3);
    std::vector<save::PathTile> paths;
    for (int row = 0; row < 3; ++row)
        paths.push_back(ground_path({ 5, row }, "Old", static_cast<std::uint64_t>(row + 1)));
    FootpathBuilder queue = started_at({ 1, 1 });
    queue.queue = true;
    const auto route = find_route(queue, { 9, 1 }, g, paths, 1000);
    REQUIRE(route);
    CHECK(joined(*route));
    const auto over = std::ranges::find_if(*route, [](const RouteStep& s) { return s.piece.spot.col == 5; });
    REQUIRE(over != route->end());
    CHECK(std::min(over->piece.start, over->piece.end) >= 3); // Higher than one steep ramp climbs
}
