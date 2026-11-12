// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ride improvements: families of special track pieces (loops, corkscrews, launches...) that research invents
// for the ride types a park already has. Until its improvement is invented, a piece of the family cannot be
// built; tracks already built keep theirs. The original game has nothing like it: it offers every piece of a
// ride type at once.
//
// The families are data: assets/simulation/ride_improvements.json, an object whose "improvements" list gives each
// one an id, the string key of its name, its research cost in hundredths and the words that mark its pieces. A
// piece belongs to a family when it is picked by name and its name holds one of the words, in any case.
#pragma once

#include "park/track/pieces.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::park::track
{

    struct Improvement
    {
        std::string id { };                  ///< Names it in saves and scripts
        std::string name { };                ///< String key of its name
        std::int64_t cost = 0;               ///< Hundredths research spends to invent it
        std::vector<std::string> pieces { }; ///< Words of the names of the pieces it unlocks, in lower case
    };

    /// The improvements a table in the shape of ride_improvements.json lists, in its order; an error naming the
    /// first entry that is not a well-formed improvement, or an id given twice.
    [[nodiscard]] std::expected<std::vector<Improvement>, std::string> parse_improvements(std::string_view json);

    /// The table built into the program.
    // shortcut: only the built-in table; a mod's table added to it waits for loading mods.
    [[nodiscard]] std::span<const Improvement> builtin_improvements();

    /// The first improvement of `table` that is `locked` (by id) and keeps `shape` from being built. Stations and
    /// pieces picked by curve, slope and bank never wait for one.
    [[nodiscard]] std::optional<const Improvement&> locking_improvement(std::span<const Improvement> table, std::span<const std::string> locked, const PieceShape& shape);

} // namespace rct3::park::track
