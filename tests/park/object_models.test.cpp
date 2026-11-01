// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/object_models.hpp"

#include "park/terrain_mesh.hpp"

#include <algorithm>
#include <cstdlib>
#include <doctest/doctest.h>

using namespace rct3::park;

TEST_CASE("a model takes the colour slots of all its pictures, at every level")
{
    ObjectModel model;
    CHECK(colour_slots(model) == 0);
    ModelLevel& near = model.levels.emplace_back();
    near.parts.emplace_back().slots = 0b001;
    near.parts.emplace_back().slots = 0;
    ModelLevel& far = model.levels.emplace_back();
    far.billboard = Billboard { };
    far.billboard->slots = 0b100;
    CHECK(colour_slots(model) == 0b101);
}

TEST_CASE("the water picture is see-through, tiles and is never recoloured")
{
    const PaletteImage water = water_picture();
    REQUIRE(water.width > 0);
    REQUIRE(water.indices.size() == water.width * water.height);
    REQUIRE(water.alpha.size() == water.indices.size());
    CHECK(water.slots == 0);
    CHECK(std::ranges::all_of(water.alpha, [](std::uint8_t a) { return a > 0 && a < 255; }));
    // The edges meet: across the seam the shade steps no more than between neighbours inside the picture.
    const auto at = [&](std::uint32_t x, std::uint32_t y) { return static_cast<int>(water.indices[y * water.width + x]); };
    int inside = 0;
    int seam = 0;
    for (std::uint32_t y = 0; y < water.height; ++y)
    {
        for (std::uint32_t x = 1; x < water.width; ++x)
            inside = std::max(inside, std::abs(at(x, y) - at(x - 1, y)));
        seam = std::max(seam, std::abs(at(0, y) - at(water.width - 1, y)));
    }
    CHECK(seam <= inside);
    // In the lakes' colour at its darkest.
    CHECK(water.palette[0].b == static_cast<std::uint8_t>(kWaterColour.b * 255));
}

TEST_CASE("a mesh is blended as its transparency says")
{
    CHECK(part_blend(0, "SIOpaqueUnlit") == PartBlend::Opaque);
    CHECK(part_blend(1, "SIAlphaMask") == PartBlend::Cutout);
    CHECK(part_blend(2, "SIAlpha") == PartBlend::Blended);
    CHECK(part_blend(7, "") == PartBlend::Opaque);
}

TEST_CASE("a mesh of the depth filling style writes depth only, whatever its transparency")
{
    for (const std::uint32_t transparency : { 0u, 1u, 2u })
    {
        CHECK(part_blend(transparency, "SIFillZ") == PartBlend::DepthOnly);
        CHECK(part_blend(transparency, "sifillz") == PartBlend::DepthOnly);
    }
    CHECK(part_blend(2, "SIFillZed") == PartBlend::Blended);
}

TEST_CASE("only the chrome style that reads no texture of its own is drawn as chrome")
{
    CHECK(chrome_style("SIOpaqueChrome"));
    CHECK(chrome_style("siopaquechrome"));
    CHECK_FALSE(chrome_style("SIAlphaMaskChrome"));
    CHECK_FALSE(chrome_style("SIOpaqueChromeModulate"));
    CHECK_FALSE(chrome_style("SIOpaque"));
    CHECK_FALSE(chrome_style(""));
}

TEST_CASE("a car's lengths run along the line from its rear to its front, even askew in its model")
{
    // Front and rear 2 apart, along (0.6, 0, 0.8); wheels 0.5 in from either end, 0.2 up.
    const CarFit fit = fit_car({ .front = { 0.6f, 0, 0.8f }, .rear = { -0.6f, 0, -0.8f }, .wheels = { glm::vec3 { 0.3f, 0.2f, 0.4f }, glm::vec3 { -0.3f, 0.2f, -0.4f } } });
    CHECK(fit.length == doctest::Approx(2));
    CHECK(fit.front_wheels == doctest::Approx(0.5f));
    CHECK(fit.rear_wheels == doctest::Approx(1.5f));
    CHECK(fit.on_track.y == doctest::Approx(0.2f));
    CHECK(fit.link_front == 0);
    CHECK(fit.link_rear == doctest::Approx(2));
    CHECK_FALSE(fit.link);
}

TEST_CASE("a car without wheel points stands on its middle, and its links couple on where it says")
{
    const CarFit fit = fit_car({ .front = { 0, 0.5f, 1 }, .rear = { 0, 0.5f, -1 }, .link_front = glm::vec3 { 0, 0.5f, 0.8f }, .link_rear = glm::vec3 { 0, 0.5f, -0.9f } });
    CHECK(fit.front_wheels == doctest::Approx(1));
    CHECK(fit.rear_wheels == doctest::Approx(1));
    CHECK(fit.on_track.z == doctest::Approx(0));
    CHECK(fit.link_front == doctest::Approx(0.2f));
    CHECK(fit.link_rear == doctest::Approx(1.9f));
    CHECK(fit.link_height == doctest::Approx(0.5f));
}
