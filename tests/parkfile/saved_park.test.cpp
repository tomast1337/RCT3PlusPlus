// SPDX-License-Identifier: GPL-3.0-or-later
#include "parkfile/saved_park.hpp"

#include "parkfile/binary.hpp"
#include "simulation/units.hpp"

#include <doctest/doctest.h>

using namespace rct3;

namespace
{
    parkfile::SavedPark small_park()
    {
        parkfile::SavedPark park;
        park.base = { std::byte { 1 }, std::byte { 2 }, std::byte { 3 } };
        park.summary.park_name = "Example Park";
        park.summary.preview = { 9, 8, 7, 6 };
        park.grid.width = 2;
        park.grid.height = 1;
        park.grid.cells.resize(2);
        park.paths.emplace_back().col = 5;
        save::PlacedObject& o = park.objects.emplace_back();
        o.id = 42;
        o.symbol = "bench";
        o.base_height = 3;
        o.height_adjust = 0.5f;
        o.colours = { 1, 2, 3 };
        park.rides.stalls.emplace_back();
        park.progress.cash = 123456;
        park.progress.date.year = 2;
        park.progress.forecast = save::WeatherType::Unknown;
        park.progress.day_time = 0.25f;
        park.camera.target = { 1, 2, 3 };
        return park;
    }
} // namespace

TEST_CASE("a saved park reads back as written")
{
    const auto bytes = parkfile::write_saved_park(small_park());
    REQUIRE(parkfile::is_park_file(bytes));
    const auto read = parkfile::read_saved_park(bytes);
    REQUIRE_MESSAGE(read, (read ? "" : read.error()));
    CHECK(read->base.size() == 3);
    CHECK(read->summary.park_name == "Example Park");
    CHECK(read->summary.preview == std::vector<std::uint8_t> { 9, 8, 7, 6 });
    CHECK(read->grid.width == 2);
    CHECK(read->grid.cells.size() == 2);
    CHECK(read->paths.at(0).col == 5);
    CHECK(read->objects.at(0).id == 42);
    CHECK(read->objects.at(0).symbol == "bench");
    CHECK(read->objects.at(0).base_height == 3);
    CHECK(read->objects.at(0).height_adjust == 0.5f);
    CHECK(read->objects.at(0).colours == std::array<std::int32_t, 3> { 1, 2, 3 });
    CHECK(read->rides.stalls.size() == 1);
    CHECK(read->progress.cash == 123456);
    CHECK(read->progress.date.year == 2);
    CHECK(read->progress.day_time == 0.25f);
    CHECK(read->camera.target == glm::vec3 { 1, 2, 3 });
    CHECK_FALSE(read->people); // As in saves made before people were kept
    CHECK(read->no_queue_signs.empty());
    CHECK(parkfile::write_saved_park(*read) == bytes);

    parkfile::SavedPark hidden = *read;
    hidden.no_queue_signs = { 7, 9 };
    const auto again = parkfile::read_saved_park(parkfile::write_saved_park(hidden));
    REQUIRE(again);
    CHECK(again->no_queue_signs == std::vector<save::ObjectId> { 7, 9 });
    CHECK_FALSE(again->shows); // None kept: read again from the original park

    hidden.shows = { { .ride = 4, .tricks = { { .name = "PonyShowIdle", .seconds = 4 }, { .name = "PonyShowHop", .seconds = 12, .invented = true } }, .order = { 1, 1 } } };
    const auto shown = parkfile::read_saved_park(parkfile::write_saved_park(hidden));
    REQUIRE(shown);
    REQUIRE(shown->shows);
    REQUIRE(shown->shows->size() == 1);
    CHECK(shown->shows->front().tricks.at(1).name == "PonyShowHop");
    CHECK(shown->shows->front().tricks.at(1).invented);
    CHECK(shown->shows->front().order == std::vector<std::uint32_t> { 1, 1 });
    CHECK_FALSE(shown->goals); // None kept: they start again from the original park

    hidden.goals = parkfile::SavedGoals { .max_loan = 400, .interest_rate_percent = 2.5f };
    hidden.goals->objectives.objectives = { { .difficulty = 1, .prize_id = 5, .success = true } };
    hidden.goals->objectives.conditions = { { .kind = save::ConditionKind::ParkGuests, .completed = true, .value = 250 } };
    const auto goals = parkfile::read_saved_park(parkfile::write_saved_park(hidden));
    REQUIRE(goals);
    REQUIRE(goals->goals);
    CHECK(goals->goals->objectives.objectives.at(0).success);
    CHECK(goals->goals->objectives.conditions.at(0).value == 250);
    CHECK(goals->goals->interest_rate_percent == 2.5f);
    CHECK(goals->broken_items.empty());

    hidden.broken_items = { 42, 43 };
    const auto broken = parkfile::read_saved_park(parkfile::write_saved_park(hidden));
    REQUIRE(broken);
    CHECK(broken->broken_items == std::vector<std::uint64_t> { 42, 43 });
    CHECK_FALSE(broken->waterfalls); // None kept: read again from the original park

    hidden.waterfalls.emplace(); // All the park's waterfalls deleted
    const auto dry = parkfile::read_saved_park(parkfile::write_saved_park(hidden));
    REQUIRE(dry);
    REQUIRE(dry->waterfalls);
    CHECK(dry->waterfalls->empty());
    hidden.waterfalls->push_back({ .lip_left = { 1, 9, 2 }, .lip_right = { 1, 9, 6 }, .bottom = { 5, 3, 4 }, .flow = { 1, 0, 0 } });
    const auto falls = parkfile::read_saved_park(parkfile::write_saved_park(hidden));
    REQUIRE(falls);
    REQUIRE(falls->waterfalls);
    REQUIRE(falls->waterfalls->size() == 1);
    CHECK(falls->waterfalls->front().bottom.y == 3.0f);
}

TEST_CASE("a cut or changed saved park is refused")
{
    auto bytes = parkfile::write_saved_park(small_park());
    CHECK_FALSE(parkfile::read_saved_park(std::span(bytes).first(bytes.size() - 1)));
    // The first chunk's layout hash, just after its header.
    bytes[16 + 16] ^= std::byte { 1 };
    CHECK_FALSE(parkfile::read_saved_park(bytes));
    CHECK_FALSE(parkfile::is_park_file(std::vector<std::byte>(4)));
}

TEST_CASE("a unit is laid out as its bare number, so parks saved before it was wrapped still load")
{
    CHECK(parkfile::detail::layout_hash<simulation::Share>() == parkfile::detail::layout_hash<float>());
    CHECK(parkfile::detail::layout_hash<simulation::Money>() == parkfile::detail::layout_hash<std::int64_t>());
    CHECK(parkfile::detail::layout_hash<std::optional<simulation::Seconds>>() == parkfile::detail::layout_hash<std::optional<float>>());
}

TEST_CASE("a save keeps the park's objects and those the player built, not those loading makes")
{
    std::vector<save::PlacedObject> objects(7);
    for (std::size_t i = 0; i < objects.size(); ++i)
        objects[i].id = 10 + i;
    // 10 and 11 are the park's; 12 a support made on loading; 13 a path's deck; 14 a track piece; 15 a flat
    // ride and 16 its entrance.
    std::vector<save::PathTile> paths(1);
    paths[0].elevation.emplace().scenery_ref = 13;
    save::RideModel rides;
    rides.rides.emplace_back().track.emplace().pieces.emplace_back().scenery = 14;
    save::Ride& flat = rides.rides.emplace_back();
    flat.base.spot = save::PlacementSpot { .object = 15 };
    flat.stations.emplace_back().entrance_spot = save::PlacementSpot { .object = 16 };
    const auto kept = parkfile::kept_objects(objects, 2, paths, rides);
    REQUIRE(kept.size() == 6);
    CHECK(kept[0].id == 10);
    CHECK(kept[1].id == 11);
    CHECK(kept[2].id == 13);
    CHECK(kept[3].id == 14);
    CHECK(kept[4].id == 15);
    CHECK(kept[5].id == 16);
}

TEST_CASE("a save keeps the guests and staff, and loading puts them back")
{
    using namespace simulation;
    SimWorld world;
    auto& r = world.registry;
    save::PeopleModel model;
    model.guests.resize(2);
    model.guests[0].in_park = true;
    model.staff.resize(1);
    // Ann is drawn as the second saved guest; Bob came in at the gate since; Cy has left.
    const entt::entity ann = r.create();
    r.emplace<Visitor>(ann, Visitor { .name = "Ann" });
    r.emplace<Pose>(ann, Pose { .position = { 1, 0, 2 }, .heading = 0.5f });
    r.emplace<Needs>(ann, Needs { .hunger = 0.25f });
    r.emplace<Mood>(ann);
    r.emplace<Wallet>(ann, 500);
    r.emplace<Taste>(ann, 0.1f, 0.7f);
    const entt::entity bob = r.create();
    r.emplace<Visitor>(bob, Visitor { .name = "Bob" });
    r.emplace<Pose>(bob);
    r.emplace<Needs>(bob);
    r.emplace<Mood>(bob);
    r.emplace<Wallet>(bob);
    const entt::entity cy = r.create();
    r.emplace<Visitor>(cy);
    r.emplace<Pose>(cy);
    r.emplace<Needs>(cy);
    r.emplace<Mood>(cy);
    r.emplace<Wallet>(cy);
    r.emplace<Left>(cy);
    const entt::entity party = r.create();
    r.emplace<Party>(party, Party { .leader = bob, .members = { ann, bob } });
    r.emplace<Member>(ann, party);
    r.emplace<Member>(bob, party);
    const entt::entity sweeper = r.create();
    r.emplace<Staff>(sweeper, Staff { .role = StaffRole::Handyman, .wage = 900, .training = 1, .laziness = 0.25f });
    r.emplace<Pose>(sweeper, Pose { .position = { 3, 0, 4 } });
    r.emplace<Patrol>(sweeper, Patrol { { { 1, 2 }, { 3, 4 } }, 7 });

    parkfile::SavedPark park = small_park();
    park.people = parkfile::keep_people(world, model, { { ann, 1 }, { sweeper, 0 } });
    const auto read = parkfile::read_saved_park(parkfile::write_saved_park(park));
    REQUIRE_MESSAGE(read, (read ? "" : read.error()));
    REQUIRE(read->people);
    const parkfile::SavedPeople& people = *read->people;
    CHECK_FALSE(people.model.guests[0].in_park);
    CHECK(people.model.guests[1].in_park);
    REQUIRE(people.guests.size() == 2);
    REQUIRE(people.staff.size() == 1);
    CHECK(people.staff[0].look == 0);
    CHECK(people.staff[0].role == StaffRole::Handyman);
    CHECK(people.staff[0].wage == 900);
    CHECK(people.staff[0].training == 1);
    CHECK(people.staff[0].laziness == 0.25f);
    REQUIRE(people.staff[0].patrol);
    CHECK(people.staff[0].patrol->tiles == std::vector<glm::ivec2> { { 1, 2 }, { 3, 4 } });
    CHECK(people.staff[0].patrol->zone == 7);

    SimWorld loaded;
    auto& l = loaded.registry;
    const auto guests = parkfile::place_guests(loaded, people);
    REQUIRE(guests.size() == 2);
    const std::size_t first = people.guests[0].look == 1 ? 0 : 1; // Ann's place
    const entt::entity ann2 = guests[first];
    const entt::entity bob2 = guests[1 - first];
    CHECK(l.get<Visitor>(ann2).name == "Ann");
    CHECK(l.get<Pose>(ann2).position == glm::vec3 { 1, 0, 2 });
    CHECK(l.get<Pose>(ann2).heading.value == 0.5f);
    CHECK(l.get<Needs>(ann2).hunger.value == 0.25f);
    CHECK(l.get<Wallet>(ann2).cash.value == 500);
    CHECK(l.get<Taste>(ann2).most.value == 0.7f);
    CHECK(l.get<Visitor>(bob2).name == "Bob");
    CHECK(people.guests[1 - first].look == -1);
    // As start_guests leaves them, walking.
    for (const entt::entity e : guests)
        l.emplace<Errand>(e);
    parkfile::place_parties(loaded, people, guests);
    REQUIRE(l.all_of<Member>(ann2));
    const auto& together = l.get<Party>(l.get<Member>(ann2).party);
    CHECK(together.leader == bob2);
    CHECK(l.get<Errand>(ann2).kind == ErrandKind::Following);
    // No paths to walk on here, so the sweeper is not placed.
    CHECK(parkfile::place_staff(loaded, people) == std::vector<entt::entity> { entt::null });
}
