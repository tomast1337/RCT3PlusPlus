// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/object_pick.hpp"

#include <doctest/doctest.h>
#include <optional>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{
    std::optional<std::size_t> index_of(std::optional<PickHit> hit)
    {
        return hit.transform([](const PickHit& h) { return h.index; });
    }

    save::TerrainGrid flat(float height)
    {
        save::TerrainGrid g;
        g.width = 4;
        g.height = 4;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(16);
        for (save::TerrainCell& c : g.cells)
            c.corners = { height, height, height, height };
        return g;
    }

    /// A model filling the unit cube above its origin.
    ObjectModel cube()
    {
        ModelPart p;
        p.vertices = { { { -1, 0, -1 }, { 0, 1, 0 }, { 0, 0 } }, { { 1, 2, 1 }, { 0, 1, 0 }, { 1, 1 } } };
        ObjectModel m;
        m.levels = { { 100, { p }, { } } };
        return m;
    }

    save::PlacedObject object(std::uint64_t id, int col)
    {
        save::PlacedObject o;
        o.id = id;
        o.symbol = "Cube";
        o.overlay = "Things/Things";
        o.col = col;
        o.base_height = 0;
        return o;
    }

    Ray down_at(float x, float z)
    {
        return { { x, 50, z }, { 0, -1, 0 } };
    }

    const auto any = [](const save::PlacedObject&) { return true; };
} // namespace

TEST_CASE("a line meets a box where it enters it, and misses a box beside it")
{
    const Ray r { { -5, 0.5f, 0.5f }, { 1, 0, 0 } };
    REQUIRE(box_hit(r, { 0, 0, 0 }, { 1, 1, 1 }));
    CHECK(*box_hit(r, { 0, 0, 0 }, { 1, 1, 1 }) == doctest::Approx(5));
    CHECK_FALSE(box_hit(r, { 0, 2, 0 }, { 1, 3, 1 }));
    // Starting inside, it meets the box at once; a box behind it is not met.
    CHECK(*box_hit({ { 0.5f, 0.5f, 0.5f }, { 1, 0, 0 } }, { 0, 0, 0 }, { 1, 1, 1 }) == 0);
    CHECK_FALSE(box_hit({ { 3, 0.5f, 0.5f }, { 1, 0, 0 } }, { 0, 0, 0 }, { 1, 1, 1 }));
}

TEST_CASE("a line looking down meets the ground at its height, and nothing off the map")
{
    const save::TerrainGrid g = flat(3);
    REQUIRE(ground_hit(down_at(6, 6), g));
    CHECK(*ground_hit(down_at(6, 6), g) == doctest::Approx(47).epsilon(0.001));
    CHECK_FALSE(ground_hit(down_at(-20, 6), g));
    CHECK_FALSE(ground_hit({ { 6, 50, 6 }, { 0, 1, 0 } }, g));
}

TEST_CASE("the nearest wanted object on the line is picked")
{
    const save::TerrainGrid g = flat(0);
    ObjectModels models;
    models.emplace("Things/Things|Cube", cube());
    const std::vector<save::PlacedObject> objects { object(1, 0), object(2, 1) };
    // Tile 1's middle is at x 6.
    CHECK(index_of(pick_object(down_at(6, 2), objects, models, g, { }, any)) == 1u);
    CHECK(index_of(pick_object(down_at(2, 2), objects, models, g, { }, any)) == 0u);
    CHECK_FALSE(pick_object(down_at(13, 13), objects, models, g, { }, any));
    // Looking along the row, the nearer one hides the farther.
    const Ray along { { -10, 1, 2 }, { 1, 0, 0 } };
    CHECK(index_of(pick_object(along, objects, models, g, { }, any)) == 0u);
    CHECK(index_of(pick_object(along, objects, models, g, { }, [](const save::PlacedObject& o) { return o.id == 2; })) == 1u);
}

TEST_CASE("ground in front hides an object, and a fitted object is found where it was fitted")
{
    save::TerrainGrid g = flat(0);
    ObjectModels models;
    models.emplace("Things/Things|Cube", cube());
    std::vector<save::PlacedObject> objects { object(1, 1) };
    // A hill on tile 0 stands between the line and the object.
    g.cells[0].corners = { 10, 10, 10, 10 };
    const Ray along { { -10, 1, 2 }, { 1, 0, 0 } };
    CHECK_FALSE(pick_object(along, objects, models, g, { }, any));

    const Fitted fitted { { 1, glm::translate(glm::mat4(1.0f), glm::vec3(10, 0, 10)) } };
    CHECK(index_of(pick_object(down_at(10, 10), objects, models, flat(0), fitted, any)) == 0u);
    CHECK_FALSE(pick_object(down_at(6, 2), objects, models, flat(0), fitted, any));
}

TEST_CASE("the nearest person on the line is picked, unless the ground is in front")
{
    const save::TerrainGrid g = flat(0);
    const std::vector<glm::mat4> people { glm::translate(glm::mat4(1.0f), glm::vec3(2, 0, 2)), glm::translate(glm::mat4(1.0f), glm::vec3(6, 0, 2)) };
    CHECK(index_of(pick_person(down_at(6, 2), people, g)) == 1u);
    CHECK_FALSE(pick_person(down_at(10, 10), people, g));
    // Looking along the row at head height, the nearer one hides the farther; the hit is where the box starts.
    const auto along = pick_person({ { -10, 1.5f, 2 }, { 1, 0, 0 } }, people, g);
    CHECK(index_of(along) == 0u);
    CHECK(along.transform([](const PickHit& h) { return h.distance; }).value_or(-1) == doctest::Approx(12 - 0.4f));
    // Above their heads, nobody.
    CHECK_FALSE(pick_person({ { -10, 2.5f, 2 }, { 1, 0, 0 } }, people, g));
    save::TerrainGrid hill = g;
    hill.cells[0].corners = { 10, 10, 10, 10 };
    CHECK_FALSE(pick_person({ { -10, 1.5f, 2 }, { 1, 0, 0 } }, people, hill));
}

TEST_CASE("kept places and boxes are worked out again only for the objects that changed")
{
    save::TerrainGrid g = flat(0);
    ObjectModels models;
    models.emplace("Things/Things|Cube", cube());
    save::PlacedObject rock = object(3, 2);
    rock.symbol = "Rock";
    std::vector<save::PlacedObject> objects { object(1, 0), object(2, 1), rock };
    ObjectPlaces places;
    std::vector<PickBox> boxes;
    const auto update = [&] {
        const std::vector<std::size_t> changed = update_places(places, objects, models, g, { });
        update_pick_boxes(boxes, places, changed);
        return changed;
    };
    CHECK(update() == std::vector<std::size_t> { 0, 1, 2 });
    CHECK(boxes.size() == 2); // The rock has no model
    CHECK(update().empty());

    objects[1].col = 3;
    CHECK(update() == std::vector<std::size_t> { 1 });
    const auto wanted = [](std::size_t) { return true; };
    CHECK(index_of(pick_object(down_at(14, 2), boxes, g, wanted)) == 1u);
    CHECK_FALSE(pick_object(down_at(6, 2), boxes, g, wanted));

    // A model loaded later is found for the object that had none.
    models.emplace("Things/Things|Rock", cube());
    CHECK(update() == std::vector<std::size_t> { 2 });
    CHECK(boxes.size() == 3);

    // The ground under an object moves it.
    g.cells[0].corners = { 1, 1, 1, 1 };
    CHECK(update() == std::vector<std::size_t> { 0 });
    CHECK(places.transforms[0][3].y == doctest::Approx(1));

    // Taking one out moves the rest down a place: what now stands there is worked out again.
    objects.erase(objects.begin());
    CHECK(update() == std::vector<std::size_t> { 0, 1 });
    REQUIRE(boxes.size() == 2);
    CHECK(boxes[1].index == 1);
    CHECK(index_of(pick_object(down_at(14, 2), boxes, g, wanted)) == 0u);
}
