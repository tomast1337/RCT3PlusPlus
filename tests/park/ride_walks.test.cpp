// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ride_walks.hpp"

#include <doctest/doctest.h>

using namespace rct3::park;

namespace
{
    bool near(const glm::vec3& a, const glm::vec3& b)
    {
        return glm::distance(a, b) < 1e-4f;
    }

    /// A building at `origin` facing +x into the ride.
    glm::mat4 building(glm::vec3 origin)
    {
        return glm::translate(glm::mat4(1.0f), origin);
    }
} // namespace

TEST_CASE("flat ride riders go round the loop the shorter way to their seat's path, and out by the exit")
{
    // A square loop round the middle, corners counted anticlockwise seen from above.
    Walkways ways;
    ways.loop = { { 4, 0, -4 }, { 4, 0, 4 }, { -4, 0, 4 }, { -4, 0, -4 } };
    ways.paths = { { { 4, 0, 4 }, { 2, 0.5f, 2 } }, { { -4, 0, -4 }, { -2, 0.5f, -2 } } };
    const std::vector<glm::vec3> seats { { 1.5f, 1, 1.5f }, { -1.5f, 1, -1.5f } };
    // In from the east, out to the west.
    const glm::mat4 entrance = glm::rotate(building({ 7, 0, -3 }), glm::pi<float>(), glm::vec3(0, 1, 0));
    const glm::mat4 exit = building({ -7, 0, 3 });
    const RideWays w = flat_ride_ways(ways, seats, entrance, exit);
    REQUIRE(w.on.size() == 2);
    REQUIRE(w.off.size() == 2);

    // Through the entrance to its tile's edge, then from the loop's nearest corner to the seat's path.
    CHECK(near(w.on[0].front(), { 5, 0, -3 }));
    CHECK(near(w.on[0][1], { 4, 0, -4 }));
    CHECK(near(w.on[0][2], { 4, 0, 4 }));
    CHECK(near(w.on[0].back(), { 2, 0.5f, 2 }));
    // The other seat's path is one corner round the other way.
    CHECK(w.on[1].size() == 4);
    CHECK(near(w.on[1][1], { 4, 0, -4 }));
    CHECK(near(w.on[1][2], { -4, 0, -4 }));
    // Off: back down the path, round to the corner nearest the exit, through the exit and out.
    CHECK(near(w.off[0].front(), { 2, 0.5f, 2 }));
    CHECK(near(w.off[0][1], { 4, 0, 4 }));
    CHECK(near(w.off[0][2], { -4, 0, 4 }));
    CHECK(near(w.off[0][3], { -5, 0, 3 }));
    CHECK(near(w.off[0].back(), { -9, 0, 3 }));
}

TEST_CASE("a flat ride without walkways is walked straight across")
{
    const std::vector<glm::vec3> seats { { 0, 1, 0 } };
    const RideWays w = flat_ride_ways({ }, seats, building({ -6, 0, 0 }), building({ -6, 0, 4 }));
    CHECK(w.on[0] == std::vector<glm::vec3> { { -4, 0, 0 }, { 0, 1, 0 } });
    CHECK(w.off[0] == std::vector<glm::vec3> { { 0, 1, 0 }, { -4, 0, 4 }, { -8, 0, 4 } });
}

TEST_CASE("tracked ride riders climb to the platform edge and walk along it to beside their seat")
{
    const Platform platform { .height = 1, .offset = 0.5f, .step_depth = 0.25f, .step_start = 1, .inside = -0.5f };
    const std::vector<glm::vec3> seats { { 3.5f, 1.4f, 6 } };
    const RideWays w = track_ride_ways(platform, seats, building({ 0, 0, 0 }), building({ 0, 0, 12 }));
    CHECK(w.on[0] == std::vector<glm::vec3> { { -0.5f, 0, 0 }, { 0.75f, 0, 0 }, { 2.25f, 1, 0 }, { 2.5f, 1, 0 }, { 2.5f, 1, 6 } });
    CHECK(w.off[0] == std::vector<glm::vec3> { { 2.5f, 1, 6 }, { 2.5f, 1, 12 }, { 2.25f, 1, 12 }, { 0.75f, 0, 12 }, { -0.5f, 0, 12 }, { -2, 0, 12 } });

    // Boarding from the top of the step, riders do not walk along.
    const RideWays top = track_ride_ways({ .height = 1, .walk_along = false }, seats, building({ 0, 0, 0 }), building({ 0, 0, 12 }));
    CHECK(top.on[0].back() == glm::vec3 { 1.75f, 1, 0 });
    CHECK(top.off[0].front() == glm::vec3 { 1.75f, 1, 12 });
}

TEST_CASE("the ticket spot faces along the entrance spot's own z axis")
{
    RideWays w;
    const glm::mat4 entrance = glm::rotate(building({ 10, 0, 20 }), glm::half_pi<float>(), glm::vec3(0, 1, 0));
    set_ticket_spot(w, entrance, glm::translate(glm::mat4(1.0f), glm::vec3(-1, 0, -0.5f)));
    CHECK(near(w.ticket, { 9.5f, 0, 21 }));
    CHECK(w.ticket_heading == doctest::Approx(glm::half_pi<float>()));
}
