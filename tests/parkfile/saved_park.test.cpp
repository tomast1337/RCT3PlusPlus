// SPDX-License-Identifier: GPL-3.0-or-later
#include "parkfile/saved_park.hpp"

#include "parkfile/binary.hpp"
#include "simulation/import_park.hpp"
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
        park.progress.climate = 6;
        park.progress.weather_hold = 33;
        park.progress.wind_change = -0.1f;
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
    CHECK(read->progress.climate == 6); // The weather goes on where it was
    CHECK(read->progress.weather_hold == 33);
    CHECK(read->progress.wind_change == -0.1f);
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
    CHECK(falls->flower_beds.empty());

    hidden.flower_beds = { { .object_ref = 42, .water = 7.5f } };
    const auto beds = parkfile::read_saved_park(parkfile::write_saved_park(hidden));
    REQUIRE(beds);
    REQUIRE(beds->flower_beds.size() == 1);
    CHECK(beds->flower_beds.front().object_ref == 42);
    CHECK(beds->flower_beds.front().water == 7.5f);
}

TEST_CASE("a saved park keeps its day mode")
{
    parkfile::SavedPark park = small_park();
    const auto old = parkfile::read_saved_park(parkfile::write_saved_park(park));
    REQUIRE(old);
    CHECK_FALSE(old->day_mode); // As in saves made before the mode was kept
    park.day_mode = simulation::DayMode::AlwaysNight;
    const auto night = parkfile::read_saved_park(parkfile::write_saved_park(park));
    REQUIRE(night);
    CHECK(night->day_mode == simulation::DayMode::AlwaysNight);
    park.day_mode = static_cast<simulation::DayMode>(7);
    const auto odd = parkfile::read_saved_park(parkfile::write_saved_park(park));
    REQUIRE(odd);
    CHECK(odd->day_mode == simulation::DayMode::ParkHours);
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

TEST_CASE("a save keeps the park's objects and those the player built, platforms too, not those loading makes")
{
    std::vector<save::PlacedObject> objects(11);
    for (std::size_t i = 0; i < objects.size(); ++i)
        objects[i].id = 10 + i;
    // 10 and 11 are the park's; 12 a support made on loading; 13 a path's deck; 14 a track piece; 15 a flat
    // ride and 16 its entrance; 17 a tree the player planted; 18 a stall; 19 one grown beyond the edge; 20 the
    // track piece's platform model, owned by its platform record 30.
    objects[10].owner_ref = 30;
    std::vector<save::PathTile> paths(1);
    paths[0].elevation.emplace().scenery_ref = 13;
    save::RideModel rides;
    save::TrackPiece& piece = rides.rides.emplace_back().track.emplace().pieces.emplace_back();
    piece.scenery = 14;
    piece.platform = save::TrackPlatform { .id = 30 };
    save::Ride& flat = rides.rides.emplace_back();
    flat.base.spot = save::PlacementSpot { .object = 15 };
    flat.stations.emplace_back().entrance_spot = save::PlacementSpot { .object = 16 };
    rides.stalls.emplace_back().base.spot = save::PlacementSpot { .object = 18 };
    const auto kept = parkfile::kept_objects(objects, 2, { 17 }, paths, rides);
    REQUIRE(kept.size() == 9);
    CHECK(kept[0].id == 10);
    CHECK(kept[1].id == 11);
    CHECK(kept[2].id == 13);
    CHECK(kept[3].id == 14);
    CHECK(kept[4].id == 15);
    CHECK(kept[5].id == 16);
    CHECK(kept[6].id == 17);
    CHECK(kept[7].id == 18);
    CHECK(kept[8].id == 20);
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
    // Ann is drawn as the second saved guest; Bob arrived since and is walking in; Cy has left.
    const entt::entity ann = r.create();
    r.emplace<Visitor>(ann, Visitor { .name = "Ann" });
    r.emplace<Pose>(ann, Pose { .position = { 1, 0, 2 }, .heading = 0.5f });
    r.emplace<Needs>(ann, Needs { .hunger = 0.25f });
    r.emplace<Mood>(ann);
    r.emplace<Wallet>(ann, Wallet { .cash = { 500 }, .bank = { 4000 }, .spent = { 300 }, .spent_rides = { 300 } });
    r.emplace<Taste>(ann, 0.1f, 0.7f);
    const entt::entity bob = r.create();
    r.emplace<Visitor>(bob, Visitor { .name = "Bob" });
    r.emplace<Pose>(bob);
    r.emplace<Needs>(bob);
    r.emplace<Mood>(bob);
    r.emplace<Wallet>(bob);
    r.emplace<Outside>(bob); // Still on the road in from a spawn point
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
    r.emplace<Staff>(sweeper, Staff { .role = StaffRole::Handyman, .wage = 900, .training = 1, .laziness = 0.25f, .mended = 5, .think = { 12.5f }, .miserable = 3 });
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
    // How far they are towards quitting, their next thought and what they mended.
    CHECK(people.staff[0].mended == 5);
    CHECK(people.staff[0].think == 12.5f);
    CHECK(people.staff[0].miserable == 3);

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
    CHECK(l.get<Wallet>(ann2).bank.value == 4000);
    CHECK(l.get<Wallet>(ann2).spent_rides.value == 300);
    CHECK(l.get<Taste>(ann2).most.value == 0.7f);
    CHECK(l.get<Visitor>(bob2).name == "Bob");
    REQUIRE(l.all_of<Outside>(bob2));
    CHECK(l.get<Outside>(bob2).way == Outside::Way::In);
    CHECK_FALSE(l.all_of<Outside>(ann2));
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

TEST_CASE("a save keeps its swimmers in the pool of their changing room")
{
    using namespace simulation;
    SimWorld world;
    auto& r = world.registry;
    const auto room_at = [](entt::registry& in, std::int32_t col) {
        const entt::entity room = in.create();
        in.emplace<PoolDoor>(room, PoolDoor { .col = col, .row = 3 });
        return room;
    };
    room_at(r, 1);
    const entt::entity room = room_at(r, 2);
    const entt::entity dee = r.create();
    r.emplace<Visitor>(dee);
    r.emplace<Pose>(dee, Pose { .position = { 5, -1, 6 } });
    r.emplace<Needs>(dee);
    r.emplace<Mood>(dee);
    r.emplace<Wallet>(dee);
    r.emplace<Errand>(dee, Errand { .kind = ErrandKind::Swimming, .target = room });
    r.emplace<Swimmer>(dee, Swimmer { .changed = true, .left = { 42 }, .goal = { 5, -1, 7 }, .in_water = true, .spot = 2, .place = 1, .use = PoolUse::Lounge, .waited = { 3 } });

    parkfile::SavedPark park = small_park();
    park.people = parkfile::keep_people(world, { }, { });
    const auto read = parkfile::read_saved_park(parkfile::write_saved_park(park));
    REQUIRE_MESSAGE(read, (read ? "" : read.error()));
    SimWorld loaded;
    auto& l = loaded.registry;
    room_at(l, 1);
    const entt::entity room2 = room_at(l, 2);
    const auto guests = parkfile::place_guests(loaded, *read->people);
    REQUIRE(guests.size() == 1);
    l.emplace<Errand>(guests[0]); // As start_guests leaves them, walking
    CHECK(parkfile::place_swimmers(loaded, *read->people, guests) == 1);
    CHECK(l.get<Errand>(guests[0]).kind == ErrandKind::Swimming);
    CHECK(l.get<Errand>(guests[0]).target == room2);
    CHECK(l.get<Pose>(guests[0]).position == glm::vec3 { 5, -1, 6 });
    REQUIRE(l.all_of<Swimmer>(guests[0]));
    const Swimmer& swimmer = l.get<Swimmer>(guests[0]);
    CHECK(swimmer.changed);
    CHECK(swimmer.left.value == 42);
    CHECK(swimmer.goal == glm::vec3 { 5, -1, 7 });
    CHECK(swimmer.in_water);
    CHECK(swimmer.spot == 2);
    CHECK(swimmer.place == 1);
    CHECK(swimmer.use == PoolUse::Lounge);
    CHECK(swimmer.waited.value == 3);
}

TEST_CASE("a loaded swimmer heads for no slide, the one they were saved heading for being another entity once loaded")
{
    using namespace simulation;
    SimWorld world;
    auto& r = world.registry;
    const entt::entity room = r.create();
    r.emplace<PoolDoor>(room, PoolDoor { .col = 2, .row = 3 });
    const entt::entity slide = r.create();
    const entt::entity dee = r.create();
    r.emplace<Visitor>(dee);
    r.emplace<Pose>(dee);
    r.emplace<Needs>(dee);
    r.emplace<Mood>(dee);
    r.emplace<Wallet>(dee);
    r.emplace<Errand>(dee, Errand { .kind = ErrandKind::Swimming, .target = room });
    r.emplace<Swimmer>(dee, Swimmer { .changed = true, .left = { 42 }, .slide = slide, .room = room });

    parkfile::SavedPark park = small_park();
    park.people = parkfile::keep_people(world, { }, { });
    const auto read = parkfile::read_saved_park(parkfile::write_saved_park(park));
    REQUIRE_MESSAGE(read, (read ? "" : read.error()));
    SimWorld loaded;
    auto& l = loaded.registry;
    // The saved slide's number now names something else.
    for (int i = 0; i < 3; ++i)
        static_cast<void>(l.create());
    const entt::entity room2 = l.create();
    l.emplace<PoolDoor>(room2, PoolDoor { .col = 2, .row = 3 });
    const auto guests = parkfile::place_guests(loaded, *read->people);
    REQUIRE(guests.size() == 1);
    l.emplace<Errand>(guests[0]);
    CHECK(parkfile::place_swimmers(loaded, *read->people, guests) == 1);
    const Swimmer& swimmer = l.get<Swimmer>(guests[0]);
    CHECK(swimmer.slide == entt::null);
    CHECK(swimmer.room == entt::null);
    CHECK(swimmer.left.value == 42);
}

TEST_CASE("a save keeps the enclosures, animal houses with their keepers, animals and dung")
{
    using namespace simulation;
    SimWorld world;
    auto& r = world.registry;
    const entt::entity sweeper = r.create();
    r.emplace<Staff>(sweeper, Staff { .role = StaffRole::Handyman });
    r.emplace<Pose>(sweeper);
    const entt::entity keeper = add_animal_house(world, { .object = 7, .enclosure = 0, .troughs = { 2, 0, 2 }, .food = { 0.5f } }, { 1, 0, 1 });
    const entt::entity yak = add_animal(world, { .species = "yak", .name = "Yak", .enclosure = 0 }, { 3, 0, 1 });
    r.get<Animal>(yak).hunger = { 0.25f };
    const entt::entity dung = r.create();
    r.emplace<Dung>(dung, Dung { 0 });
    r.emplace<Pose>(dung, Pose { .position = { 2, 0, 3 } });
    r.get<KeeperDuty>(keeper) = { .house = r.get<KeeperDuty>(keeper).house, .job = KeeperJob::Clearing, .goal = { 2, 0, 3 }, .target = dung, .busy = { 4 } };
    save::EnclosureMap map { .cols = 2, .rows = 1, .tiles = { 0, -1 }, .fences = { "Wood" } };

    parkfile::SavedPark park = small_park();
    park.people = parkfile::keep_people(world, { }, { });
    park.animals = parkfile::keep_animals(world, map);
    const auto read = parkfile::read_saved_park(parkfile::write_saved_park(park));
    REQUIRE_MESSAGE(read, (read ? "" : read.error()));
    REQUIRE(read->animals);
    CHECK(read->animals->enclosures.tiles == std::vector<std::int32_t> { 0, -1 });
    CHECK(read->animals->enclosures.fences == std::vector<std::string> { "Wood" });

    SimWorld loaded;
    auto& l = loaded.registry;
    const auto staff = parkfile::place_staff(loaded, *read->people);
    parkfile::place_animals(loaded, *read->animals, staff);
    const auto keepers = l.view<const Staff, const KeeperDuty>();
    REQUIRE(std::ranges::distance(keepers) == 1);
    const entt::entity keeper2 = keepers.front();
    CHECK(l.get<Pose>(keeper2).position == r.get<Pose>(keeper).position);
    const AnimalHouse& house = l.get<AnimalHouse>(l.get<KeeperDuty>(keeper2).house);
    CHECK(house.object == 7);
    CHECK(house.food.value == 0.5f);
    const auto animals = l.view<const Animal, const Pose>();
    REQUIRE(std::ranges::distance(animals) == 1);
    CHECK(animals.get<const Animal>(animals.front()).name == "Yak");
    CHECK(animals.get<const Animal>(animals.front()).hunger.value == 0.25f);
    CHECK(animals.get<const Pose>(animals.front()).position == glm::vec3 { 3, 0, 1 });
    CHECK(l.view<const Dung>().size() == 1);
    // The keeper goes on clearing the same dung.
    const KeeperDuty& duty = l.get<KeeperDuty>(keeper2);
    CHECK(duty.job == KeeperJob::Clearing);
    CHECK(duty.goal == glm::vec3 { 2, 0, 3 });
    CHECK(duty.target == l.view<const Dung>().front());
    CHECK(duty.busy.value == 4);
}

TEST_CASE("a save keeps the house a keeper is filling the troughs of")
{
    using namespace simulation;
    SimWorld world;
    auto& r = world.registry;
    const entt::entity keeper = add_animal_house(world, { .object = 7, .enclosure = 0, .troughs = { 2, 0, 2 } }, { 1, 0, 1 });
    const entt::entity other = r.get<KeeperDuty>(add_animal_house(world, { .object = 8, .enclosure = 0, .troughs = { 3, 0, 3 } }, { 3, 0, 1 })).house;
    r.get<KeeperDuty>(keeper) = { .house = r.get<KeeperDuty>(keeper).house, .job = KeeperJob::Filling, .goal = { 3, 0, 3 }, .target = other, .busy = { 2 } };
    parkfile::SavedPark park = small_park();
    park.people = parkfile::keep_people(world, { }, { });
    park.animals = parkfile::keep_animals(world, { });
    const auto read = parkfile::read_saved_park(parkfile::write_saved_park(park));
    REQUIRE_MESSAGE(read, (read ? "" : read.error()));
    SimWorld loaded;
    auto& l = loaded.registry;
    parkfile::place_animals(loaded, *read->animals, parkfile::place_staff(loaded, *read->people));
    const auto filling = std::ranges::find_if(l.view<const KeeperDuty>(), [&](entt::entity k) { return l.get<const KeeperDuty>(k).job == KeeperJob::Filling; });
    REQUIRE(filling != l.view<const KeeperDuty>().end());
    CHECK(l.get<AnimalHouse>(l.get<const KeeperDuty>(*filling).target).object == 8);
}

TEST_CASE("a save keeps the pools, their extras and changing rooms")
{
    parkfile::SavedPark park = small_park();
    park.pools = park::PoolMap { { { .col = 3, .row = 4, .kind = park::PoolKind::Jacuzzi, .surface = 6, .depth = 3 } }, { { .symbol = "Lamp", .overlay = "Pools/Own", .col = 3, .row = 4, .side = rct3::park::Side { 2 }, .refund = -5, .fit = { .side = true, .step = 1 } } }, { { .symbol = "Hut", .overlay = "Style/Own", .col = 2, .row = 4, .side = rct3::park::Side { 1 }, .ticket = 250, .open = true } } };
    const auto read = parkfile::read_saved_park(parkfile::write_saved_park(park));
    REQUIRE_MESSAGE(read, (read ? "" : read.error()));
    REQUIRE(read->pools);
    REQUIRE(read->pools->squares.size() == 1);
    const park::PoolSquare& s = read->pools->squares[0];
    CHECK((s.col == 3 && s.row == 4 && s.kind == park::PoolKind::Jacuzzi && s.surface == 6 && s.depth == 3));
    REQUIRE(read->pools->extras.size() == 1);
    const park::PoolExtra& e = read->pools->extras[0];
    CHECK((e.symbol == "Lamp" && e.overlay == "Pools/Own" && e.side == park::Side { 2 } && e.refund == -5 && e.fit.side && e.fit.step == 1 && !e.fit.wall));
    REQUIRE(read->pools->rooms.size() == 1);
    const park::PoolRoom& r = read->pools->rooms[0];
    CHECK((r.symbol == "Hut" && r.overlay == "Style/Own" && r.col == 2 && r.row == 4 && r.side == park::Side { 1 } && r.ticket == 250 && r.open));
    CHECK_FALSE(parkfile::read_saved_park(parkfile::write_saved_park(small_park()))->pools);
}

TEST_CASE("a save keeps a person's switches and a ride's call for a mechanic")
{
    parkfile::SavedPark park = small_park();
    save::PeopleModel model;
    model.guests.resize(1);
    model.guests[0].person.thoughts_to_console = true;
    model.staff.resize(1);
    model.staff[0].person.highlight = true;
    const simulation::SimWorld world;
    park.people = parkfile::keep_people(world, model, { });
    park.rides.rides.resize(1);
    park.rides.rides[0].base.mechanic_asked = true;
    const auto read = parkfile::read_saved_park(parkfile::write_saved_park(park));
    REQUIRE_MESSAGE(read, (read ? "" : read.error()));
    REQUIRE(read->people);
    CHECK(read->people->model.guests[0].person.thoughts_to_console);
    CHECK_FALSE(read->people->model.guests[0].person.highlight);
    CHECK(read->people->model.staff[0].person.highlight);
    REQUIRE(read->rides.rides.size() == 1);
    CHECK(simulation::upkeep_of(read->rides.rides[0]).called);
}
