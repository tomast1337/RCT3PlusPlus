// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/model_lights.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    NamedPlace at(std::string_view name, glm::vec3 position, std::int32_t bone = -1)
    {
        return { .name = name, .place = glm::translate(glm::mat4(1.0f), position), .bone = bone };
    }
} // namespace

TEST_CASE("a light's colour and reach can be written in its name")
{
    const std::array places { at("lantern", { 0, 0, 0 }), at("xxlightstart02_r255g0b51_radius7", { 1, 2, 3 }, 4) };
    const std::vector<ModelLight> lights = model_lights(places);
    REQUIRE(lights.size() == 1);
    CHECK(lights[0].position == glm::vec3(1, 2, 3));
    CHECK(lights[0].colour.r == doctest::Approx(1));
    CHECK(lights[0].colour.g == doctest::Approx(0));
    CHECK(lights[0].colour.b == doctest::Approx(0.2));
    CHECK_FALSE(lights[0].colour_slot);
    CHECK(lights[0].radius == doctest::Approx(7));
    CHECK(lights[0].bone == 4);
}

TEST_CASE("a reach in centimetres and a kind of lamp")
{
    const std::array places { at("lightstart01_torch_r10g20b30_radius75cm", { 0, 0, 0 }) };
    const std::vector<ModelLight> lights = model_lights(places);
    REQUIRE(lights.size() == 1);
    CHECK(lights[0].radius == doctest::Approx(0.75));
    CHECK(lights[0].colour.b == doctest::Approx(30.0 / 255));
}

TEST_CASE("an end point and a colour point with the same number complete a light")
{
    const std::array places { at("lightend03", { 0, 1, 9 }), at("lightcolour03_r0g255b0", { }), at("lightstart03", { 0, 1, 0 }), at("lightend04", { 0, 0, 0 }) };
    const std::vector<ModelLight> lights = model_lights(places);
    REQUIRE(lights.size() == 1);
    CHECK(lights[0].radius == doctest::Approx(9));
    CHECK(lights[0].colour == glm::vec3(0, 1, 0));
}

TEST_CASE("an end point outranks a reach in the name")
{
    const std::array places { at("lightstart01_radius2", { 0, 0, 0 }), at("lightend01", { 3, 4, 0 }) };
    const std::vector<ModelLight> lights = model_lights(places);
    REQUIRE(lights.size() == 1);
    CHECK(lights[0].radius == doctest::Approx(5));
}

TEST_CASE("a light can take the colour of one of the item's slots")
{
    const std::array places { at("lightstart01_ flexi2_radius4", { }), at("lightstart02", { }), at("lightcolour02_flexi1", { }), at("lightend02", { 1, 0, 0 }) };
    const std::vector<ModelLight> lights = model_lights(places);
    REQUIRE(lights.size() == 2);
    CHECK(lights[0].colour_slot == 2);
    CHECK(lights[1].colour_slot == 1);
}

TEST_CASE("points that are not whole lights are left out")
{
    const std::array places { at("meshlightstart_hi", { }), at("lightstart05_startred", { }), at("lightstart06_r1g2b3", { }), at("lightstart07_flexi9_radius0", { }) };
    CHECK(model_lights(places).empty());
}

TEST_CASE("lights come in number order, white unless named")
{
    const std::array places { at("lightstart10_radius1", { }), at("lightstart02_radius1", { }), at("LIGHTSTART01_RADIUS1", { }) };
    const std::vector<ModelLight> lights = model_lights(places);
    REQUIRE(lights.size() == 3);
    CHECK(lights[0].colour == glm::vec3(1.0f));
    CHECK(lights[0].radius == doctest::Approx(1));
}

TEST_CASE("particle effects take their name from the start and their way from the end")
{
    const std::array places { at("particlestart02_Smoke", { 1, 0, 0 }), at("particleend02", { 1, 0, 2 }), at("ParticleStart01_campfire", { 0, 1, 0 }), at("particlestart03", { }), at("particleend01_campfire", { 0, 1, 0 }) };
    const std::vector<ModelEffect> effects = model_effects(places);
    REQUIRE(effects.size() == 2);
    CHECK(effects[0].effect == "campfire");
    CHECK(effects[0].up == glm::vec3(0, 1, 0)); // An end on the start itself gives no way: up
    CHECK(effects[1].effect == "smoke");
    CHECK(effects[1].position == glm::vec3(1, 0, 0));
    CHECK(effects[1].up == glm::vec3(0, 0, 1));
}
