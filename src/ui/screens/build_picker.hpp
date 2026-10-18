// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window that opens beside the build menus' buttons: the rides, shops or scenery of one kind, to pick one
// to build. It has the frame of the options window: the menu's icon in the corner and its name as the title.
// Under the title a search box; under that a row of filters; then the items as a grid of icons that scrolls,
// filling as many columns as fit. The window resizes from its edges and the grid reflows.
//
// Resting the pointer on an item tells its name, how it sits on the grid or what building it starts, and
// its price. Items dearer than the cash the owner hands the window are shaded red. Items of one set, such as
// the pieces of a building, follow a heading that opens to show them; headings start closed. Items are in
// the order of their names.
//
// - The search box keeps only the items whose names hold what was typed, ignoring case and accents; a
//   heading stays, open, while any of its items match. Escape empties the box, then closes the window.
// - Object filters, one for each kind of item (steel coasters, lamps, roofs) when there are more than one:
//   a kind's items show while its filter is on and hide while it is off. Items of no kind always show.
// - Theme filters, one for each theme the owner hands the window, in its order, when it hands more than one:
//   a theme's items show while its filter is on (in colour) and hide while it is off (grey), whether the
//   window holds any or not. Items of no theme always show.
// - Pack filters, one for each add-on pack the items come from: while any is on, only the items of the packs
//   that are on show.
//
// Picking an item marks it; the owner asks for it with take_picked(). The window knows nothing of the items:
// the owner hands it rows (build_rows.hpp makes them) and gets back the number it gave the row picked.
#pragma once

#include "ui/string_table.hpp"
#include "ui/widget.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/edit_box.hpp"
#include "ui/widgets/scroll_area.hpp"
#include "ui/widgets/window.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui
{

    namespace build_picker
    {
        /// The window's size when first opened, and the parts above the grid. Our own values.
        inline constexpr float kWidth = 380;
        inline constexpr float kHeight = 400;
        inline constexpr float kCaptionX = 59;  ///< Right of the window's icon, under the title
        inline constexpr float kBarHeight = 20; ///< Of the search box and of the filters' row
        inline constexpr float kGap = 4;
        inline constexpr float kFilter = 20;  ///< Width and height of a filter
        inline constexpr float kGroupGap = 8; ///< Between the object, theme and pack filters
        inline constexpr float kNote = 16;    ///< Under the grid, while there is a note

        /// The grid.
        inline constexpr float kTile = 72;
        inline constexpr float kTileIcon = 64;
        inline constexpr float kBadge = 16; ///< In a tile's top-right corner
    } // namespace build_picker

    /// One item, or a heading over a set of them, as the window shows it.
    struct PickerRow
    {
        std::u16string name;
        std::string icon;                ///< Skin
        std::u16string size;             ///< Second line of the help, under the name
        std::u16string price;            ///< Third line; empty when the item has no price
        std::int64_t cost = 0;           ///< The price in hundredths of the currency unit
        std::string badge;               ///< Skin in the right-hand corner, if any
        std::string style;               ///< The theme's name; empty for items of no theme
        std::uint32_t addon = 0;         ///< 0 for the base game, else its add-on pack
        std::string kind;                ///< Its object filter's name ("Coaster_Steel"); empty for none
        std::size_t item = 0;            ///< The owner's number for the item
        int rank = 0;                    ///< Rows sort by this first, then by name
        std::vector<PickerRow> children; ///< A heading's rows; a row with any is a heading
    };

    /// A theme the window offers a filter for.
    struct PickerTheme
    {
        std::string name;     ///< As rows name it, and its filter's skins
        std::u16string title; ///< As the player reads it
    };

    struct BuildPickerStyle
    {
        WindowStyle window;
        std::string open_heading; ///< On an open heading
        std::string closed_heading;
        std::string search_box; ///< Behind the search box, and while it has the keyboard focus
        std::string search_box_focused;
        std::string object_filter;     ///< + an object filter's name: its skin while on
        std::string object_filter_off; ///< ... and while off
        std::string object_help;       ///< + an object filter's name: the string key of its help
        std::string style_filter;      ///< + the theme's name: the skin of its filter while on
        std::string style_filter_off;  ///< ... and while off
        std::string pack_filter;       ///< + the pack's number from 0: the picture of its filter
        float pack_icon_scale = 0.5f;  ///< The pictures are badges drawn larger elsewhere
        std::string pack_name;         ///< + the pack's number from 0: the string key of its name
        std::string style_help;        ///< String key of the theme filters' help, under the theme's name
        ScrollBarStyle scroll_bar;
        Color text_colour = hex("#000000");
        Color hint_colour = hex("#7f7f7f");
        Color hover_colour = hex("#2484be30");
        Color picked_colour = hex("#2484be60");
        Color pack_on_colour = hex("#2484be90"); ///< Behind a pack filter that is on
        Color dear_colour = hex("#d0101070");    ///< Over items the park cannot pay for
    };

    class BuildPickerRow;

    class BuildPicker : public Window
    {
    public:
        /// `icon`: the skin in the window's corner. `themes`: those to offer filters for.
        BuildPicker(std::string name, BuildPickerStyle style, const StringTable& strings, std::u16string title, std::string icon, std::vector<PickerRow> rows, std::vector<PickerTheme> themes = { });

        /// The item of each row picked since the last call, the last one.
        [[nodiscard]] std::optional<std::size_t> take_picked();
        [[nodiscard]] std::optional<std::size_t> picked() const
        {
            return picked_;
        }
        /// What the park can pay; items dearer are shaded. None: money never runs out.
        std::optional<std::int64_t> cash;
        /// A line under the grid, e.g. to say what the window leaves out.
        std::u16string note;
        /// The kinds and themes whose filters are off, by name, and the packs whose filters are on. The owner
        /// may set them before the first update to restore them.
        std::set<std::string, std::less<>> kinds_off;
        std::set<std::string, std::less<>> styles_off;
        std::set<std::uint32_t> packs_only;

        // For tests and owners that drive the window.
        [[nodiscard]] std::span<BuildPickerRow* const> rows() const
        {
            return rows_;
        }
        [[nodiscard]] EditBox& search_box() const
        {
            return *search_;
        }
        /// The object filters, the theme filters, then the pack filters; named after the kind, the theme, and
        /// "pack" + the pack's number.
        [[nodiscard]] std::span<Button* const> filters() const
        {
            return filters_;
        }

        bool on_key(const KeyPress& key) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        void add_rows(const std::vector<PickerRow>& rows, BuildPickerRow* heading);
        void add_filters(const std::vector<PickerTheme>& themes);
        /// Whether the filters and the search let `row` show; a heading while any of its rows do.
        [[nodiscard]] bool shows(const PickerRow& row) const;
        void lay_out();

        BuildPickerStyle style_;
        const StringTable& strings_;
        std::vector<PickerRow> data_;
        std::vector<BuildPickerRow*> rows_;
        struct FilterKey
        {
            enum class Group : std::uint8_t
            {
                Object,
                Theme,
                Pack,
            };
            Group group = Group::Object;
            std::string name; ///< Of the kind or the theme
            std::uint32_t pack = 0;
        };
        std::vector<Button*> filters_;
        std::vector<FilterKey> keys_; ///< Of each filter
        EditBox* search_ = nullptr;
        ScrollArea* list_ = nullptr;
        std::u16string query_; ///< Folded, as the layout last filtered by
        bool dirty_ = true;
        float laid_width_ = 0; ///< Of the grid when it was laid out
        bool laid_note_ = false;
        std::optional<std::size_t> picked_;
        std::optional<std::size_t> picked_out_;
    };

    /// An item of the grid, or a heading.
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
        bool dear = false;   ///< Costs more than the park can pay

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
