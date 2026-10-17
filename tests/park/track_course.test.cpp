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
    // Kept 2 above the track with its wheels at 4 and 6 along, facing +x (its model faces +z).
    const glm::mat4 saved = glm::rotate(glm::translate(glm::mat4(1.0f), { 5, 2, 0 }), glm::radians(90.0f), { 0, 1, 0 });
    check_near(glm::vec3(park::moved_along(c, saved, { 6, 4 }, 0)[3]), { 5, 2, 0 });
    const glm::mat4 moved = park::moved_along(c, saved, { 6, 4 }, 10);
    check_near(glm::vec3(moved[3]), { 10, 2, 5 });
    check_near(glm::vec3(moved * glm::vec4(0, 0, 1, 0)), { 0, 0, 1 }); // Its front now faces along z
    check_near(glm::vec3(moved * glm::vec4(0, 1, 0, 0)), { 0, 1, 0 });
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
