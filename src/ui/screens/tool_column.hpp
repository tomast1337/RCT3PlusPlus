// SPDX-License-Identifier: GPL-3.0-or-later
//
// The column of large buttons up the left edge of the park view, as the game shows it: from the bottom up
// delete, land, paths, scenery, shops, rides and park management, then after half a step's gap the button
// to leave, files and options, the view controls and the maps. Each shows its icon and, as help, the string
// named after the icon with "Help" added.
//
// The column keeps a fixed step between buttons on tall screens and packs them closer on short ones, so all
// of them always fit. The owner hears of a press through take_pressed() and decides what it opens.
#pragma once

#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace rct3::ui
{

    /// The column's buttons, bottom to top.
    enum class ParkTool : std::uint8_t
    {
        Delete,
        Land,
        Paths,
        Scenery,
        Shops,
        Rides,
        Management,
        Leave, ///< Quit to the main menu, or back to the career screen in a career
        Files,
        View,
        Map,
    };
    inline constexpr std::size_t kParkToolCount = 11;

    struct ToolColumnStyle
    {
        /// Each button's icon in the order of ParkTool; the leave button's is the one to the main menu.
        std::array<std::string, kParkToolCount> icons;
        std::string career_leave_icon; ///< The leave button's icon in a career
        float swell = 2;               ///< Units a button grows by under the pointer (ours)
    };

    namespace tool_column
    {
        inline constexpr float kMargin = 8;      ///< From the screen's left and bottom edges to the first button
        inline constexpr float kButtonSize = 40; ///< And the step between buttons on tall screens
    } // namespace tool_column

    /// The step between buttons on a screen `height` units tall.
    [[nodiscard]] float tool_column_step(float height);

    class ToolColumn : public Widget
    {
    public:
        ToolColumn(std::string name, ToolColumnStyle style, const StringTable& strings);

        /// In a career the leave button goes back to the career screen instead of the main menu.
        void set_career(bool career);

        [[nodiscard]] Button& button(ParkTool tool) const
        {
            return *buttons_.at(static_cast<std::size_t>(tool));
        }
        [[nodiscard]] std::optional<ParkTool> take_pressed()
        {
            return std::exchange(pressed_, std::nullopt);
        }

        void update(const UpdateContext& context) override;

    private:
        void place(float height);

        ToolColumnStyle style_;
        const StringTable& strings_;
        std::array<Button*, kParkToolCount> buttons_ { };
        std::optional<ParkTool> pressed_;
        float placed_for_ = -1; ///< The parent height the buttons were last placed for
    };

} // namespace rct3::ui
