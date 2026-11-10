// SPDX-License-Identifier: GPL-3.0-or-later
//
// The staff window, as the game lays it out: under the title a drop-down to sort by, then one list of every
// worker of the jobs shown, and along the bottom the hire buttons and the switches that show or hide each job,
// with buttons that unfold or fold up every row.
//
// A row shows the worker's picture (a click opens their panel, a second click soon after takes the view to
// them), their name to type over, what they are doing, their patrol button, their mood and their uniform's
// colours (an entertainer's costume too). Unfolded it adds their tasks to switch, their laziness, the warning
// and sack buttons, their training, their wage with arrows to raise or lower it, and the day they were hired;
// the park inspector's unfolded row is the kinds of things they look at, to switch.
//
// The window keeps the sort, the jobs shown and the unfolded rows itself. The owner gives it the park with
// show(), again every so often, and hears what the player asked with take_asks().
#pragma once

#include "ui/game_style.hpp"
#include "ui/screens/attraction_info.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/screens/staff_roster_lines.hpp"
#include "ui/widgets/colour_palette.hpp"
#include "ui/widgets/dropdown.hpp"
#include "ui/widgets/edit_box.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/question_box.hpp"
#include "ui/widgets/scroll_area.hpp"
#include "ui/widgets/window.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rct3::ui::screens
{

    struct StaffRosterStyle
    {
        widgets::WindowStyle window;
        widgets::DropdownStyle dropdown;
        widgets::ScrollBarStyle scroll;
        widgets::ColourPaletteStyle palette;
        widgets::QuestionBoxStyle question;
        widgets::MenuStyle menu;
        std::string name_box; ///< Behind a name, and while it is typed into
        std::string name_box_focused;
        std::string list_back; ///< Behind the list
        Color text_colour = hex("#000000");
        Color accent_colour = hex("#2484be"); ///< Of the sort's name
        Color stripe = hex("#efe8ef");        ///< Behind every other row
        Color plain = hex("#ffffff");
        float sort_x = 59;                ///< Right of the window's icon
        float sort_height = 18;           ///< Of the band with the drop-down
        float row_height = 46;            ///< Of a folded row
        float line_height = 20;           ///< Of each line an unfolded row adds
        float bottom_line = 20;           ///< Of each line along the bottom
        float label_width = 40;           ///< Of "Hire:" and "Show:"
        float hire_size = 23;             ///< Of a hire button; the picture is about this wide
        float job_size = 24;              ///< Of a switch showing a job
        float name_width = 150;           ///< Of the name and what they do
        float swatch = 16;                ///< Of a colour of the uniform
        std::size_t uniform_colours = 32; ///< On the board, as the info panel's
        std::size_t palette_columns = 8;
    };

    /// What the player asked of the staff window. A change is one the info panel's controls make too, by
    /// PersonControl, for the worker of that place in the park's staff.
    struct RosterAsks
    {
        std::optional<save::model::StaffKind> hire;
        std::optional<std::size_t> open;   ///< Their panel
        std::optional<std::size_t> centre; ///< The view to them
        std::optional<std::pair<std::size_t, InfoChange>> change;
        /// Paint the clothes of pot `pot` of worker `staff` in colour `colour`.
        struct Paint
        {
            std::size_t staff = 0;
            std::size_t pot = 0;
            std::size_t colour = 0;
        };
        std::optional<Paint> paint;
    };

    /// A task's switch in a row: its flag bit, skin family and help.
    struct RosterTask
    {
        int bit = 0;
        std::string_view skin;
        std::string_view help;
        bool inverted = false; ///< The bit set means the task is off
    };

    /// The switches of the tasks of `kind`, in the order they stand.
    [[nodiscard]] std::span<const RosterTask> roster_tasks(save::model::StaffKind kind);

    class StaffRoster : public widgets::Window
    {
    public:
        StaffRoster(std::string name, const StaffRosterStyle& style, const StringTable& strings, float width, float height);

        /// Writes the rows again from `facts`, keeping those that did not change.
        void show(const ManagementFacts& facts, const StringTable& strings, const InfoUnits& units);
        /// Seconds since show() last ran.
        [[nodiscard]] float page_age() const
        {
            return page_age_;
        }
        [[nodiscard]] RosterSort sort() const
        {
            return sort_;
        }
        [[nodiscard]] RosterJobs jobs() const
        {
            return jobs_;
        }
        /// The rows in the order shown, by place in the park's staff, and whether each is unfolded.
        [[nodiscard]] std::vector<std::pair<std::size_t, bool>> rows() const;
        [[nodiscard]] RosterAsks take_asks()
        {
            return std::exchange(asks_, { });
        }

        void update(const UpdateContext& context) override;

    private:
        struct Row
        {
            RosterLine line;
            bool expanded = false;
            Widget* panel = nullptr;
            widgets::Button* expand = nullptr;
            widgets::Button* portrait = nullptr;
            widgets::EditBox* name = nullptr;
            widgets::Button* patrol = nullptr;
            widgets::Button* costume = nullptr;
            widgets::Button* discipline = nullptr;
            widgets::Button* train = nullptr;
            widgets::Button* wage_up = nullptr;
            widgets::Button* wage_down = nullptr;
            std::vector<std::pair<int, widgets::Button*>> tasks; ///< With their bit
            std::vector<widgets::Button*> swatches;              ///< By pot
            std::array<widgets::Button*, 2> choices { };         ///< Warn and sack, beside the discipline button while open
        };

        /// Makes row `r`'s widgets again from its line.
        void build_row(Row& r);
        /// Lays the rows down the list, each as tall as it is unfolded.
        void stack_rows();
        /// Opens a box saying `text`, with an answer to go ahead and, when `ask`, one to back out.
        void say(std::u16string text, bool ask);

        StaffRosterStyle style_;
        const StringTable* strings_ = nullptr;
        InfoUnits units_;
        widgets::Dropdown* sort_box_ = nullptr;
        widgets::ScrollArea* list_ = nullptr;
        std::array<widgets::Button*, 5> hires_ { };
        std::array<widgets::Button*, kRosterJobs.size()> views_ { };
        widgets::Button* expand_all_ = nullptr;
        widgets::Button* contract_all_ = nullptr;
        std::vector<Row> rows_;
        std::vector<std::size_t> unfolded_; ///< By place in the park's staff
        RosterSort sort_ = RosterSort::Job;
        RosterJobs jobs_ = kStartRosterJobs;
        bool animal_houses_ = false;
        float page_age_ = 0;
        float since_portrait_ = 0;                                    ///< Seconds since the last click on a picture
        std::optional<std::size_t> last_portrait_;                    ///< Whose picture it was
        widgets::ColourPalette* palette_ = nullptr;                   ///< Open for the swatch `painting_`
        std::optional<std::pair<std::size_t, std::size_t>> painting_; ///< Worker and pot
        widgets::Menu* costumes_ = nullptr;                           ///< Open for the worker `dressing_`
        std::optional<std::size_t> dressing_;
        widgets::QuestionBox* question_ = nullptr;
        std::optional<std::size_t> training_; ///< The worker the open question is about
        RosterAsks asks_;
    };

} // namespace rct3::ui::screens

namespace rct3::ui::game_skins
{
    /// The staff window in the large frame: its list on paper with every other row tinted, the uniforms'
    /// colours picked from the info panel's board of swatches.
    [[nodiscard]] inline screens::StaffRosterStyle staff_roster_style(const Palette& p)
    {
        screens::StaffRosterStyle s;
        s.window = window_style(p);
        s.dropdown = dropdown_style(p);
        s.scroll = scroll_bar_style(p, widgets::Orientation::Vertical);
        s.palette = info_panel_style(p).palette;
        s.question = question_box_style(p);
        s.menu = menu_style(p);
        s.name_box = kListBox;
        s.name_box_focused = kListBoxFocused;
        s.list_back = kListBox;
        s.text_colour = p.text;
        s.accent_colour = p.accent;
        s.stripe = p.stripe;
        s.plain = p.paper;
        return s;
    }

} // namespace rct3::ui::game_skins
