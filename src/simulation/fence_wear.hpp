// SPDX-License-Identifier: GPL-3.0-or-later
//
// Enclosure fences wearing out. The fence of an enclosure with animals in it takes a knock now and then, from age,
// and far more often from animals of a species that needs a stronger fence than it is: the stronger they are
// against it, and the more of them, the harder they push. A knock damages the panels of one of its tiles, or
// breaks panels already damaged, leaving a gap. Only straight panels wear (park::panelled). Free mechanics mend
// them, broken ones first, from a path near the fence. The player is told when an enclosure's fence breaks, reminded
// while it stays broken, and told once it is mended.
#pragma once

#include "core/function_ref.hpp"
#include "core/types.hpp"
#include "random/generator.hpp"
#include "save/model/world.hpp"
#include "simulation/park/messages.hpp"
#include "units/units.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace rct3::simulation
{

    struct SimWorld;
    struct AnimalGround;

    inline constexpr std::string_view kFenceBroken = "EnclosureMaintenance_Broken";
    inline constexpr std::string_view kFenceStillBroken = "EnclosureMaintenance_StillBroken";
    inline constexpr std::string_view kFenceFixed = "EnclosureMaintenance_Fixed";

    /// Values for how fences wear.
    struct FenceWearRates
    {
        units::Seconds ageing { 3600 }; ///< Between knocks from age alone, on average, to an enclosure with animals in it
        /// Between knocks from one animal whose species needs a fence one step stronger than its enclosure's, on average;
        /// one two steps too strong knocks twice as often.
        units::Seconds pushing { 300 };
        units::Seconds reminder { 300 }; ///< Between reminders while a fence stays broken
    };

    inline constexpr FenceWearRates kFenceWearRates { };

    /// How strong something is by its symbol: for a species, the least strength of fence that holds it; for a kind of
    /// fence, its own.
    using Strengths = std::function_ref<f32(std::string_view symbol)>;

    /// The park's fences as they wear, kept in the registry's context.
    struct FenceUpkeep
    {
        random::RandomState dice { };
        units::Seconds since_check { };
        units::Seconds since_told { };
        /// By enclosure, whether its fence was broken when last looked at; none before the first look, so a park
        /// loaded with a broken fence is not told of it as new.
        std::optional<std::vector<bool>> gapped { };
    };

    /// Wears the fences of `map` by `dt`, the animals of `world` pushing on them, sends its mechanics to mend them and
    /// tells `messages` of fences broken and mended, pointing out where over `ground`. Returns whether the fence of any
    /// tile changed.
    bool step_fence_wear(SimWorld& world, save::model::EnclosureMap& map, const AnimalGround& ground, Strengths strength, units::Seconds dt, park::ParkMessages& messages);

    /// A tile of enclosure `which` whose fence is broken: where its animals could get out. None while it is whole.
    [[nodiscard]] std::optional<save::model::Tile> fence_gap(const save::model::EnclosureMap& map, i32 which);

    /// Knocks the fence of a tile of enclosure `which` picked with `dice` one step worse: damaged, or broken once
    /// damaged. Returns the tile; none when the enclosure has no straight panels left to wear.
    std::optional<save::model::Tile> knock_fence(save::model::EnclosureMap& map, i32 which, random::RandomState& dice);

    /// Knocks the fences of `map` `count` times, each time those of an enclosure picked at random. Returns how many
    /// knocks found a panel to wear.
    usize knock_fences(SimWorld& world, save::model::EnclosureMap& map, usize count);

} // namespace rct3::simulation
