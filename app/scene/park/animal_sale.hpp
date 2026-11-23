// SPDX-License-Identifier: GPL-3.0-or-later
//
// Selling a zoo animal from its window: the park asks first, naming what it fetches (simulation/animal_sale.hpp),
// and on a yes the money comes in, the animal leaves, its window closes and a box says it was sold.
#pragma once

#include "simulation/animals.hpp"
#include "simulation/entt.hpp"
#include "ui/interface_assets.hpp"
#include "ui/money_text.hpp"
#include "units/units.hpp"

#include <optional>
#include <string_view>

namespace rct3::scene::park
{

    struct ParkScene;

    inline constexpr std::string_view kSellAnimalName = "sell_animal_question";

    /// What one of `animal`'s kind (its species, age and sex) costs to buy, from the build catalogue; nothing
    /// before the catalogue is read, or for a kind it does not list.
    [[nodiscard]] std::optional<units::Money> animal_buy_price(const ParkScene& scene, const simulation::Animal& animal);

    /// Asks whether to sell `animal`, naming what it fetches; the answer comes back in the park's update.
    void ask_sell_animal(ParkScene& scene, const ui::InterfaceAssets& assets, const ui::Currency& currency, entt::entity animal);

    /// Sells `animal` (simulation::sell_animal), closes its window and tells the player in a box; nothing comes
    /// back for one that may not be sold, or whose kind the catalogue does not price.
    std::optional<units::Money> sell_animal(ParkScene& scene, const ui::InterfaceAssets& assets, entt::entity animal);

} // namespace rct3::scene::park
