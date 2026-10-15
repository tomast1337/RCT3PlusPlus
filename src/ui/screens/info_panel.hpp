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

#include "ui/widgets/button.hpp"
#include "ui/widgets/line_plot.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui
{

    struct InfoPanelStyle
    {
        std::string disc;                             ///< Behind the view, fully opaque
        std::string glass;                            ///< Over the view, faint
        std::string rim;                              ///< Round the right of the disc
        std::string shadow;                           ///< Under the disc
        std::string rim_shadow;                       ///< Under the rim
        std::string strip;                            ///< Behind a tab's content, stretched in nine parts
        std::string close_icon;                       ///< On the close button
        std::string tab_face;                         ///< Behind each tab's icon
        Color section_colour = hex("#b8d8f0");        ///< Of a section button's text
        Color picked_section_colour = hex("#ffffff"); ///< Of the shown section's
        float glass_alpha = 0.15f;
        Color text_colour = hex("#ffffff");
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
    };

    namespace info_panel
    {
        inline constexpr float kDisc = 128;       ///< Width and height of the disc
        inline constexpr float kDiscMargin = 18;  ///< From the screen's right and bottom edges to the disc
        inline constexpr float kShadowOffset = 3; ///< Of the shadows, right and down
        /// Across the round view of the thing, centred on the disc, within the disc's plain middle.
        inline constexpr float kView = 106;
        inline constexpr float kRingRadius = 70; ///< From the disc's middle to the tabs' middles
        inline constexpr float kFirstAngle = 45; ///< Degrees up from the right, of slot 0
        inline constexpr float kSlotAngle = 30;  ///< Between neighbouring slots, going round to the left
        inline constexpr float kTabSize = 27;    ///< A tab's face at rest
        inline constexpr float kTabGrown = 40;   ///< Under the pointer
        inline constexpr float kIconSize = 40;   ///< Of the tab icons' own pictures
        inline constexpr float kCloseSize = 14;
        inline constexpr float kCloseRight = 9;    ///< From the screen's right edge
        inline constexpr float kCloseBottom = 9;   ///< From the screen's bottom edge
        inline constexpr float kStripOverlap = 13; ///< Of the strip under the disc
        inline constexpr float kStripBottom = 6;   ///< From the screen's bottom edge
        inline constexpr float kStripExtra = 31;   ///< Of the strip's width beyond its content's
        inline constexpr float kStripNarrowest = 131;
        inline constexpr float kStripHeight = 130; ///< Of the strip, whatever its content
        inline constexpr float kBodyLeft = 16;     ///< Of the content, from the strip's left edge
        inline constexpr float kBodyTop = 12;      ///< From the strip's top edge
        inline constexpr float kPageWidth = 310;   ///< Of a page of lines
        inline constexpr float kLineHeight = 15;
        inline constexpr float kValueColumn = 185;  ///< From the page's left edge to where values start
        inline constexpr float kSectionHeight = 15; ///< Of the row of section buttons
        inline constexpr float kPlotHeight = 64;
        inline constexpr float kPairValue = 50;  ///< Of the value of a line paired with another on its row
        inline constexpr float kPairGap = 6;     ///< Between the two half lines of a row
        inline constexpr float kChoiceSize = 22; ///< A choice button
        inline constexpr float kChoiceIconScale = 0.684f;
        inline constexpr float kChoiceRadius = 25; ///< From the tab's middle to its choices'
        /// Radians either side of the line out from the disc through the tab, for two and for three choices.
        inline constexpr float kTwoChoiceSpread = 0.35f;
        inline constexpr float kThreeChoiceSpread = 0.7f;
    } // namespace info_panel

    struct InfoLine
    {
        std::u16string label;
        std::u16string value;
        bool half = false; ///< Short enough to share its row with the next half line
    };

    struct InfoSeries
    {
        std::u16string name;
        std::vector<float> values;
    };

    /// A tab's content: a title, a row of buttons for its sections when it has more than one, lines of a
    /// label and a value, a note under them and a graph.
    struct InfoPage
    {
        std::u16string title;
        std::u16string heading; ///< Under the title, such as what kind of thing it is
        std::vector<std::u16string> sections;
        std::size_t section = 0; ///< The one shown
        std::vector<InfoLine> lines;

        std::u16string note;
        std::vector<InfoSeries> graph;
    };

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
        /// Fills the body with `page`: its title, its lines in two columns and its note. An empty page hides the
        /// strip.
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
        void mark_picked();
        void open_choices(std::size_t tab);
        void close_choices();

        InfoPanelStyle style_;
        std::vector<Button*> tabs_;
        std::vector<int> slots_;
        std::vector<std::vector<InfoChoice>> tab_choices_;
        std::vector<Button*> choices_;
        std::optional<std::size_t> choosing_;
        std::optional<std::pair<std::size_t, std::size_t>> choice_now_;
        std::vector<Button*> sections_;
        std::optional<std::size_t> section_now_;
        Button* close_ = nullptr;
        Widget* body_ = nullptr;
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
