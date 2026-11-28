// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/types.hpp"
#include "simulation/actions/action.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"

#include <doctest/doctest.h>
#include <utility>

using namespace rct3;
using namespace rct3::simulation::actions;

namespace
{
    struct Park
    {
        simulation::SimWorld sim { };
        simulation::park::ParkState state { };
        save::model::ParkModel model { };
        save::model::RideModel rides { };
        simulation::Research research { };
        std::optional<std::vector<simulation::ResearchItem>> research_items { };
        std::optional<i64> ledger_month { };
        simulation::DayClock day_clock { };
        rct3::park::CarriedKinds carried { };
        std::vector<save::model::StallIndex> unjoined_stalls { };
        std::vector<rct3::park::track::TrackRules> track_rules { };
        std::vector<std::optional<rct3::park::track::TrackCourse>> courses { };
        rct3::park::pools::PoolMap pools { };
        std::optional<save::model::RideIndex> building { };
        std::vector<save::model::RideIndex> readying { };
        std::vector<save::model::RideShow> shows { };
        save::model::PeopleModel people { };
        std::vector<entt::entity> staff_walkers { };
        std::vector<rct3::park::person::Outfit> wardrobe { };
        usize costumes_given = 0;
        bool simulating = true;
        std::vector<save::model::PlacedObject> objects { };
        Changed changed { };
        save::model::TerrainGrid grid { };
        save::model::EnclosureMap enclosures { };
        World world { .sim = sim,
                      .park = state,
                      .model = model,
                      .rides = rides,
                      .research = research,
                      .research_items = research_items,
                      .ledger_month = ledger_month,
                      .day_clock = day_clock,
                      .carried = carried,
                      .unjoined_stalls = unjoined_stalls,
                      .track_rules = track_rules,
                      .courses = courses,
                      .pools = pools,
                      .building = building,
                      .readying = readying,
                      .shows = shows,
                      .people = people,
                      .staff_walkers = staff_walkers,
                      .wardrobe = wardrobe,
                      .costumes_given = costumes_given,
                      .simulating = simulating,
                      .objects = objects,
                      .changed = changed,
                      .grid = grid,
                      .enclosures = enclosures };
    };
} // namespace

TEST_CASE("developer actions are marked for the host alone")
{
    CHECK(UseDeveloperTool::kHostOnly);
    CHECK(SetParkValues::kHostOnly);
}

TEST_CASE("money added counts as a cheat, and a park without money is refused it")
{
    Park p;
    p.state.cash = units::Money { 500 };
    REQUIRE(simulation::actions::apply(p.world, Action { UseDeveloperTool { .tool = DeveloperTool::AddMoney, .amount = 1000 } }));
    CHECK(p.state.cash == units::Money { 1500 });
    CHECK(p.state.cheats_used);
    p.state.cash = std::nullopt;
    CHECK(simulation::actions::apply(p.world, Action { UseDeveloperTool { .tool = DeveloperTool::AddMoney, .amount = 1000 } }).error() == Refusal::Missing);
}

TEST_CASE("buying all land takes every tile on sale for nothing, and its boundary is to be built again")
{
    Park p;
    constexpr auto kForSale = std::to_underlying(save::model::TileFlag::ForSale);
    constexpr auto kRights = std::to_underlying(save::model::TileFlag::RightsOnly);
    p.grid.width = 3;
    p.grid.height = 1;
    p.grid.cells = { { .flags = kForSale }, { .flags = kForSale | kRights }, { .flags = 0 } };
    p.state.cash = units::Money { 0 };
    CHECK(simulation::actions::apply(p.world, UseDeveloperTool { .tool = DeveloperTool::BuyAllLand }) == 2);
    CHECK(p.grid.cells[0].has(save::model::TileFlag::Owned));
    CHECK(p.grid.cells[1].has(save::model::TileFlag::Owned));
    CHECK_FALSE(p.grid.cells[2].has(save::model::TileFlag::Owned));
    CHECK(p.state.cash == units::Money { 0 });
    CHECK(p.changed.land_owned);
}

TEST_CASE("research finished at once invents what is left, and waits for the build lists")
{
    Park p;
    CHECK(simulation::actions::check(p.world, UseDeveloperTool { .tool = DeveloperTool::FinishResearch }).error() == Refusal::Missing);
    p.model.content.entries = { { .id = 7 }, { .id = 8 } };
    p.research_items = std::vector<simulation::ResearchItem> { { .id = 7 }, { .id = 8 } };
    CHECK(simulation::actions::apply(p.world, UseDeveloperTool { .tool = DeveloperTool::FinishResearch }) == 2);
    CHECK(p.model.content.entries[0].invented);
    CHECK(p.model.content.entries[1].invented);
    CHECK(p.changed.invented);
}

TEST_CASE("stall items take back their kind's name, once")
{
    Park p;
    p.rides.stalls.emplace_back().items = { { .name = "Fizz" }, { .name = "%ME%" } };
    CHECK(simulation::actions::apply(p.world, UseDeveloperTool { .tool = DeveloperTool::ResetItemNames }) == 1);
    CHECK(p.rides.stalls[0].items[0].name == "%ME%");
    CHECK(simulation::actions::apply(p.world, UseDeveloperTool { .tool = DeveloperTool::ResetItemNames }) == 0);
}

TEST_CASE("the developer switches count only when they turn")
{
    Park p;
    SUBCASE("slow guests")
    {
        CHECK(simulation::actions::apply(p.world, UseDeveloperTool { .tool = DeveloperTool::SlowGuests, .amount = 4 }) == 1);
        CHECK(p.sim.guest_slowdown == 4);
        CHECK(simulation::actions::apply(p.world, UseDeveloperTool { .tool = DeveloperTool::SlowGuests, .amount = 4 }) == 0);
        CHECK(simulation::actions::apply(p.world, UseDeveloperTool { .tool = DeveloperTool::SlowGuests, .amount = 0 }) == 1);
        CHECK(p.sim.guest_slowdown == 1);
    }
    SUBCASE("parties huddle often")
    {
        CHECK(simulation::actions::apply(p.world, UseDeveloperTool { .tool = DeveloperTool::PartiesHuddleOften, .amount = 1 }) == 1);
        CHECK(p.sim.parties_huddle_often);
        CHECK(simulation::actions::apply(p.world, UseDeveloperTool { .tool = DeveloperTool::PartiesHuddleOften, .amount = 1 }) == 0);
    }
}

TEST_CASE("parties are let in only at a gate, and cash goes to every guest still in")
{
    Park p;
    CHECK(simulation::actions::apply(p.world, Action { UseDeveloperTool { .tool = DeveloperTool::AdmitParties, .amount = 3 } }).error() == Refusal::Missing);
    const entt::entity in = p.sim.registry.create();
    p.sim.registry.emplace<simulation::Wallet>(in);
    const entt::entity gone = p.sim.registry.create();
    p.sim.registry.emplace<simulation::Wallet>(gone);
    p.sim.registry.emplace<simulation::Left>(gone);
    CHECK(simulation::actions::apply(p.world, UseDeveloperTool { .tool = DeveloperTool::GiveEveryoneCash, .amount = 250 }) == 1);
    CHECK(p.sim.registry.get<simulation::Wallet>(in).cash == units::Money { 250 });
    CHECK(p.sim.registry.get<simulation::Wallet>(gone).cash == units::Money { 0 });
}

TEST_CASE("the park's live values are set outright, each given one alone")
{
    Park p;
    p.state.cash = units::Money { 100 };
    REQUIRE(simulation::actions::apply(p.world, Action { SetParkValues { .rating = 0.75f } }));
    CHECK(p.state.rating == 0.75f);
    CHECK(p.state.cash == units::Money { 100 });
    CHECK(p.state.cheats_used);
    REQUIRE(simulation::actions::apply(p.world, Action { SetParkValues { .loan = units::Money { -5 } } }));
    CHECK(p.state.loan == units::Money { 0 });
    REQUIRE(simulation::actions::apply(p.world, Action { SetParkValues { .hours = std::pair { 540, 1260 } } }));
    CHECK(p.model.park.opening_minutes == 540);
    CHECK(p.model.park.closing_minutes == 1260);
    REQUIRE(simulation::actions::apply(p.world, Action { SetParkValues { .weather = save::model::WeatherType::Snowstorm } }));
    CHECK(p.state.weather == save::model::WeatherType::Snowstorm);
    CHECK(p.state.forecast == save::model::WeatherType::Snowstorm);
    REQUIRE(simulation::actions::apply(p.world, Action { SetParkValues { .time_of_day = 0.25f } }));
    CHECK(p.day_clock.time == 0.25f);
    CHECK(p.day_clock.shown == 0.25f);
}

TEST_CASE("a value the park does not keep is refused")
{
    Park p;
    CHECK(simulation::actions::apply(p.world, Action { SetParkValues { .cash = units::Money { 100 } } }).error() == Refusal::Missing);
    CHECK(simulation::actions::apply(p.world, Action { SetParkValues { .entrance_fee = units::Money { 100 } } }).error() == Refusal::Missing);
    CHECK_FALSE(p.state.cheats_used);
}
