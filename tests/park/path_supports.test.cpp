// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_supports.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{

    SupportSets posts()
    {
        SupportSets sets;
        add_support_piece(sets, "stone", kSupportMiddleType, "Post2", 2);
        add_support_piece(sets, "stone", kSupportMiddleType, "Post8", 8);
        add_support_piece(sets, "stone", kSupportMiddleType, "Post1", 1);
        add_support_piece(sets, "stone", kSupportMiddleType, "Post4", 4);
        add_support_piece(sets, "stone", kSupportFootType, "Foot", 1);
        add_support_piece(sets, "stone", kSupportDeepFootType, "DeepFoot", 1);
        return sets;
    }

    save::TerrainGrid flat_grid(int width, int height, float level = 0)
    {
        save::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        for (save::TerrainCell& c : g.cells)
            c.corners = { level, level, level, level };
        return g;
    }

    std::vector<std::string> symbols(const std::vector<StackedPiece>& stack)
    {
        std::vector<std::string> out;
        for (const StackedPiece& s : stack)
            out.push_back(s.symbol);
        return out;
    }

} // namespace

TEST_CASE("a stack is a foot then the tallest middles that fit")
{
    const SupportSet s = posts().at("stone").front();
    const auto stack = support_stack(s, 2, 2, 16);
    CHECK(symbols(stack) == std::vector<std::string> { "Foot", "Post8", "Post4", "Post1" }); // 2 would overshoot
    CHECK(stack[0].level == 2);
    CHECK(stack[1].level == 3);
    CHECK(stack.back().level == 15);
}

TEST_CASE("uneven ground takes the deep foot, from the highest corner rounded up")
{
    const SupportSet s = posts().at("stone").front();
    const auto stack = support_stack(s, 1.2f, 2.5f, 6);
    REQUIRE_FALSE(stack.empty());
    CHECK(stack[0].symbol == "DeepFoot");
    CHECK(stack[0].level == 3);
    CHECK(symbols(stack) == std::vector<std::string> { "DeepFoot", "Post2" });
}

TEST_CASE("no stack where the gap has no room for a foot")
{
    const SupportSet s = posts().at("stone").front();
    CHECK(support_stack(s, 3, 3, 3).empty());
    CHECK(support_stack(s, 3, 3, 2).empty());
    CHECK(support_stack(SupportSet { }, 0, 0, 10).empty());
}

TEST_CASE("raised paths get stacks under their pieces, but not over ground paths")
{
    const save::TerrainGrid g = flat_grid(3, 1);
    ObjectModels models;
    ObjectModel deck;
    deck.support_group = "stone";
    deck.lowest_level = -1;
    deck.levels = { { 100, { }, { } } };

    std::vector<save::PlacedObject> objects;
    for (int col = 0; col < 3; ++col)
    {
        save::PlacedObject o;
        o.id = static_cast<std::uint64_t>(10 + col);
        o.symbol = "Deck";
        o.col = col;
        o.base_height = 5;
        o.rotation = 3;
        objects.push_back(o);
    }
    models.emplace(model_key(objects[0]), deck);

    std::vector<save::PathTile> paths;
    for (int col = 0; col < 3; ++col)
    {
        save::PathTile p;
        p.kind = save::PathKind::Flying;
        p.col = static_cast<std::uint8_t>(col);
        p.elevation = save::PathElevation { };
        p.elevation->scenery_ref = static_cast<std::uint64_t>(10 + col);
        paths.push_back(p);
    }
    save::PathTile below;
    below.kind = save::PathKind::Ground;
    below.col = 2;
    paths.push_back(below);

    const auto supports = path_supports(paths, objects, models, g, posts());
    // Tiles 0 and 1: a foot at 0 and middles up to level 4, under the deck's underside.
    REQUIRE(supports.size() == 2 * 3);
    for (const save::PlacedObject& s : supports)
    {
        CHECK(s.col != 2);
        CHECK(s.id > 12);
        CHECK(s.rotation == 3);
        CHECK(s.force_absolute_height);
        CHECK(s.overlay == kSupportOverlay);
    }
    CHECK(supports[0].symbol == "Foot");
    CHECK(supports[0].base_height == 0);
    CHECK(supports[1].symbol == "Post2");
    CHECK(supports[1].base_height == 1);
    CHECK(supports[2].symbol == "Post1");
    CHECK(supports[2].base_height == 3);
}
