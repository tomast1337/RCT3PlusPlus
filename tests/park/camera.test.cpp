// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/camera.hpp"

#include <cmath>
#include <doctest/doctest.h>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid slope(int size)
    {
        save::TerrainGrid g;
        g.width = size;
        g.height = size;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(size * size));
        for (int row = 0; row < size; ++row)
        {
            for (int col = 0; col < size; ++col)
            {
                // Rising one unit a tile towards +X.
                const auto x0 = static_cast<float>(col);
                g.cells[static_cast<std::size_t>(row * size + col)].corners = { x0, x0 + 1, x0, x0 + 1 };
            }
        }
        return g;
    }
} // namespace

TEST_CASE("the eye sits back from and above the target, which lands mid-view")
{
    ParkCamera c;
    c.target = { 10, 2, 10 };
    const glm::vec3 e = eye(c);
    CHECK(e.y > c.target.y);
    CHECK(e.z < c.target.z); // Yaw 0 looks along +Z
    const glm::vec4 clip = view_projection(c, 1.5f) * glm::vec4(c.target, 1);
    CHECK(clip.x / clip.w == doctest::Approx(0).epsilon(0.001));
    CHECK(clip.y / clip.w == doctest::Approx(0).epsilon(0.001));
    CHECK(clip.z / clip.w > 0);
    CHECK(clip.z / clip.w < 1);
}

TEST_CASE("moving follows the way the camera faces, and keeps to the ground")
{
    const save::TerrainGrid g = slope(8);
    ParkCamera c = overview(g);
    CHECK(c.target.x == doctest::Approx(16));
    CHECK(c.target.y == doctest::Approx(4));

    c.yaw = 0;
    const ParkCamera ahead = on_ground(panned(c, 0, 4), g);
    CHECK(ahead.target.z == doctest::Approx(c.target.z + 4));
    const ParkCamera right = on_ground(panned(c, 4, 0), g);
    CHECK(right.target.x == doctest::Approx(c.target.x + 4));
    CHECK(right.target.y == doctest::Approx(c.target.y + 1));

    const ParkCamera off = on_ground(panned(c, 1000, 0), g);
    CHECK(off.target.x < 32);
}

TEST_CASE("tilt and distance stay within their limits")
{
    ParkCamera c;
    CHECK(turned(c, 0, 10).pitch == doctest::Approx(kMaxPitch));
    CHECK(turned(c, 0, -10).pitch == doctest::Approx(kMinPitch));
    CHECK(turned(c, 1, 0).yaw == doctest::Approx(1));
    CHECK(zoomed(c, 100).distance == doctest::Approx(kMaxDistance));
    CHECK(zoomed(c, 0.001f).distance == doctest::Approx(kMinDistance));
}

TEST_CASE("the line of sight through a point of the view passes through what is drawn there")
{
    ParkCamera c;
    c.target = { 10, 2, 10 };
    c.yaw = 0.7f;
    const float aspect = 1.6f;
    const glm::vec3 thing { 14, 3, 8 };
    const glm::vec4 clip = view_projection(c, aspect) * glm::vec4(thing, 1);
    const Ray r = view_ray(c, aspect, { clip.x / clip.w, clip.y / clip.w });
    CHECK(glm::length(r.direction) == doctest::Approx(1));
    const glm::vec3 to = thing - r.origin;
    const float along = glm::dot(to, r.direction);
    CHECK(along > 0);
    CHECK(glm::length(to - r.direction * along) == doctest::Approx(0).epsilon(0.001));
    // Through the middle of the view it passes through the target.
    const Ray middle = view_ray(c, aspect, { 0, 0 });
    const glm::vec3 t = c.target - middle.origin;
    CHECK(glm::length(t - middle.direction * glm::dot(t, middle.direction)) == doctest::Approx(0).epsilon(0.001));
}

TEST_CASE("framing points looks at their middle from far enough to see them all")
{
    const std::vector<glm::vec3> points { { 0, 0, 0 }, { 40, 10, 20 }, { 10, 0, 0 } };
    const ParkCamera c = framed(points, 0.5f, 0.6f);
    CHECK(c.target.x == doctest::Approx(20));
    CHECK(c.target.y == doctest::Approx(5));
    CHECK(c.target.z == doctest::Approx(10));
    CHECK(c.yaw == 0.5f);
    CHECK(c.pitch == 0.6f);
    // Every corner of the box shows in a square view.
    const glm::mat4 m = view_projection(c, 1);
    for (const glm::vec3& p : points)
    {
        const glm::vec4 clip = m * glm::vec4(p, 1);
        CHECK(std::abs(clip.x / clip.w) <= 1);
        CHECK(std::abs(clip.y / clip.w) <= 1);
    }
    // A single point is seen from the nearest the camera goes.
    CHECK(framed(std::vector<glm::vec3> { { 3, 4, 5 } }, 0, 0.6f).distance == kMinDistance);
}

TEST_CASE("the pointer pushes the view only near the screen's edges, hardest at them")
{
    const glm::vec2 size { 800, 600 };
    CHECK(edge_push({ 400, 300 }, size, 10) == glm::vec2(0, 0));
    CHECK(edge_push({ 0, 300 }, size, 10) == glm::vec2(-1, 0));
    CHECK(edge_push({ 799, 300 }, size, 10) == glm::vec2(1, 0));
    CHECK(edge_push({ 400, 0 }, size, 10) == glm::vec2(0, 1));    // Top: forward
    CHECK(edge_push({ 400, 599 }, size, 10) == glm::vec2(0, -1)); // Bottom: back
    CHECK(edge_push({ 5, 300 }, size, 10).x == doctest::Approx(-0.5f));
    CHECK(edge_push({ 0, 0 }, size, 10) == glm::vec2(-1, 1));
    CHECK(edge_push({ 0, 0 }, size, 0) == glm::vec2(0, 0));
}

TEST_CASE("a wheel zoom rolls in over its time, most of it early, all of it in the end")
{
    float pending = 1;
    float taken = 0;
    for (int frame = 0; frame < 15; ++frame) // A quarter second at 60 frames a second
    {
        const float step = zoom_step(pending, 1.0f / 60, 0.25f);
        if (frame == 0)
            CHECK(step == doctest::Approx(0.18f).epsilon(0.05));
        pending -= step;
        taken += step;
    }
    CHECK(taken == doctest::Approx(0.95f).epsilon(0.01));
    for (int frame = 0; frame < 120 && pending != 0; ++frame)
        pending -= zoom_step(pending, 1.0f / 60, 0.25f);
    CHECK(pending == 0);
    CHECK(zoom_step(-2, 1, 0) == -2);
}

TEST_CASE("a camera set from an eye and a point looks from that eye")
{
    const glm::vec3 from { 10, 30, -20 };
    const glm::vec3 at { 40, 2, 15 };
    const ParkCamera c = looking(from, at);
    CHECK(c.target == at);
    const glm::vec3 back = eye(c);
    CHECK(back.x == doctest::Approx(from.x).epsilon(1e-4));
    CHECK(back.y == doctest::Approx(from.y).epsilon(1e-4));
    CHECK(back.z == doctest::Approx(from.z).epsilon(1e-4));
    // Straight down is past the tilt's limit.
    CHECK(looking({ 0, 50, 0 }, { 0, 0, 0 }).pitch == doctest::Approx(kMaxPitch));
}

TEST_CASE("riding a car looks out ahead of it and leans as it banks")
{
    const ParkCamera c;
    // Its eyes at (5, 3, 0), facing +Z, banked a quarter turn so its up is the world's -X.
    const Seat seat = glm::translate(glm::mat4(1.0f), glm::vec3(5, 3, 0)) * glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(0, 0, 1));
    CHECK(eye(c, seat) == glm::vec3(5, 3, 0));
    const glm::mat4 view = view_projection(c, 1, seat);
    const auto screen = [&](glm::vec3 p) {
        const glm::vec4 clip = view * glm::vec4(p, 1);
        return glm::vec3(clip) / clip.w;
    };
    const glm::vec3 ahead = screen({ 5, 3, 10 });
    CHECK(ahead.x == doctest::Approx(0).epsilon(1e-4));
    CHECK(ahead.y == doctest::Approx(0).epsilon(1e-4));
    CHECK(screen({ 4, 3, 10 }).y > 0.05f); // Over the rider's head is up the screen
    CHECK(screen({ 5, 3, -10 }).z > 1);    // Behind is not seen
}

TEST_CASE("riding along looks at the track ahead, sways against the pull and shakes with speed")
{
    ovl::RideCameraTuning tuning { .look_ahead = 8, .sway_per_g = 0.5f, .sway_most = 0.4f, .shake_from = 1, .shake = 0.01f, .shake_across = 0.1f, .shake_up = 0.1f, .look_tilt = 0.5f };
    const glm::mat4 still = glm::translate(glm::mat4(1.0f), glm::vec3(0, 2, 0));

    // The track bends off to the right ahead: the view turns to it and leans into the turn.
    RideView view;
    const glm::mat4 turned = ride_look(view, still, glm::vec3(8, 2, 8), true, tuning, 0.1f, { });
    CHECK(turned[2].x == doctest::Approx(std::sqrt(0.5f)));
    CHECK(turned[1].x > 0);
    // Track lying behind is not looked at.
    view = { };
    CHECK(ride_look(view, still, glm::vec3(0, 2, -8), true, tuning, 0.1f, { })[2].z == doctest::Approx(1));

    // Speeding up along +Z: the head sways back, up to the most it may.
    view = { };
    float speed = 0;
    float along = 0;
    glm::mat4 seat;
    for (int i = 0; i < 20; ++i)
    {
        speed += 2;
        along += speed * 0.1f;
        seat = ride_look(view, glm::translate(glm::mat4(1.0f), glm::vec3(0, 2, along)), std::nullopt, true, tuning, 0.1f, { 0, 0 });
    }
    CHECK(seat[3].z < along);
    CHECK(glm::length(view.sway) <= 0.4f + 1e-5f);
    // Without sway the eyes stay put.
    RideView nose;
    nose.last = glm::vec3(0, 2, -4);
    nose.velocity = glm::vec3(0, 0, 0);
    CHECK(glm::vec3(ride_look(nose, still, std::nullopt, false, tuning, 0.1f, { })[3]) == glm::vec3(0, 2, 0));

    // Fast, it shakes; at a crawl it does not.
    const glm::mat4 shaken = ride_look(view, glm::translate(glm::mat4(1.0f), glm::vec3(0, 2, along + 4)), std::nullopt, true, tuning, 0.1f, { 1, 1 });
    CHECK(shaken[2].x > 0.01f);
    RideView slow;
    slow.last = glm::vec3(0, 2, 0);
    CHECK(ride_look(slow, glm::translate(glm::mat4(1.0f), glm::vec3(0, 2, 0.05f)), std::nullopt, true, tuning, 0.1f, { 1, 1 })[2].x == doctest::Approx(0));
}

TEST_CASE("a rider looks round from their seat, the way the seat is turned")
{
    // Banked a quarter turn, its up the world's -X.
    const glm::mat4 seat = glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(0, 0, 1));
    const glm::mat4 left = looked_round(seat, { glm::half_pi<float>(), 0 });
    // A quarter turn about the seat's up, from looking along +Z.
    CHECK(std::abs(left[2].z) < 1e-5f);
    CHECK(glm::length(glm::vec3(left[1]) - glm::vec3(seat[1])) < 1e-5f);
    const glm::mat4 up = looked_round(glm::mat4(1.0f), { 0, 0.5f });
    CHECK(up[2].y > 0.4f); // Pitching up looks up
    CHECK(look_round_limited({ 7, 3 }).y == doctest::Approx(kRideLookMostPitch));
    CHECK(look_round_limited({ 7, 0 }).x == doctest::Approx(7 - glm::two_pi<float>()));
}
