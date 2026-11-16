// SPDX-License-Identifier: GPL-3.0-or-later
//
// Guests changed by hand, from the developer windows: how they feel and what they need set, those levels held
// where they were set while the park runs on, and guests sent somewhere. Each change is made to the guest's own
// components, so a park saved afterwards keeps it.
#pragma once

#include "simulation/entt.hpp"
#include "units/units.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace rct3::simulation
{

    struct SimWorld;

    /// A level of a guest's Mood or Needs, 0 to 1.
    enum class GuestLevel : std::uint8_t
    {
        Happiness,
        Energy,
        Nausea,
        Wet,
        Heat,
        Hunger,
        Thirst,
        Bladder,
        Swim,
        Tolerance,
    };
    inline constexpr std::size_t kGuestLevels = 10;

    /// The level's name as the developer windows and scripts call it: "happiness", "nausea"...
    [[nodiscard]] std::string_view level_name(GuestLevel level);
    /// The level named `name`; none for a name of no level.
    [[nodiscard]] std::optional<GuestLevel> level_named(std::string_view name);

    /// Levels kept where they were set, whatever the park does to the guest, to try out how a guest in that state
    /// behaves. A developer's tool: not saved with the park.
    struct HeldLevels
    {
        std::array<std::optional<units::Share>, kGuestLevels> at { };
    };

    /// The guest's `level`; none for one without Mood and Needs, such as a guest gone home.
    [[nodiscard]] std::optional<units::Share> level_of(const entt::registry& r, entt::entity guest, GuestLevel level);

    /// Sets the guest's `level`, kept within 0..1, and holds it there when it is held. False for a guest without it.
    bool set_level(entt::registry& r, entt::entity guest, GuestLevel level, units::Share to);

    /// Holds the guest's `level` where it is now, or lets it go.
    void hold_level(entt::registry& r, entt::entity guest, GuestLevel level, bool held);

    [[nodiscard]] bool level_held(const entt::registry& r, entt::entity guest, GuestLevel level);

    /// Puts every held level back where it is held. Run at the end of each step.
    void keep_held_levels(entt::registry& r);

    /// Happy, rested and well: no sickness, nothing needed, dry and cool.
    void make_happy_and_healthy(entt::registry& r, entt::entity guest);

    void cure_nausea(entt::registry& r, entt::entity guest);

    /// Sends the guest's party (the guest alone when in none) to `target`: an attraction, or the park's gate to
    /// go home. Only a party walking the paths can be sent; false for one busy elsewhere, or where no path leads.
    bool send_to(SimWorld& world, entt::entity guest, entt::entity target);

    /// Sends the guest's party home through the park's first gate.
    bool send_home(SimWorld& world, entt::entity guest);

} // namespace rct3::simulation
