// SPDX-License-Identifier: GPL-3.0-or-later
//
// One of the park's management windows (park operations, finances, staff and so on) in the game's large
// frame: its icon in the corner, its name as the title, picture tabs in the top-right corner with the open
// tab's name on their left, and under them the open tab's page.
//
// A page is lines of a label and a value, then controls the player sets, then a graph, then a table of rows
// under headings, then a row of buttons; any of them may be missing. The table fills what the rest leaves of the window. The window does not
// know what it shows: the owner hears which tab the player picked through take_tab() and gives the page with
// show_page(), again whenever what it shows has changed. The window sorts the table's rows itself, by the
// heading the player pressed last on that tab, and keeps the scroll and the selection across new pages. A
// graph has a tick box by each series' name to show or hide it; the numbers up its left are those of the series
// ticked last, and which series are hidden is kept across new pages. A new page with the same controls keeps
// their widgets, so a bar dragged or an arrow held goes on working; the owner hears of changes through
// take_change().
#pragma once

#include "core/maybe.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/column_table.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/line_plot.hpp"
#include "ui/widgets/slider.hpp"
#include "ui/widgets/spinner.hpp"
#include "ui/widgets/tickbox.hpp"
#include "ui/widgets/window.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui::screens
{

    struct ManagementWindowStyle
    {
        widgets::WindowStyle window;
        widgets::ColumnTableStyle table;
        widgets::ScrollBarStyle scroll;
        float tab_size = 23;
        float tab_step = 20; ///< The tabs overlap a little, as the options window's do
        float tab_bar_height = 25;
        float caption_x = 59; ///< Right of the window's icon, under the title
        float caption_height = 20;
        font::FontRole caption_font = font::FontRole::Small;
        Color caption_colour = hex("#2484be");
        Color text_colour = hex("#000000");
        Color heading_colour = hex("#2484be"); ///< Of a line that has no value: a heading over the lines below it
        float line_height = 15;
        float value_column = 0.55f; ///< Of the page's width, where values start
        float pair_gap = 12;        ///< Between the two half lines of a row
        float plot_height = 90;
        float axis_width = 64; ///< Of the numbers left of a graph
        std::string tick_box;  ///< Skins of a graph's series' tick boxes
        std::string tick;
        float tick_size = 13;
        float gap = 6;               ///< Between the parts of a page
        std::string button;          ///< Skin family of the buttons under a page
        Rect button_inset;           ///< Of their text; see Button::text_inset
        float button_height = 22;    ///< Of a button with text
        float icon_button_size = 34; ///< Width and height of a button with an icon and no text
        /// The colours of a graph's series, in turn.
        std::vector<Color> series_colours { hex("#2484be"), hex("#e0402a"), hex("#3aa83a"), hex("#e0a020"), hex("#8a4fc0"), hex("#20a0a0"), hex("#c04080"), hex("#707070") };
        Color plot_background = hex("#ffffffa0");
        Color plot_rule = hex("#7f7f7f60"); ///< Of a graph's level lines and ticks
        widgets::SpinnerStyle spinner;      ///< Of a page's numbers, shown in words with only the arrows
        /// Of a page's bars set by pressing along them, as the info panel's; colours chosen by eye.
        widgets::SliderStyle bar { .rail = { }, .thumb = { }, .thumb_width = 0, .bar = true, .bar_back_colour = hex("#ece8f0") };
        float bar_width = 130;             ///< Of a line's bar and a control's
        Color bar_back = hex("#00000040"); ///< Of a line's bar, behind its filled part
    };

    /// A tab: the skin of its picture and its name, shown as its help and beside the tabs while it is open.
    struct ManagementTab
    {
        std::string icon;
        std::u16string name;
    };

    /// A button under a page: a picture or a text, and its help. A button with choices fans them out beside
    /// it when pressed, and reports the one picked instead of the press.
    struct ManagementButton
    {
        std::string icon { };
        std::u16string text { };
        std::u16string help { };
        bool enabled = true;
        std::vector<InfoChoice> choices { };
        std::string key { }; ///< Widget name of a button without an icon
    };

    /// A span of a graph the player may pick, by its name: `slots` samples back from the newest, or all of them
    /// for 0.
    struct GraphRange
    {
        std::u16string name;
        std::size_t slots = 0;
    };

    struct ManagementPage
    {
        std::vector<InfoLine> lines;
        /// Under the lines, a row each: numbers (with InfoControl::shown), bars, and words across the page as a
        /// heading; other kinds are left out.
        std::vector<InfoControl> controls;
        std::vector<InfoSeries> graph;
        std::vector<InfoTick> graph_ticks;
        std::size_t graph_slots = 0;               ///< Samples across the graph; see LinePlot::slots
        std::vector<GraphRange> graph_ranges;      ///< Above the graph, which the wheel zooms too; none for neither
        std::vector<std::u16string> graph_when;    ///< Each sample's time, newest first, heading the readout
        std::vector<widgets::TableColumn> columns; ///< No table without any
        std::vector<widgets::TableRow> rows;
        std::optional<std::size_t> sort; ///< The column the rows are sorted by until the player picks one
        bool sort_descending = false;
        std::vector<ManagementButton> buttons;
        std::u16string note; ///< Under everything, across the page
    };

    class ManagementWindow : public widgets::Window
    {
    public:
        /// `width` x `height` with the frame, centred on the desktop, `tabs` along the top; `picked` open.
        ManagementWindow(std::string name, const ManagementWindowStyle& style, float width, float height, std::vector<ManagementTab> tabs, std::size_t picked);

        [[nodiscard]] std::size_t tab() const
        {
            return tab_;
        }
        [[nodiscard]] std::size_t tab_count() const
        {
            return tabs_.size();
        }
        [[nodiscard]] widgets::Button& tab_button(std::size_t i) const
        {
            return *tabs_.at(i);
        }
        /// Opens tab `i`, as if the player picked it, but without reporting it.
        void open_tab(std::size_t i);
        /// The tab the player picked since the last call.
        [[nodiscard]] std::optional<std::size_t> take_tab()
        {
            return std::exchange(tab_now_, std::nullopt);
        }
        /// The button under the page the player pressed since the last call.
        [[nodiscard]] std::optional<std::size_t> take_button()
        {
            return std::exchange(button_now_, std::nullopt);
        }
        /// The button under the page and the one of its choices the player picked since the last call.
        [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> take_choice()
        {
            return std::exchange(choice_now_, std::nullopt);
        }
        /// Where the page's control `id` stands now, a number's or a bar's, perhaps moved since the page came.
        [[nodiscard]] std::optional<int> control_value(int id) const;
        /// The control the player changed since the last call, the last one.
        [[nodiscard]] std::optional<InfoChange> take_change()
        {
            return std::exchange(change_now_, std::nullopt);
        }
        /// The id of the table row the player activated since the last call.
        [[nodiscard]] std::optional<std::size_t> take_row()
        {
            return std::exchange(row_now_, std::nullopt);
        }

        void show_page(const ManagementPage& page);
        /// Seconds since the last page came.
        [[nodiscard]] float page_age() const
        {
            return page_age_;
        }
        [[nodiscard]] std::optional<widgets::ColumnTable&> table() const
        {
            return core::maybe(table_);
        }
        /// The id of the table row picked, when the page has a table and one is picked.
        [[nodiscard]] std::optional<std::size_t> picked_row() const;
        /// Whether the row picked is another than when the page was last shown.
        [[nodiscard]] bool picked_row_changed() const
        {
            return picked_row() != shown_row_;
        }

        void update(const UpdateContext& context) override;

    private:
        /// The page's area inside the window's content, below the tabs.
        [[nodiscard]] Widget& page() const
        {
            return *page_;
        }
        void sort_rows();
        /// Fans out the choices of button `i` beside it.
        void open_choices(std::size_t i);
        void close_choices();
        /// Gives the graph the series that are ticked and its numbers those of the one in focus.
        void fill_graph();
        /// Spans the graph over the samples the player picked, and names the marks under it that fit.
        void span_graph();

        ManagementWindowStyle style_;
        std::vector<widgets::Button*> tabs_;
        std::vector<std::u16string> names_;
        widgets::Label* caption_ = nullptr;
        Widget* page_ = nullptr;
        Widget* lines_ = nullptr; ///< The page's lines, graph, buttons and note, made again with each page
        widgets::ColumnTable* table_ = nullptr;
        widgets::LinePlot* plot_ = nullptr;
        std::vector<InfoSeries> graph_;               ///< The page's graph's series, as plot_ draws them when ticked
        std::vector<widgets::Tickbox*> series_ticks_; ///< By graph_
        std::array<widgets::Label*, 5> axis_ { };     ///< Numbers up the graph's left, top first
        std::vector<std::string> hidden_;             ///< Keys of the series the player unticked
        std::string focus_;                           ///< Key of the series the player ticked last; the first when none
        std::optional<std::size_t> graph_span_;       ///< Samples the player zoomed the graph to; all when none
        std::size_t graph_slots_ = 0;                 ///< The page's graph's own, all of its samples
        std::vector<InfoTick> graph_ticks_;           ///< The page's graph's marks
        std::vector<std::u16string> graph_when_;      ///< See ManagementPage::graph_when
        std::vector<widgets::Label*> marks_;          ///< Words under the graph's marks that fit its span
        std::pair<float, float> marks_top_ { };       ///< Of the marks' words: an anchor and an offset down the page
        std::vector<widgets::Button*> ranges_;        ///< Picking the graph's span, by graph_ranges_
        std::vector<GraphRange> graph_ranges_;
        std::vector<widgets::Button*> buttons_;
        std::vector<std::vector<InfoChoice>> button_choices_; ///< Of each of buttons_
        std::vector<widgets::Button*> choices_;               ///< Fanned out beside the button choosing_
        std::optional<std::size_t> choosing_;
        /// Of each tab, the column the player sorted by and whether highest first; none until they pick one.
        std::vector<std::optional<std::pair<std::size_t, bool>>> sorts_;
        std::optional<std::pair<std::size_t, bool>> page_sort_; ///< The page's own, for when the player has none
        std::size_t tab_ = 0;
        float page_age_ = 0;
        std::optional<std::size_t> tab_now_;
        std::optional<std::size_t> button_now_;
        std::optional<std::pair<std::size_t, std::size_t>> choice_now_;
        std::optional<std::size_t> row_now_;
        std::optional<std::size_t> shown_row_; ///< The row picked when the page was last shown
        struct Control
        {
            InfoControl control;
            widgets::Label* label = nullptr;
            Widget* widget = nullptr;        ///< The spinner or the bar; none for words
            widgets::Label* shown = nullptr; ///< Its value in words
        };
        Widget* controls_box_ = nullptr; ///< Holds the controls' widgets, kept while the page's controls stay the same
        std::vector<Control> controls_;
        std::optional<InfoChange> change_now_;
    };

} // namespace rct3::ui::screens
