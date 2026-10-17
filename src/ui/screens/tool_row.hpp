// SPDX-License-Identifier: GPL-3.0-or-later
//
// The row of large buttons along the bottom of the park view. Unlike the game, which stacks them up the left
// edge, the row sits in the middle of the bottom edge and stays there as the window changes size. From the
// left: delete, land, paths, scenery, shops, rides and park management, then after half a step's gap the
// button to leave, files and options, the view controls and the maps. Each shows its icon and, as help, the
// string named after the icon with "Help" added.
//
// The row keeps a fixed step between buttons and packs them closer on narrow screens, so all of them always
// fit. Most buttons open a menu in a row above them (see tool_menus.hpp): one menu is open at a time, pressing its
// button again closes it, and pressing another button opens that one's instead. Picking an item leaves the
// menu open. The owner hears of a press of a button without a menu through take_pressed(), of an item
// picked through take_item(), and decides what they do.
#pragma once

#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui
{

    enum class ToolItem : std::uint8_t;

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
        std::string item_face;         ///< Behind each menu item's icon
        float item_swell = 6;          ///< Units a menu item grows by under the pointer
    };

    namespace tool_row
    {
        inline constexpr float kMargin = 8;      ///< From the screen's bottom edge, and the least from its sides
        inline constexpr float kButtonSize = 40; ///< And the step between buttons on wide screens
        inline constexpr float kItemWidth = 32;  ///< Of a menu item's face
        inline constexpr float kItemHeight = 33;
        inline constexpr float kItemStep = 40; ///< From one menu item to the next
        inline constexpr float kItemGap = 7;   ///< Between the tool button and its menu's nearest item
        inline constexpr float kItemIconScale = 0.7f;
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
        /// The item picked from a menu since the last call.
        [[nodiscard]] std::optional<ToolItem> take_item()
        {
            return std::exchange(picked_, std::nullopt);
        }
        /// The tool whose menu is open, if any.
        [[nodiscard]] std::optional<ParkTool> open_menu() const
        {
            return open_;
        }
        /// Closes the open menu.
        void close_menu();
        /// The button of `item`.
        [[nodiscard]] Button& item(ToolItem item) const;
        /// Whether `item` can be picked; one that cannot is shown faded.
        void set_available(ToolItem item, bool available);
        /// Whether what the switch `item` controls is shown. Picking a switch turns it over.
        [[nodiscard]] bool switched_on(ToolItem item) const;
        void set_switch(ToolItem item, bool shown);

        void update(const UpdateContext& context) override;

    private:
        struct MenuItem
        {
            ToolItem item;
            Button* button = nullptr;
            ParkTool tool;
            bool on = false; ///< Switches only
        };

        void place(float width);
        void place_menu(ParkTool tool);
        void show_switch(MenuItem& item);
        [[nodiscard]] MenuItem& find(ToolItem item);
        [[nodiscard]] const MenuItem& find(ToolItem item) const;

        ToolRowStyle style_;
        const StringTable& strings_;
        std::array<Button*, kParkToolCount> buttons_ { };
        std::vector<MenuItem> items_;
        std::optional<ParkTool> open_;
        std::optional<ParkTool> pressed_;
        std::optional<ToolItem> picked_;
        float placed_for_ = -1; ///< The width the buttons were last placed for
    };

} // namespace rct3::ui
