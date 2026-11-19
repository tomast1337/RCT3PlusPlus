// SPDX-License-Identifier: GPL-3.0-or-later
//
// A page of options in rows, top to bottom, as the options dialog lays them out: a name on the left and its
// control in a column further right, or a tick box with its name beside it. A row can depend on an option,
// showing only while that option has a given value (and taking no room while hidden), or being usable
// only while another option is on.
//
// Controls tied to an option start at its value, and every update writes what the user changed straight
// into the options, as the game does: there is no OK or Cancel. Rows not tied to an option leave their
// controls to the owner. Rows of options the program does not act on yet (inert) still show, greyed and not
// usable, so the page reads as the game's does.
#pragma once

#include "config/game/options.hpp"
#include "core/types.hpp"
#include "ui/widgets/dropdown.hpp"
#include "ui/widgets/key_box.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/slider.hpp"
#include "ui/widgets/spinner.hpp"
#include "ui/widgets/tickbox.hpp"

#include <bitset>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rct3::ui::screens::options
{

    struct OptionRowsStyle
    {
        std::string tick_box; ///< Skins of the tick boxes
        std::string tick;
        widgets::DropdownStyle dropdown;
        widgets::SliderStyle slider;
        widgets::SpinnerStyle spinner;
        std::string key_box; ///< Skins of the key boxes
        std::string key_box_focused;
        font::FontRole font = font::FontRole::Small;
        Color text_colour = hex("#000000");
        Color disabled_colour = hex("#7f7f7f"); ///< Of the names of rows that cannot be used
        f32 left = 5;                           ///< Where names start
        f32 indent = 10;                        ///< More for rows under another
        f32 control_x = 200;                    ///< Where controls start
        f32 control_width = 160;                ///< Of drop-downs and sliders
        f32 number_width = 70;                  ///< Of number fields
        f32 slider_inset = 0;                   ///< Sliders are this much shorter than the row at the top and bottom
        f32 row_height = 18;
        f32 row_gap = 3;
        f32 section_gap = 12;
        f32 tick_size = 15;
        f32 tick_gap = 6; ///< Between a tick box and its name
    };

    /// A value an option must have.
    struct OptionIs
    {
        config::game::GameOption option = config::game::GameOption::Fullscreen;
        f32 value = 1;
    };

    class OptionRows
    {
    public:
        /// Rows go into `page`, `width` units wide, from its top; controls write to `options`, which must
        /// outlive the rows. Rows of the `inert` options cannot be used.
        OptionRows(Widget& page, f32 width, OptionRowsStyle style, config::game::GameOptions& options, std::bitset<config::game::kGameOptionCount> inert = { });

        widgets::Tickbox& tick(config::game::GameOption option, std::u16string name, std::u16string tip = { });
        widgets::Dropdown& choice(config::game::GameOption option, std::u16string name, std::vector<std::u16string> items, std::u16string tip = { });
        widgets::Slider& level(config::game::GameOption option, std::u16string name, std::u16string tip = { });
        widgets::Spinner& number(config::game::GameOption option, std::u16string name, std::u16string tip = { });
        /// Two sliders side by side under column headings (see headings()).
        void pair(config::game::DetailPair options, std::u16string name, std::u16string tip = { });
        /// Headings over the two slider columns of pair rows.
        void headings(std::u16string first, std::u16string second);
        /// A drop-down the owner handles.
        /// `widget` names the list, and its label with "_label"; by their place when empty.
        widgets::Dropdown& list(std::u16string name, std::vector<std::u16string> items, std::u16string tip = { }, std::string widget = { });
        /// A name and a second text in the control column.
        widgets::Label& text(std::u16string name, std::u16string value, std::u16string tip = { });
        /// A line of text across the page, as tall as `lines` rows.
        widgets::Label& note(std::u16string text, i32 lines = 1);
        /// Key combinations the owner handles; `prompt` shows while it waits for keys.
        widgets::KeyBox& keys(std::u16string name, std::vector<KeyCombo> combos, std::u16string prompt, std::u16string tip = { });

        /// Puts a wider gap before the next row.
        void section();
        /// The last row goes further right, as under the row before it.
        void indent();
        /// The last row shows only while `condition` holds.
        void shown_when(OptionIs condition);
        /// The last row is usable only while `condition` holds.
        void enabled_when(OptionIs condition);
        /// The last row cannot be used at all.
        void disable();

        /// Writes what the user changed into the options, then shows, hides and places the rows. Returns
        /// whether an option changed.
        bool update();
        /// How far down the shown rows reach, as of the last update.
        [[nodiscard]] f32 height() const
        {
            return height_;
        }
        /// Sets every control from the options, e.g. after the owner changed them.
        void refresh();

        bool locked = false; ///< Every row shows but none can be used

        /// Where the two slider columns of pair rows start.
        static constexpr f32 kFirstColumn = 120;
        static constexpr f32 kSecondColumn = 260;
        static constexpr f32 kColumnWidth = 100;

    private:
        struct Part
        {
            Widget* widget = nullptr;
            f32 x = 0;
            f32 width = 0;
            f32 height = 0; ///< 0: the row's
        };
        enum class Kind : u8
        {
            Tick,
            Choice,
            Level,
            Number,
        };
        struct Binding
        {
            config::game::GameOption option = config::game::GameOption::Fullscreen;
            Kind kind = Kind::Tick;
            Widget* control = nullptr;
        };
        struct Row
        {
            std::vector<Part> parts;
            std::vector<Binding> bindings;
            f32 gap_before = 0;
            std::optional<OptionIs> shown;
            std::optional<OptionIs> enabled;
            f32 height = 0; ///< 0: one row's
            bool inert = false;
        };

        Row& add_row();
        /// `widget` is the name of the control it labels; the label is named after it with "_label".
        widgets::Label& name_label(Row& row, std::u16string name, f32 x, f32 width, std::u16string tip, std::string_view widget = { });
        /// A control's name: `widget`, or by its place when empty.
        std::string control_name(std::string_view widget, std::string_view kind);
        void set_control(const Binding& b);
        void bind(Row& row, const Binding& binding);
        [[nodiscard]] bool holds(const OptionIs& c) const;
        [[nodiscard]] f32 slider_height() const; // 0: the row's

        Widget& page_;
        f32 width_;
        OptionRowsStyle style_;
        config::game::GameOptions& options_;
        std::bitset<config::game::kGameOptionCount> inert_;
        std::vector<Row> rows_;
        f32 next_gap_ = 0;
        f32 height_ = 0;
        i32 count_ = 0; // For unique names
    };

} // namespace rct3::ui::screens::options
