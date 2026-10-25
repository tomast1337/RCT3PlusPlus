// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/support_columns.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <format>
#include <set>

using namespace rct3;
using namespace rct3::park;

namespace
{

    constexpr std::string_view kGroup = "girders";

    SupportSets girders()
    {
        SupportSets sets;
        for (const auto& [symbol, levels] : { std::pair { "Mid1", 1 }, std::pair { "Mid2", 2 }, std::pair { "Mid4", 4 }, std::pair { "Mid8", 8 } })
            add_support_piece(sets, kGroup, kSupportMiddleType, symbol, levels);
        add_support_piece(sets, kGroup, kSupportFootType, "Foot", 1);
        add_support_piece(sets, kGroup, kSupportDeepFootType, "DeepFoot", 1);
        add_support_piece(sets, kGroup, kSupportTopType, "Head", 1);
        add_support_piece(sets, kGroup, kSupportCapType, "Lid", 1);
        add_support_piece(sets, kGroup, kSupportBeamType, "ShortBeam", 1);
        add_support_piece(sets, kGroup, kSupportBeamType, "LongBeam", 1);
        return sets;
    }

    save::TerrainGrid flat_grid(int size, float level)
    {
        save::TerrainGrid g;
        g.width = size;
        g.height = size;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(size * size));
        for (save::TerrainCell& c : g.cells)
            c.corners = { level, level, level, level };
        return g;
    }

    /// A support piece's model: a box from `low` to `high`.
    ObjectModel box(glm::vec3 low, glm::vec3 high)
    {
        ObjectModel m;
        m.support_group = std::string(kGroup);
        ModelPart part;
        part.vertices = { { low }, { high } };
        m.levels.emplace_back().parts.push_back(std::move(part));
        return m;
    }

    struct Park
    {
        ObjectModels models;
        std::vector<save::PlacedObject> objects;

        Park()
        {
            const auto piece = [&](std::string_view symbol, glm::vec3 low, glm::vec3 high) { models.emplace(std::format("{}|{}", kSupportOverlay, symbol), box(low, high)); };
            piece("Head", { -0.5f, 0, -0.5f }, { 0.5f, 1, 0.5f });
            piece("Lid", { -0.3f, 0, -0.3f }, { 0.3f, 0.5f, 0.3f });
            piece("ShortBeam", { -0.5f, -0.1f, -0.2f }, { 0.5f, 0.1f, 0.2f });
            piece("LongBeam", { -1.5f, -0.1f, -0.2f }, { 1.5f, 0.1f, 0.2f });
        }

        save::PlacedObject& add(std::string symbol, SupportFootprint footprint, int col, int row, std::int32_t base, std::int32_t rotation = 0)
        {
            ObjectModel m;
            m.support_group = std::string(kGroup);
            m.supports = std::move(footprint);
            models.emplace(std::format("Track|{}", symbol), std::move(m));
            save::PlacedObject& o = objects.emplace_back();
            o.id = objects.size();
            o.symbol = std::move(symbol);
            o.overlay = "Track";
            o.col = col;
            o.row = row;
            o.base_height = base;
            o.rotation = rotation;
            return o;
        }
    };

    /// One square of track that wants a round column under it and takes levels `low` to `high`.
    SupportFootprint track_square(std::int32_t low = 0, std::int32_t high = 1)
    {
        SupportFootprint f;
        f.x_squares = 1;
        f.z_squares = 1;
        f.squares = { { low, high, kRoundSupport, true, 0x1ff } };
        f.wants_supports = true;
        f.track = true;
        return f;
    }

    std::vector<std::pair<std::string, std::int32_t>> stack(const std::vector<save::PlacedObject>& pieces, int col, int row)
    {
        std::vector<std::pair<std::string, std::int32_t>> out;
        for (const save::PlacedObject& o : pieces)
        {
            if (o.col == col && o.row == row)
                out.emplace_back(o.symbol, *o.base_height);
        }
        std::ranges::sort(out, { }, &std::pair<std::string, std::int32_t>::second);
        return out;
    }

    const save::PlacedObject* find(const BuiltSupports& built, std::string_view symbol)
    {
        const auto found = std::ranges::find(built.pieces, symbol, &save::PlacedObject::symbol);
        return found != built.pieces.end() ? &*found : nullptr;
    }

} // namespace

TEST_CASE("a footprint's squares start at the object's tile and turn with it")
{
    SupportFootprint f = track_square();
    f.x_squares = 3;
    f.squares.resize(3);
    save::PlacedObject o;
    o.col = 5;
    o.row = 5;
    CHECK(footprint_tiles(o, f) == std::vector<std::pair<int, int>> { { 5, 5 }, { 6, 5 }, { 7, 5 } });
    o.rotation = 1;
    CHECK(footprint_tiles(o, f) == std::vector<std::pair<int, int>> { { 5, 7 }, { 5, 6 }, { 5, 5 } });
}

TEST_CASE("a column under track on the ground: deep foot, middles lined up on their heights, top piece")
{
    Park park;
    park.add("Straight", track_square(), 3, 3, 10);
    const BuiltSupports built = support_columns(park.objects, park.models, { }, flat_grid(8, 2), girders(), 100);
    using Stack = std::vector<std::pair<std::string, std::int32_t>>;
    CHECK(stack(built.pieces, 3, 3) == Stack { { "DeepFoot", 2 }, { "Mid1", 3 }, { "Mid4", 4 }, { "Mid1", 8 }, { "Head", 9 } });
    CHECK(built.fitted.empty());
    CHECK(built.pieces.front().id == 101);
    CHECK(built.pieces.front().force_absolute_height);
}

TEST_CASE("a column needs room above the ground and no ground path under it")
{
    Park park;
    park.add("Low", track_square(), 1, 1, 2);
    park.add("OverPath", track_square(), 2, 2, 10);
    save::PathTile path;
    path.col = 2;
    path.row = 2;
    const std::vector<save::PathTile> paths { path };
    CHECK(support_columns(park.objects, park.models, paths, flat_grid(4, 2), girders(), 0).pieces.empty());
}

TEST_CASE("a column stands on track below only when that track or the set allows, a level clear of it")
{
    Park park;
    park.add("Upper", track_square(), 3, 3, 20);
    park.add("Lower", track_square(), 3, 3, 10);
    const auto upper_foot = [&](const SupportSets& sets) {
        std::vector<std::int32_t> feet;
        for (const save::PlacedObject& o : support_columns(park.objects, park.models, { }, flat_grid(8, 0), sets, 0).pieces)
        {
            if (o.symbol == "Foot")
                feet.push_back(*o.base_height);
        }
        return feet;
    };
    CHECK(upper_foot(girders()).empty()); // Only the lower track's column, which stands on the ground
    SupportSets anything = girders();
    anything.at(std::string(kGroup)).front().stands_on_anything = true;
    CHECK(upper_foot(anything) == std::vector<std::int32_t> { 12 });
}

TEST_CASE("items that ask for every other tile get columns where column and row differ in evenness")
{
    Park park;
    SupportFootprint f = track_square();
    f.alternate_tiles = true;
    park.add("Even", f, 2, 2, 10);
    park.add("Odd", f, 2, 3, 10);
    const BuiltSupports built = support_columns(park.objects, park.models, { }, flat_grid(8, 0), girders(), 0);
    CHECK(stack(built.pieces, 2, 2).empty());
    CHECK_FALSE(stack(built.pieces, 2, 3).empty());
}

TEST_CASE("a point near the middle of its third gets a column right under it, capped, its top stretched to fit")
{
    Park park;
    SupportFootprint f = track_square();
    f.by_points = true;
    f.capped = true;
    SupportPoint p;
    p.top = { { 0, 0 }, { 2, 1 }, { 0.5f, 0.55f } };
    p.top_height = 6;
    p.column = p.top;
    p.group = 1;
    SupportPoint other = p; // An alternative of the same group: left out
    other.top.cell = { 0, 0 };
    other.column.cell = { 0, 0 };
    f.points = { p, other };
    park.add("Dip", f, 5, 5, 10);
    const BuiltSupports built = support_columns(park.objects, park.models, { }, flat_grid(8, 0), girders(), 0);

    const save::PlacedObject* lid = find(built, "Lid");
    REQUIRE(lid != nullptr);
    // Thirds run along the model's -X, which unturned is the park's +x: the column stands a third of a tile
    // past the tile's middle.
    const glm::vec3 at { built.fitted.at(lid->id)[3] };
    CHECK(at.x == doctest::Approx(5 * 4 + 2 + 4.0f / 3));
    CHECK(at.z == doctest::Approx(22));
    CHECK(at.y == doctest::Approx(15.5f));
    const save::PlacedObject* head = find(built, "Head");
    REQUIRE(head != nullptr);
    CHECK(built.fitted.at(head->id)[1].y == doctest::Approx(0.5f)); // From 15 to 15.5
    CHECK(built.fitted.size() == built.pieces.size());
    CHECK(std::ranges::count(built.pieces, std::string("DeepFoot"), &save::PlacedObject::symbol) == 1);
    CHECK(find(built, "ShortBeam") == nullptr);
}

TEST_CASE("a point away from its column is joined to it by the shortest beam that spans the gap")
{
    Park park;
    SupportFootprint f = track_square();
    f.by_points = true;
    SupportPoint p;
    p.top = { { 0, 0 }, { 1, 1 }, { 0.5f, 0.5f } };
    p.top_height = 6;
    p.column = { { 0, 0 }, { 1, 0 } };
    p.column_height = 6;
    f.points = { p };
    park.add("Swerve", f, 5, 5, 10);
    const BuiltSupports built = support_columns(park.objects, park.models, { }, flat_grid(8, 0), girders(), 0);

    const save::PlacedObject* beam = find(built, "LongBeam");
    REQUIRE(beam != nullptr);
    CHECK(find(built, "ShortBeam") == nullptr); // 1 long, short of the third of a tile between the two
    const glm::mat4& m = built.fitted.at(beam->id);
    CHECK(glm::vec3(m[3]).x == doctest::Approx(22));
    CHECK(glm::vec3(m[3]).z == doctest::Approx(22 - 4.0f / 6)); // Halfway to the first third along z
    CHECK(glm::vec3(m[3]).y == doctest::Approx(16));
    // Stretched along its run to the distance plus half the top piece past each end.
    CHECK(glm::length(glm::vec3(m[0])) == doctest::Approx((4.0f / 3 + 1) / 3));
}

TEST_CASE("a column over a raised path, a ride or scenery is left out, unless its set stands on anything")
{
    Park park;
    park.add("Track", track_square(), 3, 3, 20);
    SupportFootprint deck = track_square(-1, 0); // A path deck: wants a column too, but is no track
    deck.track = false;
    park.add("Deck", deck, 3, 3, 10);
    SupportFootprint ride = track_square(0, 3); // Wants none of its own
    ride.wants_supports = false;
    ride.track = false;
    park.add("Ride", ride, 6, 6, 1);
    park.add("OverRide", track_square(), 6, 6, 12);
    using Stack = std::vector<std::pair<std::string, std::int32_t>>;
    const BuiltSupports built = support_columns(park.objects, park.models, { }, flat_grid(8, 0), girders(), 0);
    // Only the deck's own column to the ground.
    CHECK(stack(built.pieces, 3, 3) == Stack { { "DeepFoot", 0 }, { "Mid1", 1 }, { "Mid2", 2 }, { "Mid4", 4 }, { "Head", 8 } });
    CHECK(stack(built.pieces, 6, 6).empty());

    SupportSets anything = girders();
    anything.at(std::string(kGroup)).front().stands_on_anything = true;
    const BuiltSupports on = support_columns(park.objects, park.models, { }, flat_grid(8, 0), anything, 0);
    // On the deck's top, with no level kept clear: it is no track.
    CHECK(std::ranges::count(stack(on.pieces, 3, 3), std::pair<std::string, std::int32_t> { "Foot", 10 }) == 1);
    CHECK(std::ranges::count(stack(on.pieces, 6, 6), std::pair<std::string, std::int32_t> { "Foot", 4 }) == 1);
}

TEST_CASE("a column passes by items it is told to, and stands on the ground over buried ones")
{
    Park park;
    park.add("Track", track_square(), 3, 3, 20);
    SupportFootprint platform = track_square(0, 1);
    platform.wants_supports = false;
    park.add("Platform", platform, 3, 3, 10);
    park.add("High", track_square(), 5, 5, 20);
    park.add("Tunnel", track_square(), 5, 5, -6);
    const save::TerrainGrid grid = flat_grid(8, 0);
    CHECK(stack(support_columns(park.objects, park.models, { }, grid, girders(), 0).pieces, 3, 3).empty());
    const BuiltSupports passed = support_columns(park.objects, park.models, { }, grid, girders(), 0, { 2 });
    REQUIRE_FALSE(stack(passed.pieces, 3, 3).empty());
    CHECK(stack(passed.pieces, 3, 3).front() == std::pair<std::string, std::int32_t> { "DeepFoot", 0 });
    // Over track in a tunnel: on the ground, as on an item.
    const auto over_tunnel = stack(passed.pieces, 5, 5);
    REQUIRE_FALSE(over_tunnel.empty());
    CHECK(over_tunnel.front() == std::pair<std::string, std::int32_t> { "Foot", 0 });
}

TEST_CASE("columns are built on the tiles asked for only, each telling what it holds up and where")
{
    Park park;
    park.add("A", track_square(), 2, 2, 10);
    park.add("B", track_square(), 4, 2, 10);
    const std::set<SupportTile> tiles { { 4, 2 } };
    const BuiltSupports built = support_columns(park.objects, park.models, { }, flat_grid(8, 0), girders(), 0, { }, tiles);
    REQUIRE_FALSE(built.pieces.empty());
    REQUIRE(built.of.size() == built.pieces.size());
    for (std::size_t i = 0; i < built.pieces.size(); ++i)
    {
        CHECK(built.pieces[i].col == 4);
        CHECK(built.of[i].held == 2);
        CHECK(built.of[i].tile == SupportTile { 4, 2 });
    }
}

TEST_CASE("an item's support tiles are under its footprint and where its points' columns stand")
{
    Park park;
    SupportFootprint f = track_square();
    f.by_points = true;
    SupportPoint p;
    p.column = { { 1, 0 }, { 1, 1 } }; // Beside it
    f.points = { p };
    const save::PlacedObject& o = park.add("Beside", f, 5, 5, 10);
    CHECK(support_tiles(o, park.models) == std::vector<SupportTile> { { 5, 5 }, { 6, 5 } });
}
