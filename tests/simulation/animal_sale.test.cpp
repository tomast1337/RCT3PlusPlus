// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/animal_sale.hpp"

#include "core/types.hpp"
#include "simulation/animals.hpp"
#include "simulation/objectives.hpp"
#include "simulation/zoo_goals.hpp"

#include <doctest/doctest.h>
#include <optional>

using namespace rct3;
using namespace rct3::simulation;

TEST_CASE("an animal fetches its kind's price with a third on top, by its health")
{
    CHECK(sale_price(units::Money { 30'000 }, units::Share { 1 }) == units::Money { 40'000 });
    CHECK(sale_price(units::Money { 30'000 }, units::Share { 0.5f }) == units::Money { 20'000 });
    CHECK(sale_price(units::Money { 90'000 }, units::Share { 1 }) > sale_price(units::Money { 30'000 }, units::Share { 1 })); // Rarer kinds cost more
    CHECK(sale_price(units::Money { 30'000 }, units::Share { 0 }) == units::Money { 0 });
}

TEST_CASE("a sold animal's money comes in on the purchase/sale line and its species', and it leaves the park")
{
    entt::registry r;
    const entt::entity bird = r.create();
    r.emplace<Animal>(bird, Animal { .species = "MadeUpBird", .job = AnimalJob::Idle, .health = units::Share { 0.75f } });
    simulation::park::ParkState state { .cash = units::Money { 1'000 } };
    save::model::Finance money;
    std::optional<i64> kept;

    CHECK(sell_animal(r, bird, units::Money { 8'000 }, state, money, kept) == units::Money { 8'000 });
    CHECK_FALSE(r.valid(bird));
    CHECK(state.cash == units::Money { 9'000 });
    REQUIRE(money.ledger.size() == 1);
    CHECK(money.ledger[0].animal_spending->purchase_sale == -8'000);
    REQUIRE(money.ledger[0].species_spending.size() == 1);
    CHECK(money.ledger[0].species_spending[0].species == "MadeUpBird");
    // The species' sales goal reads the profit.
    const save::model::Condition goal { .kind = save::model::ConditionKind::AnimalSalesProfit, .value = 5'000, .animals = save::model::AnimalTarget { .species = "MadeUpBird" } };
    const GoalReading reading = read_goal(goal, ParkFigures { .this_month = money.ledger[0] });
    CHECK(reading.now == 8'000);
    CHECK(reading.met);

    SUBCASE("one still in its crate is not sold")
    {
        const entt::entity crated = r.create();
        r.emplace<Animal>(crated, Animal { .species = "MadeUpBird" });
        CHECK_FALSE(sell_animal(r, crated, units::Money { 8'000 }, state, money, kept));
        CHECK(r.valid(crated));
        CHECK(state.cash == units::Money { 9'000 });
    }
}
