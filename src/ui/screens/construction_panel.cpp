// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/construction_panel.hpp"

#include "ui/skin_library.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace rct3::ui
{

    namespace
    {
        namespace cp = construction_panel;

        constexpr std::size_t kNamedSlots = cp::kNamedRows * cp::kNamedColumns;

        /// The panel's banks as the dial places them: level and leaning to the left down its right side, as
        /// a car is seen from the front.
        constexpr std::array<Point, cp::kBanks> kBankSpots { {
            { 38, 8 },  // Hardest left
            { 44, 21 }, //
            { 38, 36 }, // Least left
            { 22, 41 }, // Level
            { 6, 36 },  // Least right
            { 2, 21 },  //
            { 7, 7 },   // Hardest right
            { 22, 1 },  // Upside down
        } };

        std::string_view suffix(IconState state)
        {
            switch (state)
            {
                case IconState::On:
                    return "On";
                case IconState::Off:
                    return "Off";
                case IconState::Dis:
                    break;
            }
            return "Dis";
        }
    } // namespace

    std::string icon_skin(std::string_view base, IconState state)
    {
        return std::string(base) + std::string(suffix(state));
    }

    std::array<Point, construction_panel::kBanks> bank_spots()
    {
        return kBankSpots;
    }

    float dial_left(std::size_t toggles)
    {
        // Toggles fill a column of two before the dial moves on by a step.
        return cp::kToggleLeft + cp::kStep * static_cast<float>((toggles + 1) / 2);
    }

    ConstructionPanel::ConstructionPanel(std::string name, const ConstructionPanelStyle& style, std::u16string chain_help, std::u16string demolish_help)
        : Widget(std::move(name))
        , style_(style)
    {
        placement = top_left(0, 0, cp::kWidth, cp::kHeight);
        const auto picture = [&](std::string widget) { return &add<Button>(std::move(widget), std::string { }); };
        for (std::size_t i = 0; i < cp::kCurves; ++i)
        {
            curves_.at(i) = picture(style.curves.at(i).empty() ? std::format("curve{}", i) : style.curves.at(i));
            curves_.at(i)->placement = top_left(cp::kRowLeft + cp::kStep * static_cast<float>(i), cp::kCurveTop, cp::kCurveSize.x, cp::kCurveSize.y);
        }
        for (std::size_t i = 0; i < cp::kSlopes; ++i)
        {
            slopes_.at(i) = picture(style.slopes.at(i).empty() ? std::format("slope{}", i) : style.slopes.at(i));
            slopes_.at(i)->placement = top_left(cp::kRowLeft + cp::kStep * static_cast<float>(i), cp::kSlopeTop, cp::kSlopeSize.x, cp::kSlopeSize.y);
        }
        for (std::size_t i = 0; i < cp::kBanks; ++i)
            banks_.at(i) = picture(style.banks.at(i).empty() ? std::format("bank{}", i) : style.banks.at(i));
        chain_ = picture("chain");
        chain_->tooltip = std::move(chain_help);
        demolish_ = picture("demolish");
        demolish_->icon = style.demolish;
        demolish_->tooltip = std::move(demolish_help);
        demolish_->placement = top_left(cp::kDemolishAt.x, cp::kDemolishAt.y, cp::kDemolishSize.x, cp::kDemolishSize.y);
        complete_ = picture("complete");
        complete_->icon = style.complete;
        complete_->placement = top_left(cp::kCompleteAt.x, cp::kCompleteAt.y, cp::kCompleteSize.x, cp::kCompleteSize.y);
        new_segment_ = picture("new_segment");
        new_segment_->icon = style.new_segment;
        new_segment_->placement = top_left(cp::kNewSegmentAt.x, cp::kNewSegmentAt.y, cp::kNewSegmentSize.x, cp::kNewSegmentSize.y);
        new_segment_->visible = false;

        const auto label = [&](std::string widget, float top) {
            Label& l = add<Label>(std::move(widget), std::u16string { }, style.font);
            l.style.colour = style.text_colour;
            l.style.vertical = Align::Centre;
            l.placement = top_left(cp::kLabelLeft, top, cp::kLabelWidth, cp::kLabelHeight);
            return &l;
        };
        piece_ = label("piece", cp::kNameTop);
        cost_ = label("cost", cp::kCostTop);

        const float column = cp::kNamedWidth / static_cast<float>(cp::kNamedColumns);
        for (std::size_t i = 0; i < kNamedSlots; ++i)
        {
            Button& b = add<Button>(std::format("named{}", i), style.named_button);
            b.toggle = true;
            b.font = style.font;
            b.text_inset = style.named_inset;
            const auto col = static_cast<float>(i % cp::kNamedColumns);
            const auto row = static_cast<float>(i / cp::kNamedColumns);
            b.placement = top_left(cp::kNamedLeft + col * column, cp::kNamedTop + row * cp::kNamedRowHeight, column - 2, cp::kNamedRowHeight - 2);
            named_.at(i) = &b;
        }
        page_up_ = &add<Button>("page_up", std::string { }, style.page_up);
        page_up_->placement = top_left(cp::kPagerLeft, cp::kNamedTop, cp::kPagerSize, cp::kPagerSize);
        page_down_ = &add<Button>("page_down", std::string { }, style.page_down);
        page_down_->placement = top_left(cp::kPagerLeft, cp::kNamedTop + cp::kNamedRowHeight * static_cast<float>(cp::kNamedRows) - cp::kPagerSize, cp::kPagerSize, cp::kPagerSize);
        for (std::size_t i = 0; i < cp::kSpecials; ++i)
        {
            Button& b = *picture(std::format("special{}", i));
            b.toggle = true;
            b.placement = top_left(cp::kSpecialAt.x + cp::kSpecialStep * static_cast<float>(i / 2), cp::kSpecialAt.y + cp::kSpecialStep * static_cast<float>(i % 2), cp::kSpecialSize, cp::kSpecialSize);
            specials_.at(i) = &b;
        }
        speed_icon_ = picture("speed_icon");
        speed_icon_->icon = style.speed_icon;
        speed_icon_->inert = true;
        speed_icon_->placement = top_left(cp::kSpeedAt.x, cp::kSpeedAt.y, cp::kSpeedIconSize, cp::kSpeedIconSize);
        speed_ = &add<Spinner>("speed", style.speed);
        speed_->decimals = 2;
        speed_->placement = top_left(cp::kSpeedAt.x + cp::kSpeedIconSize + 2, cp::kSpeedAt.y, cp::kSpeedSize.x, cp::kSpeedSize.y);
        show({ });
    }

    void ConstructionPanel::show(ConstructionView view)
    {
        view_ = std::move(view);
        // Pieces with a picture take the picture buttons while they last; the rest go in the list.
        listed_.clear();
        pictured_.clear();
        for (std::size_t i = 0; i < view_.named.size(); ++i)
        {
            const bool pictured = i < view_.named_icons.size() && !view_.named_icons[i].empty() && pictured_.size() < cp::kSpecials;
            (pictured ? pictured_ : listed_).push_back(i);
        }
        const std::size_t pages = std::max<std::size_t>(1, (listed_.size() + kNamedSlots - 1) / kNamedSlots);
        page_ = std::min(page_, pages - 1);
        lay_out();
        restyle();
    }

    void ConstructionPanel::lay_out()
    {
        const std::size_t toggles = view_.chain ? 1 : 0;
        chain_->visible = view_.chain.has_value();
        chain_->placement = top_left(cp::kToggleLeft, cp::kSlopeTop + cp::kSlopeSize.y - cp::kToggleSize.y, cp::kToggleSize.x, cp::kToggleSize.y);
        const float dial = dial_left(toggles);
        for (std::size_t i = 0; i < cp::kBanks; ++i)
            banks_.at(i)->placement = top_left(dial + kBankSpots.at(i).x, cp::kDialTop + kBankSpots.at(i).y, cp::kBankSize, cp::kBankSize);
    }

    void ConstructionPanel::restyle()
    {
        for (std::size_t i = 0; i < cp::kCurves; ++i)
        {
            curves_.at(i)->icon = icon_skin(style_.curves.at(i), view_.curves.at(i));
            curves_.at(i)->inert = view_.curves.at(i) == IconState::Dis;
        }
        for (std::size_t i = 0; i < cp::kSlopes; ++i)
        {
            slopes_.at(i)->icon = icon_skin(style_.slopes.at(i), view_.slopes.at(i));
            slopes_.at(i)->inert = view_.slopes.at(i) == IconState::Dis;
        }
        for (std::size_t i = 0; i < cp::kBanks; ++i)
        {
            // The bank pictures have no greyed state: a bank not offered is left out.
            banks_.at(i)->icon = icon_skin(style_.banks.at(i), view_.banks.at(i) == IconState::On ? IconState::On : IconState::Off);
            banks_.at(i)->visible = view_.banks.at(i) != IconState::Dis;
        }
        if (view_.chain)
        {
            chain_->icon = icon_skin(style_.chain, *view_.chain);
            chain_->inert = *view_.chain == IconState::Dis;
        }
        demolish_->enabled = view_.can_demolish;
        complete_->enabled = view_.can_complete;
        new_segment_->visible = view_.new_segment.has_value();
        new_segment_->enabled = view_.new_segment.value_or(false);
        piece_->text = view_.piece;
        cost_->text = view_.cost;
        for (std::size_t i = 0; i < kNamedSlots; ++i)
        {
            const std::size_t row = page_ * kNamedSlots + i;
            Button& b = *named_.at(i);
            b.visible = row < listed_.size();
            if (!b.visible)
                continue;
            const std::size_t index = listed_[row];
            b.text = view_.named[index];
            b.tooltip = view_.named[index];
            b.on = view_.named_on == index;
        }
        for (std::size_t i = 0; i < cp::kSpecials; ++i)
        {
            Button& b = *specials_.at(i);
            b.visible = i < pictured_.size();
            if (!b.visible)
                continue;
            b.icon = view_.named_icons[pictured_[i]];
            b.tooltip = view_.named[pictured_[i]];
            b.on = view_.named_on == pictured_[i];
            // With no face to light, the picked one shows by the others fading.
            b.icon_colour = b.on || !view_.named_on ? kWhite : hex("#ffffff66");
        }
        const bool paged = listed_.size() > kNamedSlots;
        page_up_->visible = paged;
        page_down_->visible = paged;
        page_up_->enabled = page_ > 0;
        page_down_->enabled = (page_ + 1) * kNamedSlots < listed_.size();
        speed_icon_->visible = speed_->visible = view_.speed.has_value();
        speed_->maximum = view_.speed_most;
        speed_->unit = u" " + view_.speed_unit;
        // Set only when it moved, so a number being typed is not written over.
        if (view_.speed && *view_.speed != speed_->value())
            speed_->set_value(*view_.speed);
    }

    std::optional<ConstructionEvent> ConstructionPanel::take_event()
    {
        if (events_.empty())
            return std::nullopt;
        const ConstructionEvent e = events_.front();
        events_.erase(events_.begin());
        return e;
    }

    void ConstructionPanel::update(const UpdateContext&)
    {
        // A picture button clicked: picking what is picked already builds; what is not offered does nothing.
        const auto pictures = [&](const auto& buttons, const auto& states, ConstructionAction action) {
            for (std::size_t i = 0; i < buttons.size(); ++i)
            {
                if (!buttons.at(i)->take_click())
                    continue;
                if (states.at(i) == IconState::On && !view_.named_on)
                    events_.push_back({ ConstructionAction::Build, 0 });
                else if (states.at(i) != IconState::Dis)
                    events_.push_back({ action, i });
            }
        };
        pictures(curves_, view_.curves, ConstructionAction::Curve);
        pictures(slopes_, view_.slopes, ConstructionAction::Slope);
        pictures(banks_, view_.banks, ConstructionAction::Bank);
        if (chain_->take_click() && view_.chain && *view_.chain != IconState::Dis)
            events_.push_back({ ConstructionAction::Chain, 0 });
        if (demolish_->take_click() && view_.can_demolish)
            events_.push_back({ ConstructionAction::Demolish, 0 });
        if (complete_->take_click() && view_.can_complete)
            events_.push_back({ ConstructionAction::Complete, 0 });
        if (new_segment_->take_click() && view_.new_segment.value_or(false))
            events_.push_back({ ConstructionAction::NewSegment, 0 });
        // A named piece clicked: picked, or built when it is picked already.
        const auto named = [&](Button& b, std::optional<std::size_t> index) {
            if (!b.take_click())
                return;
            if (index)
                events_.push_back({ view_.named_on == index ? ConstructionAction::Build : ConstructionAction::Named, view_.named_on == index ? 0 : *index });
            restyle(); // A toggle turns itself over; the view says what is picked
        };
        for (std::size_t i = 0; i < kNamedSlots; ++i)
        {
            const std::size_t row = page_ * kNamedSlots + i;
            named(*named_.at(i), row < listed_.size() ? std::optional(listed_[row]) : std::nullopt);
        }
        for (std::size_t i = 0; i < cp::kSpecials; ++i)
            named(*specials_.at(i), i < pictured_.size() ? std::optional(pictured_[i]) : std::nullopt);
        if (speed_->take_change() && view_.speed)
            events_.push_back({ ConstructionAction::Speed, static_cast<std::size_t>(std::max(0, speed_->value())) });
        if (page_up_->take_click() && page_ > 0)
        {
            --page_;
            restyle();
        }
        if (page_down_->take_click() && (page_ + 1) * kNamedSlots < listed_.size())
        {
            ++page_;
            restyle();
        }
    }

    void ConstructionPanel::draw(DrawContext& context) const
    {
        const Rect r = rect();
        const float dial = dial_left(view_.chain ? 1 : 0);
        draw_skin(context.list, context.skins.find(style_.dial), { r.x0 + dial, r.y0 + cp::kDialTop, r.x0 + dial + cp::kDialSize, r.y0 + cp::kDialTop + cp::kDialSize });
    }

} // namespace rct3::ui
