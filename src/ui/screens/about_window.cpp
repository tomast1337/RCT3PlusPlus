// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/about_window.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        constexpr std::string_view kTitle = "ProgramInfoDialog_Title";
        constexpr std::string_view kInfoTab = "ProgramInfoDialog_ProgramInfoTabHelp";
        constexpr std::string_view kCreditsTab = "ProgramInfoDialog_CreditsPanelTabHelp";
        constexpr std::string_view kGameName = "ProgramInfo_Title";
        constexpr std::string_view kCopyright = "ProgramInfo_Copyright";                  ///< + 1, 2, ...
        constexpr std::string_view kBaseName = "RCT3_Name";                               ///< Over the base game's credits
        constexpr std::string_view kBaseCredits = "ProgramInfoDialog_GeneralCredits";     ///< + 0, 1, ...
        constexpr std::string_view kPackCredits = "ProgramInfoDialog_GeneralCredits_AOP"; ///< + pack, '_', 0, 1, ...
        /// The add-on packs' names, by number.
        constexpr std::array<std::string_view, 2> kPackNames = { "ContentPackName_SoakedAddOn", "ContentPackName_WildAddOn" };

        /// The credits under one key prefix, numbered from 0 until one is missing.
        void add_credits(std::vector<CreditLine>& out, const StringTable& strings, std::u16string heading, const std::string& prefix, random::RandomState& random, int& gaps, int& row)
        {
            if (!strings.contains(prefix + "0"))
                return;
            ++gaps;
            out.push_back({ std::move(heading), CreditLine::Kind::Heading, Align::Centre, 0, gaps, row++ });
            for (int k = 0;; ++k)
            {
                const std::string key = prefix + std::to_string(k);
                if (!strings.contains(key))
                    break;
                ++gaps;
                // Alternately from the right and the left edge, moved in by a random amount.
                const Align align = k % 2 == 0 ? Align::End : Align::Start;
                const auto inset = static_cast<float>(random::unit(random));
                bool first = true;
                for (const auto part : std::views::split(strings.get(key), u'|'))
                {
                    std::u16string text(part.begin(), part.end());
                    out.push_back({ std::move(text), first ? CreditLine::Kind::Role : CreditLine::Kind::Name, align, inset, gaps, row++ });
                    first = false;
                }
            }
        }

        /// Copies `prefix` + first, + first + 1, ... from `from` until one is missing.
        void copy_numbered(StringTable& to, const StringTable& from, const std::string& prefix, int first)
        {
            for (int k = first; from.contains(prefix + std::to_string(k)); ++k)
                to.add(prefix + std::to_string(k), std::u16string(from.get(prefix + std::to_string(k))));
        }

    } // namespace

    void add_game_credits(StringTable& to, const StringTable& game)
    {
        copy_numbered(to, game, std::string(kCopyright), 1);
        copy_numbered(to, game, std::string(kBaseCredits), 0);
        for (std::size_t pack = 0; pack < kPackNames.size(); ++pack)
            copy_numbered(to, game, std::string(kPackCredits) + std::to_string(pack) + "_", 0);
    }

    std::vector<std::u16string> about_info_lines(const StringTable& strings)
    {
        std::vector<std::u16string> lines { std::u16string(strings.get(kGameName)) };
        for (int n = 1;; ++n)
        {
            const std::string key = std::string(kCopyright) + std::to_string(n);
            if (!strings.contains(key))
                break;
            lines.emplace_back(strings.get(key));
        }
        return lines;
    }

    std::vector<CreditLine> credit_lines(const StringTable& strings, std::span<const int> packs, random::RandomState& random)
    {
        auto newest_first = packs | std::ranges::to<std::vector<int>>();
        std::ranges::sort(newest_first, std::greater { });
        std::vector<CreditLine> out;
        int gaps = 0;
        int row = 0;
        for (const int pack : newest_first)
        {
            if (pack < 0 || static_cast<std::size_t>(pack) >= kPackNames.size())
                continue;
            const std::string prefix = std::string(kPackCredits) + std::to_string(pack) + "_";
            add_credits(out, strings, std::u16string(strings.get(kPackNames.at(static_cast<std::size_t>(pack)))), prefix, random, gaps, row);
        }
        add_credits(out, strings, std::u16string(strings.get(kBaseName)), std::string(kBaseCredits), random, gaps, row);
        return out;
    }

    CreditsScroller::CreditsScroller(std::string name, std::vector<CreditLine> lines, const AboutWindowStyle& style)
        : Widget(std::move(name))
        , lines_(std::move(lines))
        , style_(style)
    {
        clip_children = true;
    }

    float CreditsScroller::line_y(const CreditLine& line, float line_height) const
    {
        // No gap above the first credit: the roll starts with it at the top.
        return static_cast<float>(std::max(0, line.gaps - 1)) * style_.credit_gap + static_cast<float>(line.row) * line_height;
    }

    void CreditsScroller::update(const UpdateContext& context)
    {
        const auto f = context.fonts ? context.fonts->get(style_.font) : std::nullopt;
        if (f && !lines_.empty())
            length_ = line_y(lines_.back(), f->line_height()) + f->line_height();

        // Up until the last line is in sight at the bottom, then from the top again.
        const float end = length_ - rect().height();
        if (end <= 0)
        {
            offset_ = 0;
            return;
        }
        offset_ += context.elapsed * style_.roll_speed * (pressed() ? style_.fast_factor : 1.0f);
        if (offset_ > end)
            offset_ = 0;
    }

    void CreditsScroller::draw(DrawContext& context) const
    {
        const auto f = context.fonts.get(style_.font);
        if (!f)
            return;
        if (!context.list.push_clip(rect()))
        {
            context.list.pop_clip();
            return;
        }
        const Rect& r = rect();
        const float w = r.width();
        const float top = std::round(r.y0 - offset_);
        for (const CreditLine& line : lines_)
        {
            const float y = top + line_y(line, f->line_height());
            if (y + f->line_height() < r.y0)
                continue;
            if (y > r.y1)
                break;
            Rect box { r.x0, y, r.x1, y + f->line_height() };
            const float in = std::round(line.inset * style_.most_inset * w);
            if (line.align == Align::Start)
                box.x0 += in;
            else if (line.align == Align::End)
                box.x1 -= in;
            const Color colour = line.kind == CreditLine::Kind::Name ? style_.text_colour : style_.role_colour;
            draw_text(context.list, *f, line.text, box, { colour, line.align, Align::Start, false });
        }
        context.list.pop_clip();
    }

    GameInfoBox::GameInfoBox(std::string name, const AboutWindowStyle& style, const StringTable& strings, const AddOnPacks& packs, random::Seed seed)
        : Window(std::move(name), style.window, style.page_width + style.window.content_inset.x0 + style.window.content_inset.x1, style.tab_bar_height + style.page_height + style.window.content_inset.y0 + style.window.content_inset.y1)
        , style_(style)
    {
        title = strings.get(kTitle);
        icon = style.icon;
        Widget& body = content();

        caption_ = &body.add<Label>("caption", std::u16string { }, style.caption_font);
        caption_->style.vertical = Align::Centre;
        caption_->style.colour = style.caption_colour;
        caption_->placement = top_left(style.caption_x, 0, style.caption_width, style.tab_size);

        const std::array<std::pair<std::string, std::string_view>, kAboutPageCount> tabs = { { { style.icon, kInfoTab }, { style.credits_icon, kCreditsTab } } };
        for (std::size_t i = 0; i < kAboutPageCount; ++i)
        {
            // The picture is the button, drawn at the tab's size.
            auto& tab = body.add<Button>(tabs.at(i).first, tabs.at(i).first);
            tab.toggle = true;
            tab.tooltip = strings.get(tabs.at(i).second);
            const float right = style.tab_size * static_cast<float>(kAboutPageCount - i);
            tab.placement = top_left(style.page_width - right, 0, style.tab_size, style.tab_size);
            tabs_.at(i) = &tab;
            captions_.at(i) = tab.tooltip;

            auto& page = body.add<Widget>(std::string(tabs.at(i).second));
            page.placement = top_left(0, style.tab_bar_height, style.page_width, style.page_height);
            pages_.at(i) = &page;
        }

        Widget& info = *pages_.at(static_cast<std::size_t>(AboutPage::Info));
        const std::vector<std::u16string> lines = about_info_lines(strings);
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            auto& label = info.add<Label>("line" + std::to_string(i), lines[i], i == 0 ? style.title_font : style.font);
            label.style.colour = style.text_colour;
            label.style.wrap = true;
            info_.push_back(&label);
        }
        place_info();

        random::RandomState random = random::seeded(seed);
        credits_ = &pages_.at(static_cast<std::size_t>(AboutPage::Credits))->add<CreditsScroller>("roll", credit_lines(strings, packs, random), style);
        credits_->placement = fill();
        show_page(AboutPage::Info);
    }

    void GameInfoBox::place_info()
    {
        // Each line under the one before, as tall as it wraps to.
        float y = 0;
        for (Label* label : info_)
        {
            const float height = std::max(1.0f, label->extent().height());
            label->placement = top_left(0, y, style_.page_width, height);
            y += height + style_.line_gap;
        }
        info_height_ = std::max(0.0f, y - style_.line_gap);
    }

    void GameInfoBox::fit_page()
    {
        // The info page as tall as its lines, as the game sizes it to them; the credits at their own height.
        const float page = page_ == AboutPage::Info && info_height_ > 0 ? info_height_ : style_.page_height;
        const Rect& in = style_.window.content_inset;
        set_height(in.y0 + style_.tab_bar_height + page + in.y1);
        pages_.at(static_cast<std::size_t>(page_))->placement = top_left(0, style_.tab_bar_height, style_.page_width, page);
    }

    void GameInfoBox::show_page(AboutPage page)
    {
        if (page == AboutPage::Credits && page_ != AboutPage::Credits)
            credits_->restart();
        page_ = page;
        for (std::size_t i = 0; i < kAboutPageCount; ++i)
        {
            const bool current = i == static_cast<std::size_t>(page);
            pages_.at(i)->visible = current;
            tabs_.at(i)->on = current;
        }
        caption_->text = captions_.at(static_cast<std::size_t>(page));
    }

    void GameInfoBox::update(const UpdateContext& context)
    {
        Window::update(context);
        for (std::size_t i = 0; i < kAboutPageCount; ++i)
        {
            if (tabs_.at(i)->take_click())
                show_page(static_cast<AboutPage>(i));
        }
        show_page(page_); // A click on the open tab would have turned it off
        place_info();
        fit_page();
    }

} // namespace rct3::ui
