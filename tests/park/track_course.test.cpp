// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_course.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3;

namespace
{
    /// 10 along x, then 10 along z.
    park::TrackCourse corner()
    {
        park::TrackCourse c;
        c.points = { { 0, 0, 0 }, { 10, 0, 0 }, { 10, 0, 10 } };
        c.distance = { 0, 10, 20 };
        c.kinds = { park::CourseKind::Plain, park::CourseKind::Plain, park::CourseKind::Plain };
        return c;
    }

    void check_near(const glm::vec3& a, const glm::vec3& b)
    {
        CHECK(a.x == doctest::Approx(b.x));
        CHECK(a.y == doctest::Approx(b.y));
        CHECK(a.z == doctest::Approx(b.z));
    }
} // namespace

TEST_CASE("a course finds points along its length, and round again when closed")
{
    park::TrackCourse c = corner();
    check_near(park::course_point(c, 15), { 10, 0, 5 });
    check_near(park::course_direction(c, 15), { 0, 0, 1 });
    check_near(park::course_point(c, 25), { 10, 0, 10 }); // Held at the end of an open course
    c.closed = true;
    check_near(park::course_point(c, 25), { 5, 0, 0 });
}

TEST_CASE("a car moved along a course keeps its place off the track and turns with it")
{
    const park::TrackCourse c = corner();
    // Kept 2 above the track with its wheels at 4 and 6 along, facing +x (its model faces +z).
    const glm::mat4 saved = glm::rotate(glm::translate(glm::mat4(1.0f), { 5, 2, 0 }), glm::radians(90.0f), { 0, 1, 0 });
    check_near(glm::vec3(park::moved_along(c, saved, { 6, 4 }, 0)[3]), { 5, 2, 0 });
    const glm::mat4 moved = park::moved_along(c, saved, { 6, 4 }, 10);
    check_near(glm::vec3(moved[3]), { 10, 2, 5 });
    check_near(glm::vec3(moved * glm::vec4(0, 0, 1, 0)), { 0, 0, 1 }); // Its front now faces along z
    check_near(glm::vec3(moved * glm::vec4(0, 1, 0, 0)), { 0, 1, 0 });
}

TEST_CASE("a train stands nose to tail back from its front, its wheels on the track")
{
    const park::TrackCourse c = corner();
    save::Train train;
    train.cars.resize(3);
    // A car 2 long, its wheels 0.5 and 1.5 behind its front and 1 below its middle; the last fits as the second.
    const std::array fits { park::CarFit { .length = 2, .front_wheels = 0.5f, .rear_wheels = 1.5f, .on_track = { 0, -1, 0 } }, park::CarFit { .length = 3, .front_wheels = 1, .rear_wheels = 2, .on_track = { 0, 0, -0.5f } } };
    park::stand_train(c, train, fits, 8);
    CHECK(train.distance == 8);
    CHECK(train.length == 8);
    const save::Car& first = train.cars[0];
    CHECK(first.distance == 8);
    CHECK(first.length == 2);
    CHECK(first.front_wheel_distance == doctest::Approx(7.5f));
    CHECK(first.rear_wheel_distance == doctest::Approx(6.5f));
    CHECK(first.position_valid);
    // Raised so the point between its wheels is on the track, which is at 7 along.
    check_near({ first.position[0], first.position[1], first.position[2] }, { 7, 1, 0 });
    // Turned from its model, which faces +z, to face +x along the track.
    const glm::vec3 axis { first.orientation[0], first.orientation[1], first.orientation[2] };
    check_near(glm::vec3(glm::rotate(glm::mat4(1.0f), glm::length(axis), glm::normalize(axis)) * glm::vec4(0, 0, 1, 0)), { 1, 0, 0 });
    // The second's front 2 behind, its wheels at 5 and 4 along, its point between them 0.5 behind its origin.
    CHECK(train.cars[1].distance == 6);
    check_near({ train.cars[1].position[0], train.cars[1].position[1], train.cars[1].position[2] }, { 5, 0, 0 });
    CHECK(train.cars[2].distance == 3);
}

TEST_CASE("a car points from its rear wheels to its front ones, and leans with the track")
{
    // The corner, then on along z, banked to the right from the corner on.
    park::TrackCourse c = corner();
    c.points.push_back({ 10, 0, 20 });
    c.distance.push_back(30);
    c.kinds.push_back(park::CourseKind::Plain);
    const glm::vec3 leaning = glm::normalize(glm::vec3 { 1, 1, 0 });
    c.ups = { { 0, 1, 0 }, { 0, 1, 0 }, leaning, leaning };
    check_near(park::course_up(c, 25), leaning);
    // Kept flat on the track with its wheels at 4 and 6 along, facing +x.
    const glm::mat4 saved = glm::rotate(glm::translate(glm::mat4(1.0f), { 5, 0, 0 }), glm::radians(90.0f), { 0, 1, 0 });
    // Straddling the corner, it stands on the line between its wheels rather than on the bend.
    const glm::mat4 straddling = park::moved_along(c, saved, { 6, 4 }, 5);
    check_near(glm::vec3(straddling[3]), { 9.5f, 0, 0.5f });
    check_near(glm::vec3(straddling * glm::vec4(0, 0, 1, 0)), glm::normalize(glm::vec3 { 1, 0, 1 }));
    // Both wheels on the banked stretch: it leans as the track does.
    const glm::mat4 banked = park::moved_along(c, saved, { 6, 4 }, 16);
    check_near(glm::vec3(banked[3]), { 10, 0, 11 });
    check_near(glm::vec3(banked * glm::vec4(0, 1, 0, 0)), leaning);
    check_near(glm::vec3(banked * glm::vec4(0, 0, 1, 0)), { 0, 0, 1 });
}

TEST_CASE("station runs are measured from one end of the station to the other, joined over a closed course's end")
{
    using K = park::CourseKind;
    park::TrackCourse c;
    c.points.resize(6);
    c.distance = { 0, 4, 10, 12, 15, 20 };
    c.kinds = { K::Station, K::Plain, K::Station, K::Station, K::Plain, K::Plain };
    CHECK(park::station_runs(c) == std::vector<float> { 4, 5 });
    c.closed = true;
    CHECK(park::station_runs(c) == std::vector<float> { 4, 5 });
    // The last stretch is a station too: on a closed course it runs on into the first.
    c.kinds = { K::Station, K::Plain, K::Plain, K::Plain, K::Station, K::Plain };
    CHECK(park::station_runs(c) == std::vector<float> { 9 });
    c.closed = false;
    CHECK(park::station_runs(c) == std::vector<float> { 4, 5 });
    CHECK(park::station_runs(corner()).empty());
}

TEST_CASE("an arm holds each piece's angles at its middle and turns evenly between them")
{
    park::TrackCourse c;
    c.distance = { 0, 40 };
    c.points = { { 0, 0, 0 }, { 40, 0, 0 } };
    c.piece_starts = { 0, 10, 20, 30 }; // Middles at 5, 15, 25 and 35
    c.closed = true;
    using A = park::ArmAngles;
    const std::vector<std::optional<A>> held { A { 0, 0, 0, 0 }, A { 100, 10, -20, 40 }, std::nullopt, A { 20, 0, 0, 0 } };
    CHECK(park::arm_angles_at(c, held, 15) == A { 100, 10, -20, 40 });
    CHECK(park::arm_angles_at(c, held, 10) == A { 50, 5, -10, 20 });
    // Beside a piece that holds none, the car takes the angles of the one that does.
    CHECK(park::arm_angles_at(c, held, 22) == A { 100, 10, -20, 40 });
    CHECK(park::arm_angles_at(c, held, 28) == A { 20, 0, 0, 0 });
    // Round the end of the circuit, from the last middle to the first.
    CHECK(park::arm_angles_at(c, held, 0) == A { 10, 0, 0, 0 });
    CHECK(park::arm_angles_at(c, held, 40) == A { 10, 0, 0, 0 });
    CHECK_FALSE(park::arm_angles_at(c, std::vector<std::optional<A>>(4), 12));
}
