// SPDX-License-Identifier: GPL-3.0-or-later
//
// The row of large buttons along the bottom of the park view. Unlike the game, which stacks them up the left
// edge, the row sits in the middle of the bottom edge and stays there as the window changes size. From the
// left: delete, land, paths, scenery, shops, rides and park management, then after half a step's gap the
// button to leave, files and options, the view controls and the maps. Each shows its icon and, as help, the
// string named after the icon with "Help" added.
//
// The row keeps a fixed step between buttons and packs them closer on narrow screens, so all of them always
// fit. The owner hears of a press through take_pressed() and decides what it opens.
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

    /// The row's buttons, left to right.
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

    struct ToolRowStyle
    {
        /// Each button's icon in the order of ParkTool; the leave button's is the one to the main menu.
        std::array<std::string, kParkToolCount> icons;
        std::string career_leave_icon; ///< The leave button's icon in a career
        float swell = 2;               ///< Units a button grows by under the pointer (ours)
    };

    namespace tool_row
    {
        inline constexpr float kMargin = 8;      ///< From the screen's bottom edge, and the least from its sides
        inline constexpr float kButtonSize = 40; ///< And the step between buttons on wide screens
    } // namespace tool_row

    /// The step between buttons on a screen `width` units wide.
    [[nodiscard]] float tool_row_step(float width);

    class ToolRow : public Widget
    {
    public:
        ToolRow(std::string name, ToolRowStyle style, const StringTable& strings);

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
        void place(float width);

        ToolRowStyle style_;
        const StringTable& strings_;
        std::array<Button*, kParkToolCount> buttons_ { };
        std::optional<ParkTool> pressed_;
        float placed_for_ = -1; ///< The width the buttons were last placed for
    };

} // namespace rct3::ui
