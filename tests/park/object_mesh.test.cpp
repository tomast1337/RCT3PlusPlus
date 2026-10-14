// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/object_mesh.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid flat()
    {
        save::TerrainGrid g;
        g.width = 2;
        g.height = 1;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(2);
        return g;
    }

    ModelPart triangle(std::string texture, PartBlend blend)
    {
        ModelPart p;
        p.texture = std::move(texture);
        p.blend = blend;
        p.vertices = { { { 0, 0, 0 }, { 0, 1, 0 }, { 0, 0 } }, { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1 } }, { { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0 } } };
        p.indices = { 0, 1, 2 };
        return p;
    }

    save::PlacedObject object(std::string symbol, int col)
    {
        save::PlacedObject o;
        o.id = 1;
        o.owner_ref = 1;
        o.symbol = std::move(symbol);
        o.overlay = "Things/Things";
        o.col = col;
        o.base_height = 0;
        return o;
    }
} // namespace

TEST_CASE("objects are placed and batched by blending, then picture")
{
    ObjectModels models;
    ObjectModel tree;
    tree.levels = { { 100, { triangle("leaves", PartBlend::Cutout), triangle("bark", PartBlend::Opaque) }, { } } };
    ObjectModel glass;
    glass.levels = { { 100, { triangle("glass", PartBlend::Blended), triangle("bark", PartBlend::Opaque) }, { } } };
    models.emplace("Things/Things|Tree", tree);
    models.emplace("Things/Things|Glass", glass);

    save::PlacedObject hidden = object("Tree", 0);
    hidden.hidden = true;
    const std::vector<save::PlacedObject> objects { object("Tree", 0), object("Glass", 1), object("Rock", 1), hidden };
    const ObjectMesh m = object_mesh(objects, models, flat());
    CHECK(m.placed == 2);
    CHECK(m.missing == 1);

    REQUIRE(m.batches.size() == 3);
    CHECK(m.batches[0].blend == PartBlend::Opaque);
    CHECK(m.batches[0].texture == "bark");
    CHECK(m.batches[0].count == 6);
    CHECK(m.batches[1].blend == PartBlend::Cutout);
    CHECK(m.batches[2].blend == PartBlend::Blended);
    CHECK(m.batches[2].first == 9);
    CHECK(m.indices.size() == 12);
    CHECK(m.vertices.size() == 12);

    // The tree's bark stands on the middle of tile 0, the glass's bark on tile 1.
    const GroundVertex& first = m.vertices[m.indices[m.batches[0].first]];
    CHECK(first.position == glm::vec3(2, 0, 2));
    const GroundVertex& second = m.vertices[m.indices[m.batches[0].first + 3]];
    CHECK(second.position == glm::vec3(6, 0, 2));
    // Lit from above, keeping the picture's coordinates.
    CHECK(first.colour.a == 1);
    CHECK(first.colour.r > 0.45f);
    CHECK(m.vertices[m.indices[1]].uv == glm::vec2(0, 1));
}

TEST_CASE("a model is kept by its overlay and symbol")
{
    CHECK(model_key(object("Tree", 0)) == "Things/Things|Tree");
}

TEST_CASE("objects in other colours get their own batches and paintings")
{
    ModelPart wall = triangle("bricks", PartBlend::Opaque);
    wall.slots = 0b001;
    ObjectModels models;
    ObjectModel model;
    model.levels = { { 100, { wall }, { } } };
    models.emplace("Things/Things|Wall", model);

    save::PlacedObject grey = object("Wall", 0);
    grey.colours = { 1, 27, 4 };
    save::PlacedObject same = object("Wall", 1);
    same.colours = { 1, 5, 9 }; // Only slot 0 is used, so this paints alike
    save::PlacedObject red = object("Wall", 1);
    red.colours = { 27, 27, 1 };
    red.id = 2;
    save::PlacedObject custom = object("Wall", 1);
    custom.id = 3;
    const std::vector<save::PlacedObject> objects { grey, same, red, custom };
    const ColourOverrides overrides { { 3, { Rgb { 0x12, 0x34, 0x56 }, Rgb { 1, 1, 1 }, Rgb { } } } };
    const ObjectMesh m = object_mesh(objects, models, flat(), { }, overrides);
    // Grey is colour 1, red colour 27; batches come in name order.
    REQUIRE(m.batches.size() == 3);
    CHECK(m.batches[0].texture == "bricks#123456#000000#000000");
    CHECK(m.batches[0].colours == FlexiColours { Rgb { 0x12, 0x34, 0x56 }, Rgb { }, Rgb { } });
    CHECK(m.batches[1].texture == painted_name("bricks", 0b001, flexi_colours({ 1, 0, 0 })));
    CHECK(m.batches[1].picture == "bricks");
    CHECK(m.batches[1].count == 6);
    CHECK(m.batches[2].texture == painted_name("bricks", 0b001, flexi_colours({ 27, 0, 0 })));

    PaletteImage picture;
    picture.width = 1;
    picture.height = 1;
    picture.indices = { 1 + kFlexiFullShade };
    picture.slots = 0b001;
    ModelPictures pictures;
    pictures.emplace("bricks", picture);
    const auto painted = paint_batches(m.batches, pictures);
    REQUIRE(painted.size() == 3);
    const Rgb want = flexi_colour(27);
    CHECK(painted.at(m.batches[2].texture).pixels == std::vector<std::uint8_t> { want.r, want.g, want.b, 255 });
    CHECK(painted.at(m.batches[0].texture).pixels == std::vector<std::uint8_t> { 0x12, 0x34, 0x56, 255 });
}

namespace
{
    /// A finely modelled item up to 10 units away, a coarse one up to 50, and a billboard up to 200.
    ObjectModel three_levels()
    {
        ObjectModel m;
        Billboard board;
        board.texture = "flat";
        board.width = 2;
        board.height = 3;
        board.uv_min = { 0.25f, 0.0f };
        board.uv_max = { 0.75f, 1.0f };
        m.levels = { { 10, { triangle("fine", PartBlend::Opaque) }, { } }, { 50, { triangle("coarse", PartBlend::Opaque) }, { } }, { 200, { }, board } };
        return m;
    }
} // namespace

TEST_CASE("a level is the first that reaches as far as the viewer")
{
    const ObjectModel m = three_levels();
    CHECK(level_at(m, 0) == 0u);
    CHECK(level_at(m, 10) == 0u);
    CHECK(level_at(m, 11) == 1u);
    CHECK(level_at(m, 199) == 2u);
    CHECK_FALSE(level_at(m, 201));
    CHECK(level_at(m, 15, 2) == 0u);
}

TEST_CASE("objects are drawn at the level their distance calls for")
{
    ObjectModels models;
    models.emplace("Things/Things|Tree", three_levels());
    save::PlacedObject near = object("Tree", 0);
    save::PlacedObject hidden = object("Tree", 0);
    hidden.hidden = true;
    const std::vector<save::PlacedObject> objects { near, object("Rock", 0), hidden };
    const std::vector<glm::vec3> anchors = object_anchors(objects, models, flat());
    REQUIRE(anchors.size() == 3);
    CHECK(anchors[0] == glm::vec3(2, 0, 2));

    CHECK(object_levels(objects, models, anchors, { 2, 5, 2 }) == ObjectLevels { 0, kNotDrawn, kNotDrawn });
    CHECK(object_levels(objects, models, anchors, { 2, 0, 32 }) == ObjectLevels { 1, kNotDrawn, kNotDrawn });
    CHECK(object_levels(objects, models, anchors, { 2, 0, 102 }) == ObjectLevels { 2, kNotDrawn, kNotDrawn });
    CHECK(object_levels(objects, models, anchors, { 2, 0, 502 }) == ObjectLevels { kNotDrawn, kNotDrawn, kNotDrawn });

    const ObjectMesh coarse = object_mesh(objects, models, flat(), ObjectLevels { 1, kNotDrawn, kNotDrawn });
    REQUIRE(coarse.batches.size() == 1);
    CHECK(coarse.batches[0].texture == "coarse");
    CHECK(object_mesh(objects, models, flat(), ObjectLevels { 2, kNotDrawn, kNotDrawn }).indices.empty());
    CHECK(object_mesh(objects, models, flat()).batches[0].texture == "fine");
}

TEST_CASE("billboards stand on their anchor facing the viewer")
{
    ObjectModels models;
    models.emplace("Things/Things|Tree", three_levels());
    const std::vector<save::PlacedObject> objects { object("Tree", 0) };
    const std::vector<glm::vec3> anchors { { 2, 1, 2 } };
    const ObjectMesh m = billboard_mesh(objects, models, anchors, ObjectLevels { 2 }, { 2, 1, 100 });
    REQUIRE(m.batches.size() == 1);
    CHECK(m.batches[0].blend == PartBlend::Cutout);
    CHECK(m.batches[0].texture == "flat");
    CHECK(m.placed == 1);
    REQUIRE(m.vertices.size() == 4);
    REQUIRE(m.indices.size() == 6);
    // Seen from +Z the quad spans X, two wide and three tall, the top at the picture's top.
    float low = 1e9f, high = -1e9f;
    for (const GroundVertex& v : m.vertices)
    {
        CHECK(v.position.z == doctest::Approx(2));
        low = std::min(low, v.position.x);
        high = std::max(high, v.position.x);
        CHECK((v.position.y == doctest::Approx(1) || v.position.y == doctest::Approx(4)));
        CHECK((v.uv.y == 0.0f) == (v.position.y == doctest::Approx(4)));
    }
    CHECK(low == doctest::Approx(1));
    CHECK(high == doctest::Approx(3));
    CHECK(billboard_mesh(objects, models, anchors, ObjectLevels { 1 }, { 2, 1, 100 }).vertices.empty());
}
