// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/skin_library.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    SkinRef skin(TextureId texture, float w = 10, float h = 10)
    {
        SkinRef s;
        s.layout.xs = { 0, w };
        s.layout.ys = { 0, h };
        s.layout.x_count = 2;
        s.layout.y_count = 2;
        s.texture = texture;
        s.texture_width = 64;
        s.texture_height = 64;
        return s;
    }
} // namespace

TEST_CASE("skins are found by name")
{
    SkinLibrary skins;
    skins.add("Frame", skin(1, 30, 20));
    REQUIRE(skins.find("Frame"));
    CHECK(skins.find("Frame")->texture == 1);
    CHECK(skins.find("Frame")->size() == std::array<float, 2> { 30, 20 });
    CHECK_FALSE(skins.find("Missing"));
    CHECK(SkinRef { }.size() == std::array<float, 2> { 0, 0 });
}

TEST_CASE("state lookups fall back to the nearest member of the family")
{
    SkinLibrary skins;
    skins.add("ButtonUp", skin(1));
    skins.add("ButtonUpOver", skin(2));
    skins.add("ButtonDown", skin(3));

    CHECK(skins.find("Button", SkinState::Up)->texture == 1);
    CHECK(skins.find("Button", SkinState::UpOver)->texture == 2);
    CHECK(skins.find("Button", SkinState::Pressed)->texture == 3);
    CHECK(skins.find("Button", SkinState::DownOver)->texture == 3);
    CHECK(skins.find("Button", SkinState::Disabled)->texture == 1);

    SUBCASE("a skin without a family is used for every state")
    {
        skins.add("Plain", skin(9));
        CHECK(skins.find("Plain", SkinState::Pressed)->texture == 9);
        CHECK(skins.find("Plain", SkinState::UpOver)->texture == 9);
    }
    SUBCASE("nothing when neither family nor base exists")
    {
        CHECK_FALSE(skins.find("Nothing", SkinState::Up));
    }
}

TEST_CASE("drawing a missing skin draws nothing")
{
    DrawList list;
    draw_skin(list, nullptr, { 0, 0, 10, 10 });
    CHECK(list.vertices().empty());
    const SkinRef s = skin(4);
    draw_skin(list, &s, { 0, 0, 10, 10 });
    REQUIRE(list.batches().size() == 1);
    CHECK(list.batches()[0].texture == 4);
}
