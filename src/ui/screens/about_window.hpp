// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window that tells about the game, opened from the park's Files menu. Two picture tabs in its top-right
// corner pick a page, whose name shows in blue on the left of them:
//
// - Program info: the game's name, then its copyright lines, numbered from 1 in the string table and read
//   until the first number that is missing. The window is as tall as they are.
// - Credits: the people who made the game, rolling slowly upwards and starting again from the top once the
//   last name has gone by. Holding the pointer down on them rolls them faster. Each installed add-on pack
//   comes first under its own name, the base game last. Each credit is one string, its parts split by '|':
//   the first part names the role and shows in blue, the others are the people, one per line. Credits sit
//   alternately towards the right and the left edge, each moved in from it by a random amount, so the roll
//   reads as a loose zig-zag.
//
// All text comes from the string table at run time. The copyright lines and credits are the game's own words,
// so our language files leave them out and they are copied from the game's installed text (add_game_credits).
// The layout is ours, after the game's.
#pragma once

#include "random/generator.hpp"
#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rct3::ui
{

    /// One line of the rolling credits.
    struct CreditLine
    {
        enum class Kind : std::uint8_t
        {
            Heading, ///< A pack's name, or the game's, over its credits
            Role,
            Name,
        };
        std::u16string text;
        Kind kind = Kind::Name;
        Align align = Align::Start; ///< Which edge the line is moved in from; Centre for headings
        float inset = 0;            ///< From that edge, as a fraction of the roll's width
        int gaps = 0;               ///< Credit gaps above the line
        int row = 0;                ///< Lines above the line
    };

    /// Which add-on packs are installed, by the game's numbering (0 and 1).
    using AddOnPacks = std::vector<int>;

    /// The game's name and the copyright lines, as the info page shows them.
    [[nodiscard]] std::vector<std::u16string> about_info_lines(const StringTable& strings);

    /// Copies the copyright lines and every credit from `game`, the installed game's strings, into `to`.
    void add_game_credits(StringTable& to, const StringTable& game);

    /// Every credit line in order: the packs in `packs` that have credits, newest first, then the base game.
    /// The insets come from `random`.
    [[nodiscard]] std::vector<CreditLine> credit_lines(const StringTable& strings, std::span<const int> packs, random::RandomState& random);

    struct AboutWindowStyle
    {
        WindowStyle window;
        std::string icon;         ///< In the window's corner and on the info tab
        std::string credits_icon; ///< On the credits tab
        float page_width = 530;
        float page_height = 250;
        float tab_size = 20; ///< The tab pictures are drawn this big, half their own size
        float tab_bar_height = 22;
        float caption_x = 59; ///< Right of the window's icon, under the title
        float caption_width = 280;
        FontRole caption_font = FontRole::Small;
        Color caption_colour = hex("#2484be");
        FontRole title_font = FontRole::Medium; ///< Of the game's name on the info page
        FontRole font = FontRole::Small;
        Color text_colour = hex("#000000");
        Color role_colour = hex("#2484be");
        float line_gap = 5;       ///< Between the info page's lines
        float credit_gap = 25;    ///< Above each credit
        float roll_speed = 40;    ///< Units a second
        float fast_factor = 3;    ///< While the pointer holds the roll down
        float most_inset = 0.35f; ///< Of the width: the furthest a credit is moved in from its edge
    };

    enum class AboutPage : std::uint8_t
    {
        Info,
        Credits,
        Count_,
    };
    inline constexpr std::size_t kAboutPageCount = static_cast<std::size_t>(AboutPage::Count_);

    /// The credits, rolling within their rectangle.
    class CreditsScroller : public Widget
    {
    public:
        CreditsScroller(std::string name, std::vector<CreditLine> lines, const AboutWindowStyle& style);

        /// How far the roll has moved up, in units.
        [[nodiscard]] float offset() const
        {
            return offset_;
        }
        /// Back to the top, as when the page opens.
        void restart()
        {
            offset_ = 0;
        }
        /// From the top of the first line to the bottom of the last, in units; 0 before the first update.
        [[nodiscard]] float length() const
        {
            return length_;
        }
        [[nodiscard]] const std::vector<CreditLine>& lines() const
        {
            return lines_;
        }

        /// Pressing holds it down, which rolls it faster.
        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        [[nodiscard]] float line_y(const CreditLine& line, float line_height) const;

        std::vector<CreditLine> lines_;
        AboutWindowStyle style_;
        float offset_ = 0;
        float length_ = 0;
    };

    class GameInfoBox : public Window
    {
    public:
        GameInfoBox(std::string name, const AboutWindowStyle& style, const StringTable& strings, const AddOnPacks& packs, random::Seed seed);

        [[nodiscard]] AboutPage page() const
        {
            return page_;
        }
        void show_page(AboutPage page);

        [[nodiscard]] Button& tab(AboutPage page) const
        {
            return *tabs_.at(static_cast<std::size_t>(page));
        }
        [[nodiscard]] const Label& caption() const
        {
            return *caption_;
        }
        [[nodiscard]] const std::vector<Label*>& info_lines() const
        {
            return info_;
        }
        [[nodiscard]] CreditsScroller& credits() const
        {
            return *credits_;
        }

        void update(const UpdateContext& context) override;

    private:
        void place_info();
        /// Makes the window as tall as the open page.
        void fit_page();

        AboutWindowStyle style_;
        std::array<Widget*, kAboutPageCount> pages_ { };
        std::array<Button*, kAboutPageCount> tabs_ { };
        std::array<std::u16string, kAboutPageCount> captions_;
        Label* caption_ = nullptr;
        std::vector<Label*> info_;
        CreditsScroller* credits_ = nullptr;
        float info_height_ = 0; // Of the info page's lines, as of the last update
        AboutPage page_ = AboutPage::Info;
    };

} // namespace rct3::ui
