// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window that picks a career, laid out as the game's: under a line saying what to do, a list of the
// game's careers, four and a half rows in sight. Each row counts the scenarios done at each level beside
// the three hats, shows a picture of the career's park, its name in a box that renames it and how far
// along it is. Along the bottom the new, load, cancel and delete buttons, the hovered one's name on the
// left. The rows' backgrounds alternate; the picked row has a stronger frame.
//
// The window knows nothing of files. The owner gives the careers with set_careers(), and changes a row with
// set_career(), e.g. when its picture is read. What the user asks for comes back by row: take_load(),
// take_new(), take_delete() once confirmed, take_rename() when a name was changed and Enter pressed or the
// career loaded. New adds nothing by itself: the owner makes the career and adds it with add_career(), which
// picks it, as the game does. A confirmed delete takes the row out at once and leaves nothing picked.
#pragma once

#include "career/career.hpp"
#include "ui/draw_list.hpp"
#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/edit_box.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/question_box.hpp"
#include "ui/widgets/scroll_area.hpp"
#include "ui/widgets/window.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui
{

    /// Sizes in units, the content's top-left the origin.
    struct CareerDialogStyle
    {
        WindowStyle window;
        QuestionBoxStyle question;
        ScrollBarStyle scroll;
        std::string icon; ///< Of the window
        std::string button;
        std::string new_icon;
        std::string load_icon;
        std::string cancel_icon;
        std::string delete_icon;
        float big_icon_scale = 0.6f;     ///< Of the new and load icons, drawn smaller than their own size
        std::array<std::string, 3> hats; ///< Tycoon, entrepreneur, apprentice, top to bottom
        std::string row_frame;           ///< Around a row
        std::string row_frame_selected;
        std::string no_picture; ///< Shown for a career without a picture
        std::string name_box_focused;
        FontRole font = FontRole::Small;
        Color text_colour = hex("#000000");
        Color hover_colour = hex("#2484be");
        std::array<Color, 2> row_colours { hex("#efe8ef"), hex("#ffffff") };

        float width = 380;
        float subtitle_height = 21; ///< Band above the list for the line saying what to do
        float subtitle_x = 59;
        float list_height = 270;
        float bar_height = 30; ///< Under the list: the hovered button's name and the buttons
        float button_size = 30;
        float button_gap = 8;
        float row_width = 362;
        float row_height = 60;
        float cell = 20;                 ///< Hats and counts are cells this size, three down the row's left
        Rect picture { 43, 1, 117, 59 }; ///< In a row
        float text_x = 120;              ///< Name and progress, from here to the row's right
        std::size_t name_length = 40;    ///< Ours; the game sets none
        float double_click = 0.4f;       ///< Seconds between the clicks of a double click (ours)
    };

    /// What a row shows of one career.
    struct CareerRow
    {
        std::u16string name;
        career::CareerScore score;
        TextureId picture = 0; ///< 0 for none; stays the owner's
    };

    class CareerDialog : public Window
    {
    public:
        CareerDialog(std::string name, const CareerDialogStyle& style, const StringTable& strings);

        /// Shows these careers, the first picked.
        void set_careers(std::vector<CareerRow> careers);
        void set_career(std::size_t index, CareerRow career);
        /// Adds a career at the bottom and picks it.
        void add_career(CareerRow career);

        [[nodiscard]] std::size_t size() const
        {
            return rows_.size();
        }
        [[nodiscard]] std::optional<std::size_t> selected() const
        {
            return selected_;
        }
        void select(std::optional<std::size_t> index);

        [[nodiscard]] std::optional<std::size_t> take_load();
        [[nodiscard]] bool take_new();
        [[nodiscard]] std::optional<std::size_t> take_delete();
        [[nodiscard]] std::optional<std::pair<std::size_t, std::u16string>> take_rename();

        // For tests and owners that drive the window.
        [[nodiscard]] Button& new_button() const
        {
            return *new_;
        }
        [[nodiscard]] Button& load_button() const
        {
            return *load_;
        }
        [[nodiscard]] Button& delete_button() const
        {
            return *delete_;
        }
        [[nodiscard]] EditBox& name_box(std::size_t index) const;
        /// The counts beside the tycoon, entrepreneur and apprentice hats, and the progress.
        [[nodiscard]] std::array<std::u16string, 4> row_text(std::size_t index) const;
        [[nodiscard]] const Label& hover() const
        {
            return *hover_;
        }
        [[nodiscard]] QuestionBox* question() const
        {
            return question_;
        }
        void click_row(std::size_t index);

        void update(const UpdateContext& context) override;

    private:
        class Row;

        [[nodiscard]] std::u16string text(std::string_view key) const;
        Row& make_row(std::size_t index, CareerRow career);
        void place_rows();
        void load();
        void check_rename(std::size_t index);

        CareerDialogStyle style_;
        const StringTable& strings_;
        std::vector<Row*> rows_;
        std::optional<std::size_t> selected_;
        ScrollArea* list_ = nullptr;
        Label* hover_ = nullptr;
        Button* new_ = nullptr;
        Button* load_ = nullptr;
        Button* cancel_ = nullptr;
        Button* delete_ = nullptr;
        QuestionBox* question_ = nullptr;
        std::optional<std::size_t> deleting_;
        std::optional<std::size_t> load_out_;
        bool new_out_ = false;
        std::optional<std::size_t> delete_out_;
        std::optional<std::pair<std::size_t, std::u16string>> rename_out_;
        std::size_t made_ = 0; // Rows made, so each has its own name
        float since_click_ = 1e9f;
        std::optional<std::size_t> last_clicked_;
    };

    /// The progress as the game writes it: three significant digits and a percent sign, e.g. "33.3%".
    [[nodiscard]] std::u16string career_percent(float percent);

} // namespace rct3::ui
