// SPDX-License-Identifier: GPL-3.0-or-later
//
// The round panel in the bottom-right corner of the park view that tells about one thing at a time: an
// attraction, and later a person. A disc holds a view of the thing; its tabs sit on a ring round the disc,
// each at its own place on the ring; the open tab's content slides out to the left of the disc in a strip
// with rounded ends. A small button under the disc closes the panel. The panel never moves.
//
// The panel does not know what it tells about. The owner gives it the tabs with show(), hears which tab the
// player picked through take_picked(), fills body() with that tab's content and gives its size with
// set_body_size(). Showing other tabs replaces the old ones, so only one thing is ever told about.
#pragma once

#include "core/maybe.hpp"
#include "ui/panel_look.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/colour_palette.hpp"
#include "ui/widgets/dropdown.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/line_plot.hpp"
#include "ui/widgets/paint_button.hpp"
#include "ui/widgets/spinner.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui
{

    /// The panel's frame drawn in code: the colours and measures of its shapes, in units. The strip is a slab
    /// with a round top-left corner and a darker band along its top; the disc is flat on the left, where it
    /// joins the strip, and round on the right, with the band running on into its top-left corner and a rim
    /// round the view. Both cast a hard shadow down and to the right.
    struct CodedInfoPanel
    {
        Color body = hex("#4babe4");      ///< Of the strip below its band
        Color band = hex("#2484be");      ///< Along the top of the strip and the disc
        Color disc = hex("#56abe5");      ///< Of the disc and its rim
        Color view_back = hex("#7f7f7f"); ///< In the round view when it shows nothing
        Color shadow = hex("#00000080");
        float band_height = 18;
        float corner = 11;      ///< Of the strip's top corners
        float rim = 10.5f;      ///< Of the ring round the view; what it holds is a touch wider than the view
        float strip_shadow = 3; ///< Of the strip's own rectangle, taken by its shadow on the right and bottom
        /// Behind a page of lines, a line below the band and from the strip's open end to the ring's tabs: a
        /// notebook's margin at the open end, then the lines in a box ruled on its margin side and below, every other
        /// one striped.
        Color paper = hex("#ffffff");
        Color stripe = hex("#efe8ef");
        Color margin = hex("#fff7ff");
        Color rule = hex("#d6d6d6");    ///< The box's lines, two on the margin's side
        Color divider = hex("#a3a3a3"); ///< Between the parts of a page of tools
        float paper_margin = 16;
        float paper_bottom = 9;  ///< Of the strip left below the paper
        float paper_corner = 16; ///< Of its bottom corner by the tabs
    };

    struct InfoPanelStyle
    {
        PanelLook look = PanelLook::Texture; ///< Whether the frame comes from the skins below or from `coded`
        CodedInfoPanel coded;
        std::string disc;           ///< Behind the view, fully opaque
        std::string glass;          ///< Over the view, faint
        std::string rim;            ///< Round the right of the disc
        std::string shadow;         ///< Under the disc
        std::string rim_shadow;     ///< Under the rim
        std::string strip;          ///< Behind a tab's content, stretched in nine parts
        std::string close_icon;     ///< On the close button
        std::string tab_face;       ///< Behind each tab's icon
        PaintButtonStyle paint;     ///< Of a paint tab; its face is tab_face
        ColourPaletteStyle palette; ///< Of the colours a paint tab opens
        std::size_t palette_count = 32;
        std::size_t palette_columns = 8;
        SpinnerStyle spinner;   ///< Of a page's number controls
        DropdownStyle dropdown; ///< Of its lists
        std::string tick_box;   ///< Of its ticks
        std::string tick;
        std::string button;                           ///< Behind its buttons' text
        Color section_colour = hex("#b8d8f0");        ///< Of a section button's text
        Color picked_section_colour = hex("#ffffff"); ///< Of the shown section's
        Color bar_back = hex("#00000040");            ///< Of a line's bar, behind its filled part
        float glass_alpha = 0.15f;
        /// In the bottom-left corner instead, turned the other way: tabs mirrored, the strip on the right.
        /// Drawn only in code.
        bool left = false;
        float raise = 0; ///< Of the disc and its close button, clear of what stands below them
        Color text_colour = hex("#ffffff");
        Color paper_text_colour = hex("#000000"); ///< Of a page on paper
        /// On the button in the band that puts the strip away, pointing at the disc: the right-hand panel's and the
        /// left-hand one's.
        std::string hide_icon;
        std::string hide_icon_left;
        /// The colours of a graph's series, in turn.
        std::vector<Color> series_colours { hex("#ffe14a"), hex("#6be36b"), hex("#ff7a5c"), hex("#7ac8ff") };
    };

    /// One of the small buttons that fan out from a tab that offers choices instead of a page.
    struct InfoChoice
    {
        std::string icon;
        std::u16string help;
        bool enabled = true;
    };

    /// A tab: its icon, its help, and its place on the ring. A tab with choices opens them round itself
    /// when pressed, and an action tab does something at once; neither is ever the open tab.
    struct InfoTab
    {
        std::string icon;
        std::u16string help;
        int slot = 0;
        std::vector<InfoChoice> choices;
        bool action = false;
        /// A paint tab's pots, each with its colour; pressing a pot opens colours to pick for it. The tab's icon
        /// is the pots'.
        std::vector<std::size_t> paints;
        std::size_t colours = 0; ///< To pick from for its pots; 0 for the panel's own count
    };

    /// A colour the player picked for a paint tab's pot.
    struct InfoPaint
    {
        std::size_t tab = 0;
        std::size_t pot = 0;
        std::size_t colour = 0;
    };

    namespace info_panel
    {
        inline constexpr float kDisc = 128;       ///< Width and height of the disc
        inline constexpr float kDiscMargin = 18;  ///< From the screen's right and bottom edges to the disc
        inline constexpr float kShadowOffset = 3; ///< Of the shadows, right and down
        /// Across the round view of the thing, centred on the disc, within the disc's plain middle.
        inline constexpr float kView = 106;
        inline constexpr float kRingRadius = 72; ///< From the disc's middle to the tabs' middles
        inline constexpr float kFirstAngle = 42; ///< Degrees up from the right, of slot 0
        inline constexpr float kSlotAngle = 32;  ///< Between neighbouring slots, going round to the left
        inline constexpr float kTabSize = 28;    ///< A tab's face at rest
        inline constexpr float kTabGrown = 40;   ///< Under the pointer
        inline constexpr float kIconSize = 40;   ///< Of the tab icons' own pictures
        inline constexpr float kCloseSize = 14;
        inline constexpr float kHideSize = 12;     ///< The button that puts the strip away
        inline constexpr float kHideInset = 4;     ///< From the strip's open end and the band's top
        inline constexpr float kTitleLeft = 20;    ///< Of the title, clear of that button
        inline constexpr float kCloseRight = 9;    ///< From the screen's right edge
        inline constexpr float kCloseBottom = 9;   ///< From the screen's bottom edge
        inline constexpr float kStripOverlap = 13; ///< Of the strip under the disc
        inline constexpr float kStripExtra = 40;   ///< Of the strip's width beyond its content's
        inline constexpr float kStripNarrowest = 136;
        inline constexpr float kStripHeight = 130; ///< Of the strip, whatever its content
        inline constexpr float kBodyLeft = 24;     ///< Of the content, from the strip's left edge, past the paper's margin
        inline constexpr float kBodyTop = 22;      ///< From the strip's top edge, below its band
        inline constexpr float kBodyBottom = 12;   ///< From the strip's bottom edge
        inline constexpr float kPageWidth = 360;   ///< Of a page of lines
        inline constexpr float kLineHeight = 15;
        inline constexpr float kBarWidth = 130;     ///< Of a line's bar
        inline constexpr float kValueColumn = 185;  ///< From the page's left edge to where values start
        inline constexpr float kSectionHeight = 15; ///< Of the row of section buttons
        inline constexpr float kPlotHeight = 64;
        inline constexpr float kAxisWidth = 76;      ///< Of the numbers left of a graph
        inline constexpr float kAxisPlotHeight = 48; ///< Of a graph with numbers, leaving room for two rows of controls
        inline constexpr float kPairGap = 6;         ///< Between the two half lines of a row
        inline constexpr float kRingReach = 30;      ///< Of the page's right end, under the ring's tabs
        inline constexpr float kChoiceSize = 22;     ///< A choice button
        inline constexpr float kChoiceIconScale = 0.7f;
        inline constexpr float kChoiceRadius = 26; ///< From the tab's middle to its choices'
        /// Radians either side of the line out from the disc through the tab, for two and for three choices.
        inline constexpr float kTwoChoiceSpread = 0.38f;
        inline constexpr float kThreeChoiceSpread = 0.76f;
        inline constexpr float kPaletteGap = 4;     ///< Between a paint tab and the colours it opens
        inline constexpr float kScreenMargin = 4;   ///< Kept between the colours and the screen's edges
        inline constexpr float kControlHeight = 15; ///< Of a row holding a control
        inline constexpr float kTickSize = 13;
        inline constexpr float kPaintSize = 26;    ///< Of a page's pots
        inline constexpr float kNumberWidth = 70;  ///< Of a page's spinners
        inline constexpr float kChoiceWidth = 140; ///< Of a page's lists
    } // namespace info_panel

    struct InfoLine
    {
        std::u16string label;
        std::u16string value;
        bool half = false;                 ///< Short enough to share its row with the next half line
        std::string name = { };            ///< Its widgets' name: the label's string key, to find where it is made by
        std::optional<float> bar { };      ///< 0 to 1: its value sits on a bar filled that far
        Color bar_colour = hex("#40c040"); ///< The bar's filled part
    };

    struct InfoSeries
    {
        std::u16string name;
        std::vector<float> values;
        std::string key = { }; ///< Its name's string key, which its widget is named by
        bool shown = true;     ///< Drawn, or left out while keeping its colour
    };

    enum class InfoControlKind : std::uint8_t
    {
        Number, ///< A spinner
        Choice, ///< A list to pick from
        Tick,   ///< A tick box, ticked when the value is not 0
        Button, ///< Pressed for its value, which is 0
        Paint,  ///< Pots of colours, each opening the colours to pick from
        Text,   ///< A line to type in, told when Enter is pressed
        Words,  ///< Its label alone, across its column
    };

    /// Something on a page the player can change, on a row of its own under the lines: its label on the left,
    /// the control in the value column. The owner tells the controls apart by their ids.
    struct InfoControl
    {
        int id = 0;
        std::string name; ///< Its widget's name: a string key from where it is made, to find it by
        InfoControlKind kind = InfoControlKind::Number;
        std::u16string label;
        int value = 0; ///< The number, the item picked, or whether ticked
        int minimum = 0;
        int maximum = 0;
        int step = 1;
        int decimals = 0;                  ///< Of a number shown with a point, such as money in hundredths
        std::vector<std::u16string> items; ///< Of a list
        std::u16string text;               ///< On a button, typed, or a number's unit after it
        std::string icon;                  ///< A button's picture, instead of text
        std::vector<std::size_t> paints;   ///< The colour of each pot
        bool enabled = true;
        /// On the row of the control before it, after it, instead of on a row of its own.
        bool beside = false;
        int column = 0;                        ///< 0 the page's left, or all of it; 1 its right half
        float width = 0;                       ///< Of the control itself; 0 for its kind's own
        std::u16string help;                   ///< Shown under the pointer
        std::optional<Color> colour { };       ///< Of its label, instead of the panel's text colour
        std::optional<std::size_t> series { }; ///< The graph's series whose colour its label takes
    };

    /// A change the player made to a page's control: the control's id and its new value. For pots, `pot` is the
    /// one painted and `value` its colour.
    struct InfoChange
    {
        int id = 0;
        int value = 0;
        std::size_t pot = 0;
        std::u16string text; ///< Typed
    };

    /// A tab's content: a title, a row of buttons for its sections when it has more than one, lines of a
    /// label and a value, a note under them and a graph.
    struct InfoPage
    {
        std::u16string title;
        std::u16string heading; ///< Under the title, such as what kind of thing it is
        std::vector<std::u16string> sections;
        std::size_t section = 0;   ///< The one shown
        bool section_list = false; ///< Picked from a list instead of a row of words
        std::vector<InfoLine> lines;

        std::u16string note;
        std::vector<InfoSeries> graph;
        /// Numbers up the graph's left, top first, evenly apart from its top to its bottom; with them, the names
        /// under the graph are left to the controls.
        std::vector<std::u16string> graph_axis;
        std::vector<InfoControl> controls;             ///< Under the note
        std::optional<std::size_t> controls_after { }; ///< How many lines go over the controls; all when empty
        float width = 0;                               ///< Of the page; 0 for info_panel::kPageWidth
    };

    /// A widget's name from the string key of what it shows and its part, such as "Park_Name_value", so a search
    /// for the key finds where it is made; by its place, such as "value3", for what has no key.
    [[nodiscard]] inline std::string keyed_name(std::string_view key, std::string_view part, std::size_t index)
    {
        if (key.empty())
            return std::format("{}{}", part, index);
        return part.empty() ? std::string(key) : std::format("{}_{}", key, part);
    }

    /// The middle of the tab at `slot`, from the middle of the disc, with y down.
    [[nodiscard]] Point info_tab_offset(int slot);
    /// The middle of choice `i` of `count` round the tab at `slot`, from the tab's middle, with y down.
    [[nodiscard]] Point info_choice_offset(int slot, std::size_t i, std::size_t count);

    class InfoPanel : public Widget
    {
    public:
        InfoPanel(std::string name, InfoPanelStyle style);

        /// Opens the panel with `tabs`, the one at `picked` open; none when `picked` is past them.
        void show(std::vector<InfoTab> tabs, std::size_t picked);
        void hide();
        [[nodiscard]] bool showing() const
        {
            return visible;
        }

        /// The tab the player opened since the last call.
        [[nodiscard]] std::optional<std::size_t> take_picked()
        {
            return std::exchange(picked_now_, std::nullopt);
        }
        /// The section of the page the player asked for since the last call.
        [[nodiscard]] std::optional<std::size_t> take_section()
        {
            return std::exchange(section_now_, std::nullopt);
        }
        /// The action tab the player pressed since the last call.
        [[nodiscard]] std::optional<std::size_t> take_action()
        {
            return std::exchange(action_now_, std::nullopt);
        }
        /// The choice the player made since the last call, as the tab and the choice's index.
        [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> take_choice()
        {
            return std::exchange(choice_now_, std::nullopt);
        }
        /// A change the player made to a control on the page since the last call.
        [[nodiscard]] std::optional<InfoChange> take_change()
        {
            return std::exchange(change_now_, std::nullopt);
        }
        /// The widget of the page's control `id`, if the page has it.
        [[nodiscard]] std::optional<Widget&> control(int id) const;
        /// The colour the player picked for a paint tab since the last call.
        [[nodiscard]] std::optional<InfoPaint> take_paint()
        {
            return std::exchange(paint_now_, std::nullopt);
        }
        /// The paint tab whose colours are out, if any.
        [[nodiscard]] std::optional<std::size_t> painting() const
        {
            return painting_;
        }
        [[nodiscard]] std::optional<ColourPalette&> palette() const
        {
            return maybe(palette_);
        }
        /// Puts the colours away, as when the player presses elsewhere.
        void close_palette();

        /// The tab whose choices are out, if any.
        [[nodiscard]] std::optional<std::size_t> choosing() const
        {
            return choosing_;
        }
        [[nodiscard]] Button& choice(std::size_t i) const
        {
            return *choices_.at(i);
        }
        /// True once after the player closed the panel.
        [[nodiscard]] bool take_closed()
        {
            return std::exchange(closed_, false);
        }
        [[nodiscard]] std::size_t picked() const
        {
            return picked_;
        }

        /// Where the open tab's content goes; the owner empties and fills it.
        [[nodiscard]] Widget& body() const
        {
            return *body_;
        }
        /// The size of the open tab's content; the strip fits round it.
        void set_body_size(float width, float height);
        /// Fills the body with `page`: its title in the band along the strip's top, its lines in two columns, the
        /// values clear of the widest label, its note and its controls, each clear of its label. A page of lines or a
        /// note without a graph sits on striped paper, in dark text. An empty page hides the strip.
        void show_page(const InfoPage& page);

        /// Shows the picture `texture` in the round view on the disc; 0 shows the plain disc.
        void show_view(TextureId texture)
        {
            view_ = texture;
        }
        [[nodiscard]] Rect disc() const;
        /// The square the round view fits in.
        [[nodiscard]] Rect view() const;
        [[nodiscard]] Rect strip() const;
        /// Whether the strip is out: the page has content and the player has not put it away with the arrow in its
        /// band. Picking a tab brings it back.
        [[nodiscard]] bool strip_shown() const
        {
            return body_width_ > 0 && !tucked_;
        }
        [[nodiscard]] Button& tab(std::size_t i) const
        {
            return *tabs_.at(i);
        }
        [[nodiscard]] std::size_t tab_count() const
        {
            return tabs_.size();
        }

        /// The disc and the strip stop the pointer; the rest of the screen lets it through.
        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool covers(Point p) const override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        void place();
        [[nodiscard]] Color series_colour(std::size_t i) const
        {
            return style_.series_colours.empty() ? style_.text_colour : style_.series_colours[i % style_.series_colours.size()];
        }
        /// Places the lines' values and the controls across the page, measuring the labels with `font`, or
        /// guessing at their widths without one.
        void place_page(std::optional<const Font&> font);
        void draw_skins(DrawContext& context) const;
        void draw_coded(DrawList& list) const;
        void mark_picked();
        void open_choices(std::size_t tab);
        void close_choices();
        void open_palette(std::size_t tab, std::size_t pot);
        void open_page_palette(std::size_t control, std::size_t pot);
        void read_controls();

        InfoPanelStyle style_;
        std::vector<Button*> tabs_;
        std::vector<int> slots_;
        std::vector<std::vector<InfoChoice>> tab_choices_;
        std::vector<Button*> choices_;
        std::optional<std::size_t> choosing_;
        std::vector<PaintButton*> paints_;       ///< Of each tab, its pots when it is a paint tab
        std::vector<std::size_t> paint_colours_; ///< Of each tab, InfoTab::colours
        ColourPalette* palette_ = nullptr;
        std::optional<std::size_t> painting_;
        std::size_t painting_pot_ = 0;
        std::optional<InfoPaint> paint_now_;
        /// The page's controls, with their widgets; the colours are out for a page's pots when painting_page_ is
        /// set (the control) instead of painting_.
        std::vector<std::pair<InfoControl, Widget*>> controls_;
        std::optional<std::size_t> painting_page_;
        std::optional<InfoChange> change_now_;
        std::optional<std::pair<std::size_t, std::size_t>> choice_now_;
        std::vector<Button*> sections_;
        Dropdown* section_list_ = nullptr; ///< When the page picks its sections from a list
        /// A line of the page with a value, whose value lines up with the others clear of the widest label.
        struct ValueRow
        {
            Label* label = nullptr;
            Label* value = nullptr;
            Widget* bar = nullptr; ///< Under the value when it has one, with its filled part
            Widget* filled = nullptr;
            float fill = 0;
        };
        std::vector<ValueRow> value_rows_;
        /// Where each of controls_ sits down the page, set as the page is filled; across is set by place_page.
        struct ControlRow
        {
            Label* label = nullptr;
            std::size_t column = 0;
            float y = 0;
            float height = 0;
        };
        std::vector<ControlRow> control_rows_;
        float page_width_ = 0;
        bool measured_ = false; ///< Whether place_page has run with the fonts since the page was shown
        std::optional<std::size_t> section_now_;
        Button* close_ = nullptr;
        Button* hide_ = nullptr;
        bool tucked_ = false;  ///< The strip put away by the player
        bool paper_ = false;   ///< The page sits on paper
        bool striped_ = false; ///< Its lines are striped; a note alone sits in a plain box down the paper
        Widget* body_ = nullptr;
        Label* title_ = nullptr; ///< The page's, in the band

        std::size_t picked_ = 0;
        std::optional<std::size_t> picked_now_;
        std::optional<std::size_t> action_now_;
        std::vector<bool> actions_; ///< Of each tab, whether it acts at once
        bool closed_ = false;
        float body_width_ = 0;
        TextureId view_ = 0;
        float body_height_ = 0;
    };

} // namespace rct3::ui
