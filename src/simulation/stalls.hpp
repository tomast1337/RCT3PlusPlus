// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stalls: shops and facilities, what each sells or does for its guests, and the step that wears them down
// with time. The components here, and that step.
#pragma once

#include "math/glm.hpp"
#include "profiling/profiler.hpp"
#include "simulation/units.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rct3::simulation
{

    struct SimWorld;

    /// What a shop sells, or what a facility does for its guests.
    enum class Product : std::uint8_t
    {
        Drink,
        Food,
        Toilet,      ///< Relieves the bladder
        CashMachine, ///< Gives the guest more cash
        FirstAid,    ///< Settles a sick guest's stomach
        Umbrella,    ///< Keeps the rain off
    };

    /// Whether a shop's food or drink is cold (ice cream, a slush), hot (coffee, soup) or neither.
    enum class Serve : std::uint8_t
    {
        Any,
        Cold, ///< Cools a guest down; wanted most on hot days
        Hot,  ///< Wanted most on cold days
    };

    /// One thing a stall sells.
    struct Ware
    {
        Product product = Product::Drink;
        Serve serve = Serve::Any;
        Money price { };
        std::string symbol { };  ///< Of the item a guest carries away, as `Burger:cid`; empty when not known
        std::uint8_t extras = 0; ///< How many extras (toppings, sauces, ice) its item can take
        std::uint8_t topped = 0; ///< How many of those go on it
        /// What those add to how much it feeds, quenches and sickens, each by how much of it goes on.
        std::array<float, 3> topping { };
        Money cost { }; ///< Of its stock, with the extras going on, to the park for each one sold
        /// Most a guest pays for it, with those extras, in mild weather, the cold and the heat; none when not known.
        std::array<Money, 3> worth { };
    };

    struct Shop
    {
        Product product = Product::Drink; ///< What it is visited for: its first ware's
        Serve serve = Serve::Any;
        /// From the middle of its entrance tile towards the stall, along x and z, one long; buyers stand at
        /// its counter that way. Zero when not known: they stand in the tile's middle.
        glm::vec2 counter { 0.0f };
        std::vector<Ware> menu { }; ///< Its food, drink and umbrellas; empty: one ware of `product` at its attraction's price
    };

    /// One tick of the stalls and cash machines: while open they wear, and break down, closing until a mechanic
    /// mends them. Times the phase on `phase`.
    void step_stalls(SimWorld& world, std::optional<profiling::Scope>& phase);

} // namespace rct3::simulation
