// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_course.hpp"

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
    // Kept 2 above the track at 5 along, facing +x.
    const glm::mat4 saved = glm::translate(glm::mat4(1.0f), { 5, 2, 0 });
    const glm::mat4 moved = park::moved_along(c, saved, 5, 15);
    check_near(glm::vec3(moved[3]), { 10, 2, 5 });
    check_near(glm::vec3(moved * glm::vec4(1, 0, 0, 0)), { 0, 0, 1 }); // Its front now faces along z
    check_near(glm::vec3(moved * glm::vec4(0, 1, 0, 0)), { 0, 1, 0 });
}
