// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cheats set off by a guest's name: renaming a guest to a name the language files keep under one of
// kNameCheatKeys sets that cheat off, whatever the case of its letters. Some act once on the spot (money, the
// whole park cheering); others stay on in the park from then on, until it is loaded again, as they are not
// saved; a few last a short while. The guest camera and the money are the scene's to do (they need the view and
// the ledger); the rest act on the simulated park here. While the player has cheats off, a cheat's name is only a
// name.
#pragma once

#include "core/types.hpp"
#include "units/units.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace rct3::simulation
{

    struct SimWorld;

    enum class NameCheat : u8
    {
        Money,          ///< Cash for the park, booked as other income
        Cheer,          ///< Every guest jumps for joy
        Laugh,          ///< Every guest laughs
        Retch,          ///< Every guest retches, but no one is really sick
        GuestCamera,    ///< The view goes with the renamed guest, from their eyes
        Unbreakable,    ///< Rides no longer wear or break down
        RideAllFlat,    ///< Guests try any ride off track, however wild or tame for them
        RideAllTracked, ///< And any ride on track
        BigBlasts,      ///< Crashed trains blow up far wider and harder
        Hurry,          ///< Guests go about at a run for a while
        Nervous,        ///< Guests keep looking round behind them for a while
        Photos,         ///< Guests keep stopping to take photos for a while
        Nothing,        ///< A name the game knows that changes nothing there either
        NotYet,         ///< A cheat whose system rct3++ does not have yet: the player is told so
    };

    /// The language key holding the name that sets each cheat off.
    struct NameCheatKey
    {
        std::string_view key;
        NameCheat cheat;
    };
    inline constexpr std::array kNameCheatKeys {
        NameCheatKey { "CheatPeepName_Rockefeller", NameCheat::Money },
        NameCheatKey { "CheatPeepName_Sawyer", NameCheat::Cheer },
        NameCheatKey { "CheatPeepName_Empty", NameCheat::Laugh },
        NameCheatKey { "CheatPeepName_Sick", NameCheat::Retch },
        NameCheatKey { "CheatPeepName_PeepCam", NameCheat::GuestCamera },
        NameCheatKey { "CheatPeepName_Unbreakable", NameCheat::Unbreakable },
        NameCheatKey { "CheatPeepName_RideAllRides", NameCheat::RideAllFlat },
        NameCheatKey { "CheatPeepName_RideAllCoasters", NameCheat::RideAllTracked },
        NameCheatKey { "CheatPeepName_BigExplosion", NameCheat::BigBlasts },
        NameCheatKey { "CheatPeepName_Ati", NameCheat::Hurry },
        NameCheatKey { "CheatPeepName_Hitchcock", NameCheat::Nervous },
        NameCheatKey { "CheatPeepName_Photostory", NameCheat::Photos },
        NameCheatKey { "CheatPeepName_AllGroupsIndividuals", NameCheat::Nothing },
        NameCheatKey { "CheatPeepName_CoasterHeights", NameCheat::NotYet }, ///< Lifts the track height limit; rct3++ has none yet
        NameCheatKey { "CheatPeepName_Fawkes", NameCheat::NotYet },         ///< Unlocks the firework display designer
        NameCheatKey { "CheatPeepName_Moss", NameCheat::NotYet },           ///< A buggy the player drives
        NameCheatKey { "CheatPeepName_Lean", NameCheat::NotYet },           ///< Opens the flying camera editor
        NameCheatKey { "CheatPeepName_Mouse", NameCheat::NotYet },          ///< Guests stop to stare at the pointer a while
        NameCheatKey { "CheatPeepName_FunnyDarts", NameCheat::NotYet },     ///< Safari darts drawn as a dinosaur
    };

    /// Whether the player may use cheats, as their options say.
    enum class Cheats : u8
    {
        Allowed,
        Off,
    };

    /// The cheat a guest renamed `typed` sets off: the one whose name, as `name_of` gives it for its key, matches
    /// whatever the case of its letters. None for any other name, and for every name while cheats are off.
    [[nodiscard]] std::optional<NameCheatKey> named_cheat(std::string_view typed, Cheats cheats, std::function_ref<std::string(std::string_view key)> name_of);

    /// The money the cheat gives: 10,000.00, as the game gives it.
    inline constexpr units::Money kCheatMoney { 1'000'000 };
    inline constexpr f32 kBlastReach = 4;            ///< Chosen by eye: a big blast knocks over guests across several paths
    inline constexpr f32 kBlastPush = 2;             ///< Chosen by eye: and throws them well clear
    inline constexpr f32 kHurryPace = 2;             ///< Chosen by eye: guests in a hurry go about twice as fast
    inline constexpr units::Seconds kHurry { 30 };   ///< Chosen by eye: long enough to watch the crowd rush about
    inline constexpr units::Seconds kSpell { 30 };   ///< Chosen by eye: how long guests stay nervous, or keep taking photos
    inline constexpr units::Seconds kSpellGap { 8 }; ///< Chosen by eye: a guest's average wait between looks round or photos

    /// The cheats on in the park, kept with the simulated park and lost with it (not saved).
    struct NameCheats
    {
        bool unbreakable = false;
        bool ride_all_flat = false;
        bool ride_all_tracked = false;
        bool big_blasts = false;
        units::Seconds hurry { };   ///< Left of guests' hurry
        units::Seconds nervous { }; ///< Left of guests looking round behind them
        units::Seconds photos { };  ///< Left of guests taking photos
    };

    /// The message telling the player `cheat` is on, for those with nothing to see at once; empty for the rest.
    [[nodiscard]] std::string_view set_off_message(NameCheat cheat) noexcept;

    /// Sets `cheat` off in `world`; Money and GuestCamera are the scene's and do nothing here. Returns how many
    /// guests it acted on for those that act on guests at once.
    usize set_off(SimWorld& world, NameCheat cheat);

} // namespace rct3::simulation
