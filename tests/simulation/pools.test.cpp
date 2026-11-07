// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/pools.hpp"

#include "simulation/import_park.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <array>
#include <doctest/doctest.h>
#include <limits>
#include <map>
#include <set>

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
        pools.rooms.push_back({ .col = 5, .row = 1, .side = rct3::park::Side { 0 }, .ticket = 200, .open = open });
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

TEST_CASE("a pool takes so many swimmers for its water and no more")
{
    Sandbox park;
    pool_by_path(park);
    auto& r = park.world().registry;
    const entt::entity room = room_of(r);
    const auto room_for = static_cast<std::size_t>(kTuning.pools.crowd * static_cast<float>(r.get<SwimPool>(r.get<PoolDoor>(room).pool).water.size()));
    std::vector<entt::entity> keen;
    for (std::size_t i = 0; i < room_for + 4; ++i)
        keen.push_back(park.guest({ static_cast<std::int32_t>(i % 4), 0 }, { .swim = 0.9f, .cash = 1000 }));
    park.run_seconds(60);
    CHECK(r.get<Attraction>(room).customers == room_for);
    CHECK(std::ranges::count_if(keen, [&](entt::entity g) { return r.get<Errand>(g).kind == ErrandKind::Swimming; }) == static_cast<std::ptrdiff_t>(room_for));
}

TEST_CASE("nobody swims on a cold day, and those wishing to think it too cold")
{
    Sandbox park;
    pool_by_path(park);
    auto& r = park.world().registry;
    r.ctx().emplace<Weather>(Weather { .temperature = 0 });
    const auto guest = park.guest({ 0, 0 }, { .swim = 1.0f });
    park.run_seconds(60);
    CHECK(r.get<Attraction>(room_of(r)).customers == 0);
    REQUIRE(r.all_of<Thoughts>(guest));
    CHECK(std::ranges::contains(r.get<Thoughts>(guest).recent, ThoughtKind::TooColdForPools, &Thought::kind));
}

TEST_CASE("on a cool day only those wishing hard for a swim go, the others thinking it too cold")
{
    Sandbox park;
    pool_by_path(park);
    auto& r = park.world().registry;
    r.ctx().emplace<Weather>(Weather { .temperature = kTuning.pools.cool.value - 3 });
    const auto keen = park.guest({ 0, 0 }, { .swim = 0.9f });
    const auto mild = park.guest({ 1, 0 }, { .swim = kTuning.needs.want.value + 0.02f });
    REQUIRE(park.run_until([&] { return r.get<Errand>(keen).kind == ErrandKind::Swimming; }, 60));
    CHECK(r.get<Errand>(mild).kind != ErrandKind::Swimming);
    REQUIRE(r.all_of<Thoughts>(mild));
    CHECK(std::ranges::contains(r.get<Thoughts>(mild).recent, ThoughtKind::TooColdForPools, &Thought::kind));
}

TEST_CASE("on a hot day guests go for a swim wishing less for one")
{
    Sandbox park;
    pool_by_path(park);
    auto& r = park.world().registry;
    const float mild_wish = kTuning.needs.want.value - 0.1f;
    SUBCASE("hot: they go")
    {
        r.ctx().emplace<Weather>(Weather { .temperature = kTuning.pools.warm.value + 8 });
        const auto mild = park.guest({ 0, 0 }, { .swim = mild_wish });
        CHECK(park.run_until([&] { return r.get<Errand>(mild).kind == ErrandKind::Swimming; }, 60));
    }
    SUBCASE("mild: they do not")
    {
        r.ctx().emplace<Weather>(Weather { .temperature = kTuning.pools.cool.value + 2 });
        const auto mild = park.guest({ 0, 0 }, { .swim = mild_wish });
        park.run_seconds(60);
        CHECK(r.get<Errand>(mild).kind != ErrandKind::Swimming);
    }
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

TEST_CASE("a guest will not pay more for a pool than it is worth to them")
{
    Sandbox park;
    park::PoolMap pools = pool_by_path(park);
    pools.rooms[0].ticket = 5000;
    import_pools(park.world(), pools);
    auto& r = park.world().registry;
    const entt::entity room = room_of(r);
    const auto guest = park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 10000 });
    park.run_seconds(30);
    CHECK(r.get<Attraction>(room).customers == 0);
    CHECK(r.get<Wallet>(guest).cash.value == 10000);
    REQUIRE(r.all_of<Thoughts>(guest));
    CHECK(std::ranges::contains(r.get<Thoughts>(guest).recent, ThoughtKind::NotPaying, &Thought::kind));
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
    pools.squares.push_back({ .col = 6, .row = 2, .kind = park::PoolKind::HighDive, .side = rct3::park::Side { 0 } });
    pools.squares.push_back({ .col = 5, .row = 3 });
    pools.rooms.push_back({ .col = 5, .row = 1, .side = rct3::park::Side { 0 }, .ticket = 200, .open = true });
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
    pools.rooms.push_back({ .col = 5, .row = 1, .side = rct3::park::Side { 0 }, .ticket = 200, .open = true });
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
        // Water enough for them all.
        for (std::int32_t row = 3; row < 11; ++row)
            for (std::int32_t col = 4; col < 7; ++col)
                pools.squares.push_back({ .col = col, .row = row });
        pools.extras.push_back({ .symbol = "Extra", .col = 6, .row = 2, .side = rct3::park::Side { 1 }, .fit = { .lounger = lounger, .shower = !lounger } });
        pools.rooms.push_back({ .col = 5, .row = 1, .side = rct3::park::Side { 0 }, .ticket = 200, .open = true });
        import_pools(park.world(), pools);
        auto& r = park.world().registry;
        const auto& pool = r.get<SwimPool>(r.get<PoolDoor>(room_of(r)).pool);
        REQUIRE(pool.extras.size() == 1);
        const PoolSpot& spot = pool.extras.front();
        CHECK(spot.use == (lounger ? PoolUse::Lounge : PoolUse::Shower));
        const std::size_t places = lounger ? kTuning.pools.lounger_places : kTuning.pools.shower_places;
        CHECK(spot.places == places);
        std::vector<entt::entity> guests;
        for (int i = 0; i < 48; ++i)
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
    pools.rooms.push_back({ .col = 5, .row = 1, .side = rct3::park::Side { 0 }, .ticket = 200, .open = true });
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

TEST_CASE("a guest wears their swimwear from changing into it at the room till changing back, their clothes everywhere else")
{
    Sandbox park;
    pool_by_path(park);
    auto& r = park.world().registry;
    const entt::entity room = room_of(r);
    const auto guest = park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 });
    bool dressed_outside = false;
    REQUIRE(park.run_until(
        [&] {
            dressed_outside = dressed_outside || (in_swimwear(r, guest) && !r.all_of<Swimmer>(guest));
            return r.all_of<Swimmer>(guest);
        },
        60));
    // Changing, out of sight: still in their clothes.
    CHECK(r.get<Swimmer>(guest).changing.value > 0);
    CHECK_FALSE(in_swimwear(r, guest));
    REQUIRE(park.run_until([&] { return r.get<Swimmer>(guest).changing.value <= 0; }, 60));
    CHECK(in_swimwear(r, guest));
    // In the complex, on the paving and in the water alike, then dressed again at the door.
    REQUIRE(park.run_until(
        [&] {
            const auto* swimmer = r.try_get<Swimmer>(guest);
            dressed_outside = dressed_outside || (in_swimwear(r, guest) && swimmer == nullptr);
            CHECK((swimmer == nullptr || swimmer->changed == in_swimwear(r, guest)));
            return swimmer == nullptr;
        },
        kTuning.pools.stay.value + 2 * kTuning.pools.changing.value + 60));
    CHECK_FALSE(dressed_outside);
    CHECK_FALSE(in_swimwear(r, guest));
    CHECK(park.world().paths.nearest(r.get<Pose>(guest).position) == r.get<Attraction>(room).entrance);
}

TEST_CASE("a guest let go over a pool swims there in their swimwear; picked out, they keep it on till they land")
{
    Sandbox park;
    pool_by_path(park);
    auto& r = park.world().registry;
    const entt::entity room = room_of(r);
    const auto& pool = r.get<SwimPool>(r.get<PoolDoor>(room).pool);
    const auto guest = park.guest({ 0, 0 });
    park.run(5);
    CHECK(pool_door_at(park.world(), pool.water[2]) == room);
    CHECK(pool_door_at(park.world(), pool.paved[0]) == room);
    CHECK_FALSE(pool_door_at(park.world(), park.world().paths.centre(r.get<Attraction>(room).entrance)));

    REQUIRE(pick_up(park.world(), guest));
    CHECK_FALSE(in_swimwear(r, guest));
    REQUIRE(let_go(park.world(), guest, pool.water[2]));
    r.get<Pose>(guest).position = pool.water[2] + glm::vec3 { 0, 5, 0 };
    park.run(1);
    CHECK_FALSE(in_swimwear(r, guest)); // Falling in their clothes
    REQUIRE(park.run_until([&] { return r.all_of<Swimmer>(guest); }, 10));
    CHECK(in_swimwear(r, guest));
    CHECK(r.get<Errand>(guest).kind == ErrandKind::Swimming);
    CHECK(r.get<Errand>(guest).target == room);
    park.run_seconds(2);
    CHECK(r.get<Swimmer>(guest).in_water);

    // Out again, onto the path: in their swimwear while held and falling, their clothes once down.
    REQUIRE(pick_up(park.world(), guest));
    CHECK_FALSE(r.all_of<Swimmer>(guest));
    CHECK(in_swimwear(r, guest));
    const glm::vec3 path = park.world().paths.centre(r.get<Attraction>(room).entrance);
    REQUIRE(let_go(park.world(), guest, path));
    r.get<Pose>(guest).position = path + glm::vec3 { 0, 5, 0 };
    park.run(1);
    CHECK(in_swimwear(r, guest));
    REQUIRE(park.run_until([&] { return !r.all_of<Dropped>(guest); }, 20));
    CHECK_FALSE(in_swimwear(r, guest));
    CHECK_FALSE(r.all_of<Swimmer>(guest));
}

TEST_CASE("swimmers changing cannot be picked up, and staff let go over a pool walk off it")
{
    Sandbox park;
    pool_by_path(park);
    auto& r = park.world().registry;
    const entt::entity room = room_of(r);
    const auto guest = park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 });
    REQUIRE(park.run_until([&] { return r.all_of<Swimmer>(guest); }, 60));
    CHECK_FALSE(pick_up(park.world(), guest));

    const auto& pool = r.get<SwimPool>(r.get<PoolDoor>(room).pool);
    const auto handyman = park.staff(StaffRole::Handyman, { 1, 0 });
    park.run(5);
    REQUIRE(pick_up(park.world(), handyman));
    REQUIRE(let_go(park.world(), handyman, pool.water[0]));
    CHECK(r.get<Dropped>(handyman).pool == entt::null);
    REQUIRE(park.run_until([&] { return !r.all_of<Dropped>(handyman); }, 60));
    CHECK_FALSE(r.all_of<Swimmer>(handyman));
    CHECK_FALSE(in_swimwear(r, handyman));
}

TEST_CASE("swimmers climb a high diving board, walk out, dive off its end into the water one at a time, and swim on")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    park::PoolMap pools;
    pools.squares.push_back({ .col = 5, .row = 2, .kind = park::PoolKind::Tile });
    pools.squares.push_back({ .col = 6, .row = 2, .kind = park::PoolKind::HighDive, .board = 6, .side = rct3::park::Side { 0 } });
    // Water far enough out for the boards' end, more than a square out from the tower.
    for (std::int32_t row = 3; row < 6; ++row)
        for (std::int32_t col = 5; col < 7; ++col)
            pools.squares.push_back({ .col = col, .row = row });
    pools.rooms.push_back({ .col = 5, .row = 1, .side = rct3::park::Side { 0 }, .ticket = 200, .open = true });
    import_pools(park.world(), pools);
    auto& r = park.world().registry;
    const auto& pool = r.get<SwimPool>(r.get<PoolDoor>(room_of(r)).pool);
    REQUIRE(pool.boards.size() == 1);
    const PoolBoard& board = pool.boards.front();
    CHECK(board.high);
    CHECK(board.levels.size() == 2);
    std::vector<entt::entity> guests;
    for (int i = 0; i < 12; ++i)
        guests.push_back(park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 }));
    std::vector<std::vector<DiveStep>> steps(guests.size());
    std::size_t most = 0;
    bool done = false;
    for (int t = 0; t < 4 * static_cast<int>(kTuning.pools.stay.value) && !done; ++t)
    {
        park.run_seconds(0.1f);
        std::size_t on = 0;
        for (std::size_t i = 0; i < guests.size(); ++i)
            if (const auto* swimmer = r.try_get<Swimmer>(guests[i]))
            {
                on += swimmer->dive != DiveStep::None ? 1 : 0;
                if (steps[i].empty() ? swimmer->dive != DiveStep::None : steps[i].back() != swimmer->dive)
                    steps[i].push_back(swimmer->dive);
                if (swimmer->dive == DiveStep::Walk)
                    CHECK(r.get<Pose>(guests[i]).position.y > board.foot.y);
                // Back up in the water past the board's end.
                if (swimmer->dive == DiveStep::Surface)
                {
                    CHECK(swimmer->in_water);
                    CHECK(glm::dot(r.get<Pose>(guests[i]).position - board.foot, glm::vec3 { 0, 0, 1 }) > board.reach);
                }
                done = done || steps[i] == std::vector { DiveStep::Climb, DiveStep::Walk, DiveStep::Jump, DiveStep::Fall, DiveStep::Surface, DiveStep::None };
            }
        most = std::max(most, on);
    }
    CHECK(done);
    CHECK(most == 1);

    // A small board built while someone climbs: the boards are laid out again, and the climber comes down.
    entt::entity climber = entt::null;
    for (int t = 0; t < 4 * static_cast<int>(kTuning.pools.stay.value) && climber == entt::null; ++t)
    {
        park.run_seconds(0.1f);
        for (const entt::entity g : guests)
            if (const auto* swimmer = r.try_get<Swimmer>(g); swimmer != nullptr && swimmer->dive == DiveStep::Climb)
                climber = g;
    }
    REQUIRE(climber != entt::null);
    pools.extras.push_back({ .symbol = "Board", .col = 5, .row = 3, .side = rct3::park::Side { 2 }, .fit = { .board = true } });
    import_pools(park.world(), pools);
    park.run_seconds(0.1f);
    const Swimmer& down = r.get<Swimmer>(climber);
    CHECK((down.dive != DiveStep::Climb || r.get<SwimPool>(r.get<PoolDoor>(room_of(r)).pool).boards[static_cast<std::size_t>(down.board)].high));
}

TEST_CASE("swimmers waiting for a high diving board stand in a line behind its ladder, not on one spot")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    park::PoolMap pools;
    pools.squares.push_back({ .col = 5, .row = 2, .kind = park::PoolKind::Tile });
    pools.squares.push_back({ .col = 6, .row = 2, .kind = park::PoolKind::HighDive, .board = 6, .side = rct3::park::Side { 0 } });
    for (std::int32_t row = 3; row < 9; ++row)
        for (std::int32_t col = 2; col < 9; ++col)
            pools.squares.push_back({ .col = col, .row = row });
    pools.rooms.push_back({ .col = 5, .row = 1, .side = rct3::park::Side { 0 }, .ticket = 200, .open = true });
    import_pools(park.world(), pools);
    auto& r = park.world().registry;
    std::vector<entt::entity> guests;
    for (int i = 0; i < 60; ++i)
        guests.push_back(park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 }));
    // The most waiting at once, standing at their places a while, and the closest two of them ever came.
    std::size_t most = 0;
    float closest = std::numeric_limits<float>::max();
    for (int t = 0; t < 2 * static_cast<int>(kTuning.pools.stay.value) * 10; ++t)
    {
        park.run_seconds(0.1f);
        std::vector<glm::vec3> waiting;
        for (const entt::entity g : guests)
            if (const auto* swimmer = r.try_get<Swimmer>(g); swimmer != nullptr && swimmer->board >= 0 && swimmer->dive == DiveStep::None && swimmer->waited.value >= 1 && glm::distance(r.get<Pose>(g).position, swimmer->goal) < 0.01f)
                waiting.push_back(r.get<Pose>(g).position);
        most = std::max(most, waiting.size());
        for (std::size_t i = 0; i < waiting.size(); ++i)
            for (std::size_t j = i + 1; j < waiting.size(); ++j)
                closest = std::min(closest, glm::distance(waiting[i], waiting[j]));
    }
    CAPTURE(most);
    CHECK(most >= 2);
    CHECK(closest > kTuning.pools.queue_gap * 0.9f);
}

TEST_CASE("a small diving board on the pool's wall is walked out on and dived off; a ladder is a way out")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    park::PoolMap pools;
    pools.squares.push_back({ .col = 5, .row = 2, .kind = park::PoolKind::Tile });
    for (std::int32_t col = 5; col < 7; ++col)
        pools.squares.push_back({ .col = col, .row = 3 });
    pools.extras.push_back({ .symbol = "Board", .col = 5, .row = 3, .side = rct3::park::Side { 2 }, .fit = { .board = true } });
    pools.extras.push_back({ .symbol = "Ladder", .col = 6, .row = 3, .side = rct3::park::Side { 2 }, .fit = { .ladder = true } });
    pools.rooms.push_back({ .col = 5, .row = 1, .side = rct3::park::Side { 0 }, .ticket = 200, .open = true });
    import_pools(park.world(), pools);
    auto& r = park.world().registry;
    const auto& pool = r.get<SwimPool>(r.get<PoolDoor>(room_of(r)).pool);
    REQUIRE(pool.boards.size() == 1);
    REQUIRE(pool.ladders.size() == 1);
    CHECK_FALSE(pool.boards.front().high);
    CHECK(pool.extras.empty());
    // Behind the wall, on the paving the board and the ladder stand against.
    CHECK(pool.boards.front().foot.z < park.world().paths.origin.y + 3 * park.world().paths.tile_size);
    std::vector<entt::entity> guests;
    for (int i = 0; i < 8; ++i)
        guests.push_back(park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 }));
    bool walked = false, dived = false, ladder_out = false, ladder_in = false;
    park.run_until(
        [&] {
            for (const entt::entity g : guests)
                if (const auto* swimmer = r.try_get<Swimmer>(g))
                {
                    CHECK(swimmer->dive != DiveStep::Climb);
                    walked = walked || swimmer->dive == DiveStep::Walk;
                    dived = dived || (walked && swimmer->dive == DiveStep::Surface);
                    // Up the ladder from its foot in the water, and down it from the paving behind.
                    const PoolLadder& ladder = pool.ladders.front();
                    ladder_out = ladder_out || (swimmer->edge == PoolEdge::LadderOut && swimmer->edge_from == ladder.water && swimmer->edge_to == ladder.deck);
                    ladder_in = ladder_in || (swimmer->edge == PoolEdge::LadderIn && swimmer->edge_from == ladder.deck && swimmer->edge_to == ladder.water);
                }
            return dived && ladder_out && ladder_in;
        },
        6 * kTuning.pools.stay.value);
    CHECK(dived);
    CHECK(ladder_out);
    CHECK(ladder_in);
}

TEST_CASE("swimmers walk up a ramp to a raised square and down it again, the ramp's slope between")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    // The paved square by the room, a square two up beside it with a ramp climbing to it, and water below.
    park::PoolMap pools;
    pools.squares.push_back({ .col = 5, .row = 2, .kind = park::PoolKind::Tile });
    pools.squares.push_back({ .col = 6, .row = 2, .kind = park::PoolKind::Tile, .surface = 2 });
    for (std::int32_t col = 4; col < 7; ++col)
        pools.squares.push_back({ .col = col, .row = 3 });
    pools.extras.push_back({ .symbol = "Ramp", .col = 5, .row = 2, .side = rct3::park::Side { 1 }, .fit = park::pool_extra_fit("Side CentralSolid WholeTile HeightStep 2") });
    pools.rooms.push_back({ .col = 5, .row = 1, .side = rct3::park::Side { 0 }, .ticket = 200, .open = true });
    import_pools(park.world(), pools);
    auto& r = park.world().registry;
    const auto& pool = r.get<SwimPool>(r.get<PoolDoor>(room_of(r)).pool);
    CHECK(pool.extras.empty()); // A ramp is no spot to stand at
    const float tile = park.world().paths.tile_size;
    const float raised_col = park.world().paths.origin.x + 6 * tile;
    std::vector<entt::entity> guests;
    for (int i = 0; i < 8; ++i)
        guests.push_back(park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 }));
    std::set<entt::entity> up;
    bool slope = false, down = false;
    park.run_until(
        [&] {
            for (const entt::entity g : guests)
                if (r.all_of<Swimmer>(g))
                {
                    const glm::vec3 at = r.get<Pose>(g).position;
                    // On the raised square only at its height, never sunk into it.
                    if (at.x > raised_col + 0.1f && at.x < raised_col + tile && at.z > park.world().paths.origin.y + 2 * tile && at.z < park.world().paths.origin.y + 3 * tile)
                    {
                        CHECK(at.y > 1.9f);
                        up.insert(g);
                    }
                    slope = slope || (at.x > raised_col - tile && at.x < raised_col && at.y > 0.5f && at.y < 1.5f);
                    down = down || (up.contains(g) && at.y < 0.5f);
                }
            return slope && down;
        },
        4 * kTuning.pools.stay.value);
    CHECK(slope);
    CHECK(down);
}

TEST_CASE("swimmers cross the water's edge only diving in or climbing out, in the motion's time, never in a jump")
{
    Sandbox park;
    pool_by_path(park);
    auto& r = park.world().registry;
    std::vector<entt::entity> guests;
    for (int i = 0; i < 8; ++i)
        guests.push_back(park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 }));
    struct Seen
    {
        bool in_water = false;
        PoolEdge edge = PoolEdge::None;
        glm::vec3 at { 0.0f };
    };
    std::map<entt::entity, Seen> seen;
    std::set<PoolEdge> crossed;
    constexpr float kTick = 0.1f;
    // Fastest along the way: a walker, or the dive's reach over its time.
    const float most = std::max({ 2.0f, kTuning.pools.dive_reach / kTuning.pools.dive_in.value }) * kTick + 0.01f;
    for (int t = 0; t < static_cast<int>(kTuning.pools.stay.value / kTick); ++t)
    {
        park.run_seconds(kTick);
        for (const entt::entity g : guests)
        {
            const auto* swimmer = r.try_get<Swimmer>(g);
            if (swimmer == nullptr || swimmer->changing.value > 0)
            {
                seen.erase(g);
                continue;
            }
            const glm::vec3 at = r.get<Pose>(g).position;
            if (const auto was = seen.find(g); was != seen.end())
            {
                // Into or out of the water only as an edge's motion ends, and no further a tick than one walks.
                if (swimmer->in_water != was->second.in_water)
                    CHECK(was->second.edge != PoolEdge::None);
                if (swimmer->edge != PoolEdge::None || was->second.edge != PoolEdge::None)
                    CHECK(glm::distance(at, was->second.at) <= most);
            }
            if (swimmer->edge != PoolEdge::None)
                crossed.insert(swimmer->edge);
            seen[g] = { swimmer->in_water, swimmer->edge, at };
        }
    }
    CHECK(crossed.contains(PoolEdge::DiveIn));
    CHECK(crossed.contains(PoolEdge::ClimbOut));
}

namespace
{
    /// The paved square by the room at (5, 2) with water below it, beside it to the east a square three up with no way
    /// to it, and to the west a pool path climbing to a path a unit up.
    park::PoolMap levels_by_path(Sandbox& park)
    {
        park.path({ 0, 0 }, { 5, 0 });
        park::PoolMap pools;
        pools.squares.push_back({ .col = 5, .row = 2, .kind = park::PoolKind::Tile });
        pools.squares.push_back({ .col = 6, .row = 2, .kind = park::PoolKind::Tile, .surface = 3 });
        pools.squares.push_back({ .col = 4, .row = 2, .kind = park::PoolKind::PathSlope, .side = rct3::park::Side { 1 } });
        pools.squares.push_back({ .col = 3, .row = 2, .kind = park::PoolKind::Path, .surface = 1 });
        for (std::int32_t col = 4; col < 7; ++col)
            pools.squares.push_back({ .col = col, .row = 3 });
        pools.rooms.push_back({ .col = 5, .row = 1, .side = rct3::park::Side { 0 }, .ticket = 200, .open = true });
        return pools;
    }

    /// The middle of the square at (`col`, `row`), `height` units up.
    glm::vec3 square_middle(Sandbox& park, std::int32_t col, std::int32_t row, float height)
    {
        const auto& paths = park.world().paths;
        return { paths.origin.x + (static_cast<float>(col) + 0.5f) * paths.tile_size, height * paths.level_height, paths.origin.y + (static_cast<float>(row) + 0.5f) * paths.tile_size };
    }
} // namespace

TEST_CASE("pool squares join where they meet at the same height, a slope at its foot and its top, and not across a step")
{
    Sandbox park;
    import_pools(park.world(), levels_by_path(park));
    auto& r = park.world().registry;
    const auto& pool = r.get<SwimPool>(r.get<PoolDoor>(room_of(r)).pool);
    const auto& paths = park.world().paths;
    const auto walk = [&](std::int32_t col, std::int32_t row, float height) { return pool_walk_at(pool, square_middle(park, col, row, height), paths.tile_size, paths.level_height); };
    const auto deck = walk(5, 2, 0);
    REQUIRE(deck);
    const auto ways = pool_ways(pool, *deck);
    const auto reached = [&](std::int32_t col, std::int32_t row, float height) { return ways[*walk(col, row, height)].has_value(); };
    CHECK(reached(5, 3, 0)); // The water below
    CHECK(reached(4, 3, 0));
    CHECK(reached(3, 2, 1));       // Up the slope
    CHECK_FALSE(reached(6, 2, 3)); // Three up, with no way
    // The slope is walked in from its foot at the deck's height and left at its top a unit up.
    const PoolWalk& slope = pool.walks[*walk(4, 2, 0.5f)];
    REQUIRE(slope.joins.size() == 2);
    for (const PoolWalk::Join& join : slope.joins)
        CHECK(join.via.front().y == doctest::Approx((join.via.front().x < slope.at.x ? 1.0f : 0.0f) * paths.level_height + kTuning.pools.deck));
}

TEST_CASE("a ramp or a spiral staircase joins a raised square to the paving below")
{
    for (const bool ramp : { true, false })
    {
        CAPTURE(ramp);
        Sandbox park;
        park::PoolMap pools = levels_by_path(park);
        if (ramp)
            pools.extras.push_back({ .symbol = "Ramp", .col = 5, .row = 2, .side = rct3::park::Side { 1 }, .fit = park::pool_extra_fit("Side CentralSolid WholeTile HeightStep 3") });
        else
            pools.squares.back() = { .col = 6, .row = 3, .kind = park::PoolKind::Stairs, .board = 3 }; // For the water's last square, below the one three up
        import_pools(park.world(), pools);
        auto& r = park.world().registry;
        const auto& pool = r.get<SwimPool>(r.get<PoolDoor>(room_of(r)).pool);
        const auto& paths = park.world().paths;
        const auto walk = [&](std::int32_t col, std::int32_t row, float height) { return *pool_walk_at(pool, square_middle(park, col, row, height), paths.tile_size, paths.level_height); };
        const auto ways = pool_ways(pool, walk(5, 2, 0));
        REQUIRE(ways[walk(6, 2, 3)]);
        // The way there climbs only on the ramp's or staircase's square.
        CHECK(*ways[walk(6, 2, 3)] == (ramp ? walk(5, 2, 0) : walk(6, 3, 3)));
    }
}

TEST_CASE("swimmers never go where no way over the pool leads, and keep to the paving's height")
{
    Sandbox park;
    import_pools(park.world(), levels_by_path(park));
    auto& r = park.world().registry;
    const auto& paths = park.world().paths;
    std::vector<entt::entity> guests;
    for (int i = 0; i < 8; ++i)
        guests.push_back(park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 }));
    const auto tile = [&](float along, float origin) { return static_cast<std::int32_t>(std::floor((along - origin) / paths.tile_size)); };
    bool up = false;
    for (int t = 0; t < static_cast<int>(2 * kTuning.pools.stay.value); ++t)
    {
        park.run_seconds(1);
        for (const entt::entity g : guests)
            if (const auto* swimmer = r.try_get<Swimmer>(g); swimmer != nullptr && swimmer->changing.value <= 0 && !swimmer->in_water && swimmer->edge == PoolEdge::None)
            {
                const glm::vec3 at = r.get<Pose>(g).position;
                const std::int32_t col = tile(at.x, paths.origin.x);
                const std::int32_t row = tile(at.z, paths.origin.y);
                CAPTURE(col);
                CAPTURE(row);
                CHECK_FALSE((col == 6 && row == 2 && at.y > 1.5f)); // Never on the square three up
                CHECK(col >= 3);
                CHECK(col <= 6);
                // On the path a unit up at its height, on the slope between, elsewhere at the deck's.
                const float height = (col == 3 ? 1.0f : col == 4 && row == 2 ? std::clamp((static_cast<float>(5) - (at.x - paths.origin.x) / paths.tile_size), 0.0f, 1.0f) : 0.0f) * paths.level_height + kTuning.pools.deck;
                if (row == 2 && col != 6)
                    CHECK(at.y == doctest::Approx(height).epsilon(0.02));
                up = up || col == 3;
            }
    }
    CHECK(up);
}

TEST_CASE("swimmers spread over the three lanes each way across a square, not one line through its middles")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    park::PoolMap pools;
    pools.squares.push_back({ .col = 5, .row = 2, .kind = park::PoolKind::Tile });
    for (std::int32_t row = 3; row < 9; ++row)
        for (std::int32_t col = 2; col < 9; ++col)
            pools.squares.push_back({ .col = col, .row = row });
    pools.rooms.push_back({ .col = 5, .row = 1, .side = rct3::park::Side { 0 }, .ticket = 200, .open = true });
    import_pools(park.world(), pools);
    auto& r = park.world().registry;
    const auto& paths = park.world().paths;
    std::vector<entt::entity> guests;
    for (int i = 0; i < 12; ++i)
        guests.push_back(park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 }));
    // How often a swimmer is on each lane, either way: left of the middle, on it, right of it.
    std::array<int, 3> on { };
    for (int t = 0; t < static_cast<int>(kTuning.pools.stay.value); ++t)
    {
        park.run_seconds(1);
        for (const entt::entity g : guests)
            if (const auto* swimmer = r.try_get<Swimmer>(g); swimmer != nullptr && swimmer->changed && swimmer->in_water && swimmer->edge == PoolEdge::None)
            {
                const glm::vec3 at = r.get<Pose>(g).position;
                for (const float along : { at.x - paths.origin.x, at.z - paths.origin.y })
                {
                    const float off = along - (std::floor(along / paths.tile_size) + 0.5f) * paths.tile_size;
                    for (std::size_t k = 0; k < 3; ++k)
                        on[k] += std::abs(off - (static_cast<float>(k) - 1.0f) * kTuning.pools.lanes) < 0.05f;
                }
            }
    }
    CAPTURE(on);
    CHECK(on[1] > 0);
    CHECK(on[0] > on[1] / 4);
    CHECK(on[2] > on[1] / 4);
}
