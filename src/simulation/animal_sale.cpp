// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/animal_sale.hpp"

#include "simulation/animals.hpp"
#include "simulation/park/finance.hpp"

#include <cmath>

namespace rct3::simulation
{

    units::Money sale_price(units::Money buy_price, units::Share health)
    {
        return { std::lround(static_cast<f32>(buy_price.value) / 0.75f * health.value) };
    }

    std::optional<units::Money> sell_animal(entt::registry& r, entt::entity animal, units::Money buy_price, park::ParkState& park, save::model::Finance& finance, std::optional<i64>& kept)
    {
        const Animal* sold = r.valid(animal) ? r.try_get<Animal>(animal) : nullptr;
        if (sold == nullptr || sold->job == AnimalJob::Crated)
            return std::nullopt;
        const units::Money price = sale_price(buy_price, sold->health);
        (void)park::book(park, finance, kept, units::Money { -price.value }, park::LedgerLine::AnimalPurchaseSale, park::Charge::Always, sold->species);
        // Its young, its mate and whoever was after it find it gone, as when a predator takes one.
        r.destroy(animal);
        return price;
    }

} // namespace rct3::simulation
