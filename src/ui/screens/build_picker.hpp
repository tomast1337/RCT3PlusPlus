// SPDX-License-Identifier: GPL-3.0-or-later
//
// The list that opens beside the build menus' buttons: the rides, shops or scenery of one kind, a row
// each, to pick one to build. A blue panel with close and fold buttons in its top-left corner, one column
// of rows that scrolls, and along the bottom a button that shows the rows large or small.
//
// A large row shows the item's icon, its name and, under it, how it sits on the grid or what building it
// starts, and its price; a small row shows the icon small and the name. Items of one set, such as the
// pieces of a building, share a heading row that opens to show them; headings start closed. Rows are in
// the order of their names. Resting the pointer on a row tells its name and price.
//
// Picking a row marks it; the owner asks for it with take_picked(). The list knows nothing of the items:
// the owner hands it rows (build_rows.hpp makes them) and gets back the number it gave the row picked.
#pragma once

#include "ui/string_table.hpp"
#include "ui/widget.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/scroll_area.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui
{

    namespace build_picker
    {
        /// The panel's size and the parts round the list.
        inline constexpr float kWidth = 300;
        inline constexpr float kHeight = 340;
        inline constexpr float kTitleHeight = 22; ///< The band that drags the panel, with its buttons
        inline constexpr float kSide = 6;
        inline constexpr float kBottomBar = 34;
        inline constexpr float kTitleButton = 16;
        inline constexpr Point kCloseAt { 3, 3 };
        inline constexpr Point kFoldAt { 19, 3 };

        /// Rows, large and small.
        inline constexpr float kLargeRow = 70;
        inline constexpr float kSmallRow = 18;
        inline constexpr float kLargeIcon = 64;
        inline constexpr float kSmallIcon = 14;
        inline constexpr float kLargeBadge = 26;
        inline constexpr float kSmallBadge = 16;
        inline constexpr float kNameBand = 32; ///< The name's part of a large row, above its two lines
        inline constexpr float kLine = 16;
        inline constexpr float kIndent = 12; ///< Of rows under a heading
    } // namespace build_picker

    /// One row as the list shows it.
    struct PickerRow
    {
        std::u16string name;
        std::string icon;                ///< Skin
        std::u16string size;             ///< First line under the name of a large row
        std::u16string price;            ///< Second line; empty when the item has no price
        std::string badge;               ///< Skin in the right-hand corner, if any
        std::size_t item = 0;            ///< The owner's number for the item
        int rank = 0;                    ///< Rows sort by this first, then by name
        std::vector<PickerRow> children; ///< A heading's rows; a row with any is a heading
    };

    struct BuildPickerStyle
    {
        std::string background;   ///< Stretched over the panel
        std::string button_strip; ///< Behind the title buttons, at its own size
        std::string title_button; ///< Family
        std::string close_icon;
        std::string fold_icon;
        std::string text_button;  ///< Family of the expand button
        std::string open_heading; ///< On an open heading
        std::string closed_heading;
        ScrollBarStyle scroll_bar;
        Color text_colour = hex("#ffffff");
        Color button_text_colour = hex("#000000");
        Color hover_colour = hex("#ffffff28");
        Color picked_colour = hex("#ffffff50");
    };

    class BuildPickerRow;

    class BuildPicker : public Widget
    {
    public:
        /// `large`: rows start large.
        BuildPicker(std::string name, BuildPickerStyle style, const StringTable& strings, std::u16string title, std::vector<PickerRow> rows, bool large);

        /// The item of each row picked since the last call, the last one.
        [[nodiscard]] std::optional<std::size_t> take_picked();
        [[nodiscard]] std::optional<std::size_t> picked() const
        {
            return picked_;
        }
        [[nodiscard]] bool large() const
        {
            return large_;
        }
        [[nodiscard]] bool folded() const
        {
            return folded_;
        }
        /// A line on the bottom bar beside the button, e.g. to say what the list leaves out.
        std::u16string note;

        // For tests and owners that drive the list.
        [[nodiscard]] std::span<BuildPickerRow* const> rows() const
        {
            return rows_;
        }
        [[nodiscard]] Button& size_button() const
        {
            return *size_;
        }
        [[nodiscard]] Button& fold_button() const
        {
            return *fold_;
        }
        [[nodiscard]] Button& close_button() const
        {
            return *close_;
        }

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        /// Moving while dragged by its title, as a window does.
        [[nodiscard]] CursorKind held_cursor(Point p) const override
        {
            return grab_ ? CursorKind::Move : cursor(p);
        }
        void on_press(Point p) override;
        void on_drag(Point p) override;
        void on_release(Point p, bool inside) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        void add_rows(const std::vector<PickerRow>& rows, BuildPickerRow* heading);
        void lay_out();

        BuildPickerStyle style_;
        const StringTable& strings_;
        std::u16string title_;
        std::vector<PickerRow> data_;
        std::vector<BuildPickerRow*> rows_;
        ScrollArea* list_ = nullptr;
        Button* close_ = nullptr;
        Button* fold_ = nullptr;
        Button* size_ = nullptr;
        bool large_ = true;
        bool folded_ = false;
        bool dirty_ = true;
        std::optional<Point> grab_;
        std::optional<std::size_t> picked_;
        std::optional<std::size_t> picked_out_;
    };

    /// A row of the list.
    class BuildPickerRow : public Widget
    {
    public:
        BuildPickerRow(std::string name, const BuildPickerStyle& style, const PickerRow& row, BuildPickerRow* heading);

        [[nodiscard]] const PickerRow& row() const
        {
            return row_;
        }
        [[nodiscard]] BuildPickerRow* heading() const
        {
            return heading_;
        }
        [[nodiscard]] bool is_heading() const
        {
            return !row_.children.empty();
        }
        bool open = false;   ///< Headings: their rows show
        bool picked = false; ///< Drawn marked
        bool large = true;

        /// True once for each click since the last call.
        [[nodiscard]] bool take_click()
        {
            return std::exchange(clicked_, false);
        }

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool clicks() const override
        {
            return true;
        }
        void on_release(Point p, bool inside) override;
        void draw(DrawContext& context) const override;

    private:
        const BuildPickerStyle& style_;
        const PickerRow& row_;
        BuildPickerRow* heading_ = nullptr;
        bool clicked_ = false;
    };

} // namespace rct3::ui
