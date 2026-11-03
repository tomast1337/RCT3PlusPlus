// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/pools.hpp"

#include "simulation/import_park.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    /// A path along row 0 from column 0 to 5; a changing room at (5, 1) opening on the paved square below it,
    /// and three by two squares of water below that.
    park::PoolMap pool_by_path(Sandbox& park, bool open = true)
    {
        park.path({ 0, 0 }, { 5, 0 });
        park::PoolMap pools;
        pools.squares.push_back({ .col = 5, .row = 2, .kind = park::PoolKind::Tile });
        for (std::int32_t row = 3; row < 5; ++row)
            for (std::int32_t col = 4; col < 7; ++col)
                pools.squares.push_back({ .col = col, .row = row });
        pools.rooms.push_back({ .col = 5, .row = 1, .turn = 0, .ticket = 200, .open = open });
        import_pools(park.world(), pools);
        return pools;
    }

    entt::entity room_of(entt::registry& r)
    {
        const auto rooms = r.view<const PoolDoor>();
        return rooms.empty() ? entt::null : rooms.front();
    }
} // namespace

TEST_CASE("a changing room opens on the path tile before its door, at its complex's ticket price")
{
    Sandbox park;
    pool_by_path(park);
    auto& r = park.world().registry;
    const entt::entity room = room_of(r);
    REQUIRE(room != entt::null);
    const Attraction& a = r.get<Attraction>(room);
    CHECK(a.kind == AttractionKind::Pool);
    CHECK(a.price.value == 200);
    CHECK(a.open);
    REQUIRE(a.entrance != kNoNode);
    CHECK(park.world().paths.nodes()[a.entrance].tile == PathTileSpot { 5, 0, 0 });
    const auto& pool = r.get<SwimPool>(r.get<PoolDoor>(room).pool);
    CHECK(pool.water.size() == 6);
    CHECK(pool.paved.size() == 1);
}

TEST_CASE("a guest wishing to swim pays at the changing room, swims for their stay and comes out at the door")
{
    Sandbox park;
    pool_by_path(park);
    auto& r = park.world().registry;
    const entt::entity room = room_of(r);
    const auto guest = park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 });
    REQUIRE(park.run_until([&] { return r.get<Attraction>(room).customers == 1; }, 60));
    CHECK(r.get<Wallet>(guest).cash.value == 800);
    CHECK(r.get<Attraction>(room).income.value == 200);
    CHECK(r.get<Errand>(guest).kind == ErrandKind::Swimming);
    // In the water, below the path, for most of their stay.
    bool swam = false;
    REQUIRE(park.run_until(
        [&] {
            const auto* swimmer = r.try_get<Swimmer>(guest);
            swam = swam || (swimmer != nullptr && swimmer->in_water && r.get<Pose>(guest).position.z > 3 * park.world().paths.tile_size);
            return r.get<Errand>(guest).kind != ErrandKind::Swimming;
        },
        kTuning.pools.stay.value + 2 * kTuning.pools.changing.value + 60));
    CHECK(swam);
    CHECK_FALSE(r.all_of<Swimmer>(guest));
    CHECK(r.get<Needs>(guest).swim.value < 0.2f);
    CHECK(park.world().paths.nearest(r.get<Pose>(guest).position) == r.get<Attraction>(room).entrance);
}

TEST_CASE("nobody swims in the cold, and those wishing to think it too cold")
{
    Sandbox park;
    pool_by_path(park);
    auto& r = park.world().registry;
    r.ctx().emplace<Weather>(Weather { .temperature = kTuning.pools.coldest.value - 5 });
    const auto guest = park.guest({ 0, 0 }, { .swim = 0.9f });
    park.run_seconds(60);
    CHECK(r.get<Attraction>(room_of(r)).customers == 0);
    REQUIRE(r.all_of<Thoughts>(guest));
    CHECK(std::ranges::contains(r.get<Thoughts>(guest).recent, ThoughtKind::TooColdForPools, &Thought::kind));
}

TEST_CASE("a guest wishing to swim where no pool is open thinks so")
{
    Sandbox park;
    pool_by_path(park, false);
    auto& r = park.world().registry;
    const auto guest = park.guest({ 0, 0 }, { .swim = 0.9f });
    park.run_seconds(30);
    CHECK(r.get<Attraction>(room_of(r)).customers == 0);
    REQUIRE(r.all_of<Thoughts>(guest));
    CHECK(std::ranges::contains(r.get<Thoughts>(guest).recent, ThoughtKind::NoPools, &Thought::kind));
}

TEST_CASE("the wish to swim grows the faster the more a guest likes pools")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    auto& r = park.world().registry;
    const auto keen = park.guest({ 0, 0 }, { .cash = 0 });
    const auto cool = park.guest({ 3, 0 }, { .cash = 0 });
    r.emplace<Taste>(keen, Taste { .pools = { 0.9f } });
    r.emplace<Taste>(cool, Taste { .pools = { 0.1f } });
    park.run_seconds(300);
    CHECK(r.get<Needs>(keen).swim.value > 0.1f);
    CHECK(r.get<Needs>(cool).swim.value < r.get<Needs>(keen).swim.value / 5);
}

TEST_CASE("a changing room taken away sends its swimmers out at its door")
{
    Sandbox park;
    park::PoolMap pools = pool_by_path(park);
    auto& r = park.world().registry;
    const auto guest = park.guest({ 0, 0 }, { .swim = 0.9f });
    REQUIRE(park.run_until([&] { return r.get<Errand>(guest).kind == ErrandKind::Swimming; }, 60));
    const NodeId door = r.get<Attraction>(room_of(r)).entrance;
    park.run_seconds(40);
    pools.rooms.clear();
    import_pools(park.world(), pools);
    CHECK(room_of(r) == entt::null);
    CHECK(r.view<const SwimPool>().empty());
    CHECK(r.get<Errand>(guest).kind != ErrandKind::Swimming);
    CHECK_FALSE(r.all_of<Swimmer>(guest));
    CHECK(park.world().paths.nearest(r.get<Pose>(guest).position) == door);
}

TEST_CASE("pools taken in again keep their rooms and swimmers")
{
    Sandbox park;
    park::PoolMap pools = pool_by_path(park);
    auto& r = park.world().registry;
    const entt::entity room = room_of(r);
    const auto guest = park.guest({ 0, 0 }, { .swim = 0.9f });
    REQUIRE(park.run_until([&] { return r.get<Errand>(guest).kind == ErrandKind::Swimming; }, 60));
    pools.rooms[0].ticket = 300;
    pools.squares.push_back({ .col = 7, .row = 3 });
    import_pools(park.world(), pools);
    CHECK(room_of(r) == room);
    CHECK(r.get<Attraction>(room).price.value == 300);
    CHECK(r.get<SwimPool>(r.get<PoolDoor>(room).pool).water.size() == 7);
    CHECK(r.get<Errand>(guest).kind == ErrandKind::Swimming);
}

TEST_CASE("swimmers walk the plain paved squares, not those a spa pool or diving board stands on")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    park::PoolMap pools;
    pools.squares.push_back({ .col = 5, .row = 2, .kind = park::PoolKind::Tile });
    pools.squares.push_back({ .col = 4, .row = 2, .kind = park::PoolKind::Jacuzzi });
    pools.squares.push_back({ .col = 6, .row = 2, .kind = park::PoolKind::HighDive, .turn = 0 });
    pools.squares.push_back({ .col = 5, .row = 3 });
    pools.rooms.push_back({ .col = 5, .row = 1, .turn = 0, .ticket = 200, .open = true });
    import_pools(park.world(), pools);
    const auto& r = park.world().registry;
    const auto& pool = r.get<SwimPool>(r.get<PoolDoor>(room_of(park.world().registry)).pool);
    CHECK(pool.water.size() == 1);
    CHECK(pool.paved.size() == 1);
    CHECK(pool.spa.size() == 1);
}

TEST_CASE("swimmers sit in the spa pools and stand at the extras now and then")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    park::PoolMap pools;
    pools.squares.push_back({ .col = 5, .row = 2, .kind = park::PoolKind::Tile });
    pools.squares.push_back({ .col = 4, .row = 2, .kind = park::PoolKind::Jacuzzi });
    pools.squares.push_back({ .col = 6, .row = 2, .kind = park::PoolKind::Tile });
    pools.squares.push_back({ .col = 5, .row = 3 });
    pools.extras.push_back({ .symbol = "Lounger", .col = 6, .row = 2 });
    pools.rooms.push_back({ .col = 5, .row = 1, .turn = 0, .ticket = 200, .open = true });
    import_pools(park.world(), pools);
    auto& r = park.world().registry;
    const auto& pool = r.get<SwimPool>(r.get<PoolDoor>(room_of(r)).pool);
    REQUIRE(pool.extras.size() == 1);
    std::vector<entt::entity> guests;
    for (int i = 0; i < 8; ++i)
        guests.push_back(park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 }));
    bool in_spa = false, at_extra = false;
    park.run_until(
        [&] {
            for (const entt::entity g : guests)
                if (const auto* swimmer = r.try_get<Swimmer>(g))
                {
                    const glm::vec3 at = r.get<Pose>(g).position;
                    in_spa = in_spa || (swimmer->in_water && glm::distance(at, pool.spa.front()) < 0.01f);
                    at_extra = at_extra || (!swimmer->in_water && glm::distance(at, pool.extras.front().at) < 0.01f);
                }
            return in_spa && at_extra;
        },
        kTuning.pools.stay.value);
    CHECK(in_spa);
    CHECK(at_extra);
}

TEST_CASE("swimmers lie on loungers and stand under showers, as many as they take, the rest waiting their turn")
{
    for (const bool lounger : { true, false })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 5, 0 });
        park::PoolMap pools;
        pools.squares.push_back({ .col = 5, .row = 2, .kind = park::PoolKind::Tile });
        pools.squares.push_back({ .col = 6, .row = 2, .kind = park::PoolKind::Tile });
        pools.squares.push_back({ .col = 5, .row = 3 });
        pools.extras.push_back({ .symbol = "Extra", .col = 6, .row = 2, .turn = 1, .fit = { .lounger = lounger, .shower = !lounger } });
        pools.rooms.push_back({ .col = 5, .row = 1, .turn = 0, .ticket = 200, .open = true });
        import_pools(park.world(), pools);
        auto& r = park.world().registry;
        const auto& pool = r.get<SwimPool>(r.get<PoolDoor>(room_of(r)).pool);
        REQUIRE(pool.extras.size() == 1);
        const PoolSpot& spot = pool.extras.front();
        CHECK(spot.use == (lounger ? PoolUse::Lounge : PoolUse::Shower));
        const std::size_t places = lounger ? kTuning.pools.lounger_places : kTuning.pools.shower_places;
        CHECK(spot.places == places);
        std::vector<entt::entity> guests;
        for (int i = 0; i < 12; ++i)
            guests.push_back(park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 }));
        std::size_t most = 0, waiting = 0;
        for (int t = 0; t < 4 * static_cast<int>(kTuning.pools.stay.value); ++t)
        {
            park.run_seconds(0.25f);
            std::size_t using_it = 0, here = 0;
            for (const entt::entity g : guests)
                if (const auto* swimmer = r.try_get<Swimmer>(g); swimmer != nullptr && swimmer->spot == 0)
                {
                    using_it += swimmer->place >= 0 ? 1 : 0;
                    here += swimmer->place < 0 && glm::distance(r.get<Pose>(g).position, spot.at) < 0.01f ? 1 : 0;
                    if (swimmer->place >= 0)
                        CHECK(swimmer->use == spot.use);
                }
            most = std::max(most, using_it);
            waiting = std::max(waiting, here);
        }
        CHECK(most > 0);
        CHECK(most <= places);
        CHECK(waiting > 0); // Some came while it was full
    }
}

TEST_CASE("extras a park placed as scenery are used as those laid with the pool")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    park::PoolMap pools;
    pools.squares.push_back({ .col = 5, .row = 2, .kind = park::PoolKind::Tile });
    pools.squares.push_back({ .col = 6, .row = 2, .kind = park::PoolKind::Tile });
    pools.squares.push_back({ .col = 5, .row = 3 });
    pools.rooms.push_back({ .col = 5, .row = 1, .turn = 0, .ticket = 200, .open = true });
    const std::array scenery { park::PoolExtra { .symbol = "Loungers", .col = 6, .row = 2, .fit = { .lounger = true } }, park::PoolExtra { .symbol = "Far", .col = 9, .row = 9, .fit = { .lounger = true } } };
    import_pools(park.world(), pools, scenery);
    auto& r = park.world().registry;
    const auto& pool = r.get<SwimPool>(r.get<PoolDoor>(room_of(r)).pool);
    REQUIRE(pool.extras.size() == 1); // Not the one off the pool
    CHECK(pool.extras.front().use == PoolUse::Lounge);
}

TEST_CASE("swimmers dirty the water, and the room's fluid cleans it while the room is open")
{
    Sandbox park;
    pool_by_path(park);
    auto& r = park.world().registry;
    const entt::entity room = room_of(r);
    auto& pool = r.get<SwimPool>(r.get<PoolDoor>(room).pool);
    r.get<PoolTank>(room).left = { 0 }; // Nothing to clean with yet
    for (int k = 0; k < 6; ++k)
        park.guest({ k % 3, 0 }, { .swim = 0.9f, .cash = 1000 });
    REQUIRE(park.run_until([&] { return r.view<const Swimmer>().size() == 6; }, 60));
    park.run_seconds(200);
    CHECK(pool.dirt.value > 0.05f);
    // Everyone out: the fluid takes the dirt out, using the tank.
    REQUIRE(park.run_until([&] { return r.view<const Swimmer>().empty(); }, kTuning.pools.stay.value + 2 * kTuning.pools.changing.value + 60));
    const float dirty = pool.dirt.value;
    r.get<PoolTank>(room) = { .fluid = PoolFluid::Harsh, .left = { 1 } };
    park.run_seconds(120);
    CHECK(pool.dirt.value < dirty);
    CHECK(r.get<PoolTank>(room).left.value < 1);
}

TEST_CASE("swimmers coming out think of the water as it is")
{
    const auto thought_after = [](float dirt, PoolFluid fluid) {
        Sandbox park;
        pool_by_path(park);
        auto& r = park.world().registry;
        const entt::entity room = room_of(r);
        r.get<PoolTank>(room) = { .fluid = fluid, .left = { 1 } };
        auto& pool = r.get<SwimPool>(r.get<PoolDoor>(room).pool);
        const auto guest = park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 });
        park.run_until(
            [&] {
                pool.dirt = { dirt }; // Held where it is
                return r.all_of<Swimmer>(guest) && r.get<Swimmer>(guest).changed;
            },
            120);
        park.run_until(
            [&] {
                pool.dirt = { dirt };
                return !r.all_of<Swimmer>(guest);
            },
            kTuning.pools.stay.value + 2 * kTuning.pools.changing.value + 60);
        const auto* thoughts = r.try_get<Thoughts>(guest);
        if (thoughts == nullptr)
            return std::optional<ThoughtKind> { };
        const auto& recent = thoughts->recent;
        for (const ThoughtKind kind : { ThoughtKind::PoolGood, ThoughtKind::PoolBad, ThoughtKind::PoolOvercleaned })
            if (std::ranges::contains(recent, kind, &Thought::kind))
                return std::optional(kind);
        return std::optional<ThoughtKind> { };
    };
    CHECK(thought_after(0.1f, PoolFluid::Mild) == ThoughtKind::PoolGood);
    CHECK(thought_after(0.8f, PoolFluid::Mild) == ThoughtKind::PoolBad);
    CHECK(thought_after(0.0f, PoolFluid::Harsh) == ThoughtKind::PoolOvercleaned);
    CHECK(thought_after(0.4f, PoolFluid::Mild) == std::nullopt);
}

TEST_CASE("a mechanic fills a changing room's tank running low, and the park pays for the fluid")
{
    Sandbox park;
    pool_by_path(park);
    auto& r = park.world().registry;
    const entt::entity room = room_of(r);
    r.get<PoolTank>(room) = { .fluid = PoolFluid::Strong, .left = { 0.1f } };
    park.staff(StaffRole::Mechanic, { 0, 0 });
    REQUIRE(park.run_until([&] { return r.get<PoolTank>(room).left.value == 1; }, 300));
    REQUIRE(r.ctx().contains<Supplies>());
    const auto paid = r.ctx().get<Supplies>().paid.value;
    CHECK(paid > kTuning.pools.refill[1].value * 8 / 10);
    CHECK(paid <= kTuning.pools.refill[1].value);
}

TEST_CASE("a pool taken in again keeps its dirt")
{
    Sandbox park;
    park::PoolMap pools = pool_by_path(park);
    auto& r = park.world().registry;
    r.get<SwimPool>(r.get<PoolDoor>(room_of(r)).pool).dirt = { 0.5f };
    import_pools(park.world(), pools);
    CHECK(r.get<SwimPool>(r.get<PoolDoor>(room_of(r)).pool).dirt.value == doctest::Approx(0.5f));
}

TEST_CASE("a pool's dirt shows in six steps")
{
    CHECK(dirt_step(SwimPool { .dirt = { 0 } }) == 0);
    CHECK(dirt_step(SwimPool { .dirt = { 0.5f } }) == 3);
    CHECK(dirt_step(SwimPool { .dirt = { 1 } }) == 5);
}

TEST_CASE("guests a park saved swimming go on swimming in their pool for the time they had left")
{
    Sandbox park;
    pool_by_path(park);
    auto& r = park.world().registry;
    const entt::entity room = room_of(r);
    const glm::vec3 water = r.get<SwimPool>(r.get<PoolDoor>(room).pool).water.front();
    const std::array guests { park.guest({ 0, 0 }, { }), park.guest({ 0, 0 }, { }) };
    save::PeopleModel people;
    people.guests.resize(2);
    people.guests[0].person = { .position = { water.x, water.y, water.z }, .state = save::PersonState::Swimming };
    people.guests[0].time_in_pool = 30;
    people.guests[1].person = { .position = { water.x, water.y, water.z }, .state = save::PersonState::Wandering };
    CHECK(resume_saved_swimmers(park.world(), people, guests) == 1);
    CHECK_FALSE(r.all_of<Swimmer>(guests[1]));
    REQUIRE(r.all_of<Swimmer>(guests[0]));
    CHECK(r.get<Swimmer>(guests[0]).in_water);
    CHECK(r.get<Swimmer>(guests[0]).left.value == 30);
    CHECK(r.get<Pose>(guests[0]).position == water);
    CHECK(r.get<Errand>(guests[0]).kind == ErrandKind::Swimming);
    // Their time up, they change and come out at the door.
    REQUIRE(park.run_until([&] { return r.get<Errand>(guests[0]).kind != ErrandKind::Swimming; }, 30 + kTuning.pools.changing.value + 30));
    CHECK(park.world().paths.nearest(r.get<Pose>(guests[0]).position) == r.get<Attraction>(room).entrance);
}
