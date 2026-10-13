// SPDX-License-Identifier: GPL-3.0-or-later
//
// A page of options in rows, top to bottom, as the options dialog lays them out: a name on the left and its
// control in a column further right, or a tick box with its name beside it. A row can depend on an option,
// showing only while that option has a given value (and taking no room while hidden), or being usable
// only while another option is on.
//
// Controls tied to an option start at its value, and every update writes what the user changed straight
// into the options, as the game does: there is no OK or Cancel. Rows not tied to an option leave their
// controls to the owner.
#pragma once

#include "config/game_options.hpp"
#include "ui/widgets/dropdown.hpp"
#include "ui/widgets/key_box.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/slider.hpp"
#include "ui/widgets/spinner.hpp"
#include "ui/widgets/tickbox.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rct3::ui
{

    struct OptionRowsStyle
    {
        std::string tick_box; ///< Skins of the tick boxes
        std::string tick;
        DropdownStyle dropdown;
        SliderStyle slider;
        SpinnerStyle spinner;
        std::string key_box; ///< Skins of the key boxes
        std::string key_box_focused;
        FontRole font = FontRole::Small;
        Color text_colour { 0, 0, 0, 255 };
        float left = 5;            ///< Where names start
        float indent = 10;         ///< More for rows under another
        float control_x = 200;     ///< Where controls start
        float control_width = 160; ///< Of drop-downs and sliders
        float number_width = 70;   ///< Of number fields
        float row_height = 18;
        float row_gap = 3;
        float section_gap = 12;
        float tick_size = 15;
        float tick_gap = 6; ///< Between a tick box and its name
    };

    /// A value an option must have.
    struct OptionIs
    {
        config::GameOption option = config::GameOption::Fullscreen;
        float value = 1;
    };

    class OptionRows
    {
    public:
        /// Rows go into `page`, `width` units wide, from its top; controls write to `options`, which must
        /// outlive the rows.
        OptionRows(Widget& page, float width, OptionRowsStyle style, config::GameOptions& options);

        Tickbox& tick(config::GameOption option, std::u16string name, std::u16string tip = { });
        Dropdown& choice(config::GameOption option, std::u16string name, std::vector<std::u16string> items, std::u16string tip = { });
        Slider& level(config::GameOption option, std::u16string name, std::u16string tip = { });
        Spinner& number(config::GameOption option, std::u16string name, std::u16string tip = { });
        /// Two sliders side by side under column headings (see headings()).
        void pair(config::DetailPair options, std::u16string name, std::u16string tip = { });
        /// Headings over the two slider columns of pair rows.
        void headings(std::u16string first, std::u16string second);
        /// A drop-down the owner handles.
        Dropdown& list(std::u16string name, std::vector<std::u16string> items, std::u16string tip = { });
        /// A name and a second text in the control column.
        Label& text(std::u16string name, std::u16string value, std::u16string tip = { });
        /// A key combination the owner handles; `prompt` shows while it waits for keys.
        KeyBox& keys(std::u16string name, KeyCombo combo, std::u16string prompt, std::u16string tip = { });

        /// Puts a wider gap before the next row.
        void section();
        /// The last row goes further right, as under the row before it.
        void indent();
        /// The last row shows only while `condition` holds.
        void shown_when(OptionIs condition);
        /// The last row is usable only while `condition` holds.
        void enabled_when(OptionIs condition);

        /// Writes what the user changed into the options, then shows, hides and places the rows. Returns
        /// whether an option changed.
        bool update();
        /// How far down the shown rows reach, as of the last update.
        [[nodiscard]] float height() const
        {
            return height_;
        }
        /// Sets every control from the options, e.g. after the owner changed them.
        void refresh();

        /// Where the two slider columns of pair rows start.
        static constexpr float kFirstColumn = 120;
        static constexpr float kSecondColumn = 260;
        static constexpr float kColumnWidth = 100;

    private:
        struct Part
        {
            Widget* widget = nullptr;
            float x = 0;
            float width = 0;
            float height = 0; ///< 0: the row's
        };
        enum class Kind : std::uint8_t
        {
            Tick,
            Choice,
            Level,
            Number,
        };
        struct Binding
        {
            config::GameOption option = config::GameOption::Fullscreen;
            Kind kind = Kind::Tick;
            Widget* control = nullptr;
        };
        struct Row
        {
            std::vector<Part> parts;
            std::vector<Binding> bindings;
            float gap_before = 0;
            std::optional<OptionIs> shown;
            std::optional<OptionIs> enabled;
        };

        Row& add_row();
        Label& name_label(Row& row, std::u16string name, float x, float width, std::u16string tip);
        void set_control(const Binding& b);
        [[nodiscard]] bool holds(const OptionIs& c) const;

        Widget& page_;
        float width_;
        OptionRowsStyle style_;
        config::GameOptions& options_;
        std::vector<Row> rows_;
        float next_gap_ = 0;
        float height_ = 0;
        int count_ = 0; // For unique names
    };

} // namespace rct3::ui
