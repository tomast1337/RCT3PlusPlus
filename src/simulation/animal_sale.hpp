// SPDX-License-Identifier: GPL-3.0-or-later
//
// Selling a zoo animal: it fetches what one of its kind (species, age and sex) costs to buy, by its health, with a
// third on top, so rarer kinds fetch more and a healthy animal sells at a profit. The money comes in on the
// animal purchase/sale line, for the park and for its species, and the animal leaves the park.
#pragma once

#include "core/types.hpp"
#include "save/model/park.hpp"
#include "simulation/entt.hpp"
#include "simulation/park/state.hpp"
#include "units/units.hpp"

#include <optional>

namespace rct3::simulation
{

    /// What an animal in `health` fetches, when one of its kind costs `buy_price`.
    [[nodiscard]] units::Money sale_price(units::Money buy_price, units::Share health);

    /// Sells `animal` of `r`, one of whose kind costs `buy_price`: books its sale_price coming in on the animal
    /// purchase/sale line of `finance` (see park::book) and takes it out of the park. One still in its crate is not
    /// sold, and nothing comes back.
    std::optional<units::Money> sell_animal(entt::registry& r, entt::entity animal, units::Money buy_price, park::ParkState& park, save::model::Finance& finance, std::optional<i64>& kept);

} // namespace rct3::simulation
