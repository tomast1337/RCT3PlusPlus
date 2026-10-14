// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/object_mesh.hpp"

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
    tree.parts = { triangle("leaves", PartBlend::Cutout), triangle("bark", PartBlend::Opaque) };
    ObjectModel glass;
    glass.parts = { triangle("glass", PartBlend::Blended), triangle("bark", PartBlend::Opaque) };
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
    model.parts = { wall };
    models.emplace("Things/Things|Wall", model);

    save::PlacedObject grey = object("Wall", 0);
    grey.colours = { 1, 27, 4 };
    save::PlacedObject same = object("Wall", 1);
    same.colours = { 1, 5, 9 }; // Only slot 0 is used, so this paints alike
    save::PlacedObject red = object("Wall", 1);
    red.colours = { 27, 27, 1 };
    const std::vector<save::PlacedObject> objects { grey, same, red };
    const ObjectMesh m = object_mesh(objects, models, flat());
    REQUIRE(m.batches.size() == 2);
    CHECK(m.batches[0].texture == "bricks#1,0,0");
    CHECK(m.batches[0].picture == "bricks");
    CHECK(m.batches[0].count == 6);
    CHECK(m.batches[1].texture == "bricks#27,0,0");

    PaletteImage picture;
    picture.width = 1;
    picture.height = 1;
    picture.indices = { 1 + kFlexiFullShade };
    picture.slots = 0b001;
    ModelPictures pictures;
    pictures.emplace("bricks", picture);
    const auto painted = paint_batches(m.batches, pictures);
    REQUIRE(painted.size() == 2);
    const ovl::RgbaImage& r = painted.at("bricks#27,0,0");
    const Rgb want = flexi_colour(27);
    CHECK(r.pixels == std::vector<std::uint8_t> { want.r, want.g, want.b, 255 });
}
