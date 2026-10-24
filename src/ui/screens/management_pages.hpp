// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the park's management windows show: for each window its icon, name, size and tabs, and the page of each
// tab written from the park as it stands (its saved model, its running state, its people and its attractions).
//
// Nothing is made up. A value the park does not keep, or that only the simulation would work out, shows as a
// dash; a page whose content needs what is not built yet says so in its note.
#pragma once

#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "simulation/park_state.hpp"
#include "ui/screens/attraction_info.hpp"
#include "ui/screens/management_window.hpp"
#include "ui/screens/tool_menus.hpp"
#include "ui/string_table.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ui
{

    /// The management windows, in the order the menu offers them from its button outwards, most used first.
    enum class ManagementTopic : std::uint8_t
    {
        ParkOperations,
        Finances,
        Staff,
        Guests,
        Attractions,
        Objectives,
        Research,
        Challenges,
        Count_,
    };
    inline constexpr std::size_t kManagementTopicCount = static_cast<std::size_t>(ManagementTopic::Count_);

    /// The window the management menu's `item` opens; none for other items.
    [[nodiscard]] std::optional<ManagementTopic> management_topic(ToolItem item);

    struct ManagementLayout
    {
        std::string_view icon;  ///< Skin in the frame's corner: the menu item's
        std::string_view title; ///< String key
        float width = 0;        ///< With the frame
        float height = 0;
    };

    [[nodiscard]] ManagementLayout management_layout(ManagementTopic topic);

    /// The tabs of `topic`'s window, with their names.
    [[nodiscard]] std::vector<ManagementTab> management_tabs(ManagementTopic topic, const StringTable& strings);

    /// What the pages are written from. Any of it may be missing; its values then show as dashes.
    struct ManagementFacts
    {
        const save::ParkModel* park = nullptr; ///< As saved
        const simulation::ParkState* state = nullptr;
        const save::PeopleModel* people = nullptr;
        const save::RideModel* rides = nullptr;
        std::uint32_t broken_items = 0; ///< Path items vandals have broken
        std::int64_t replace_cost = 0;  ///< To replace them all at once, hundredths
    };

    /// The button on the park operations window's first page that replaces every broken path item at its full
    /// price, by its place among the page's buttons. Our own: the game has no way to mend them.
    inline constexpr std::size_t kReplaceBrokenButton = 4;

    /// The button on each page of the attractions window that puts every listed attraction in the state of the
    /// choice picked, by its place among the page's buttons, and those states by the place of their choice.
    inline constexpr std::size_t kOpenCloseAllButton = 0;
    inline constexpr std::array<save::AttractionState, 2> kOpenCloseAllStates = { save::AttractionState::Open, save::AttractionState::Closed };

    /// The page of tab `tab` of `topic`'s window.
    [[nodiscard]] ManagementPage management_page(ManagementTopic topic, std::size_t tab, const ManagementFacts& facts, const StringTable& strings, const InfoUnits& units);

    /// What a table row is about: a guest, a member of staff, a ride or a stall, by its index in the park's
    /// people or attractions. Rows carry it packed in their id.
    struct ManagementRow
    {
        enum class Kind : std::uint8_t
        {
            Guest,
            Staff,
            Ride,
            Stall,
            None, ///< A row about nothing that has an info panel
        };
        Kind kind = Kind::None;
        std::size_t index = 0;

        friend bool operator==(const ManagementRow&, const ManagementRow&) = default;
    };

    /// The park's name as shown: its text when the name is a string key, else the name with each "#ME#" standing
    /// for the scenario's name; the scenario's name when the park has none.
    [[nodiscard]] std::u16string park_name(const save::Park& park, const StringTable& strings);

    [[nodiscard]] std::size_t management_row_id(ManagementRow row);
    [[nodiscard]] ManagementRow management_row(std::size_t id);

} // namespace rct3::ui
