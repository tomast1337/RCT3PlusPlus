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

    float specials_left(std::size_t toggles)
    {
        return std::max(cp::kSpecialAt.x, dial_left(toggles) + cp::kDialSize + 4);
    }

    ConstructionPanel::ConstructionPanel(std::string name, const ConstructionPanelStyle& style, std::array<std::u16string, cp::kToggles> toggle_helps, std::u16string demolish_help)
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
        for (std::size_t i = 0; i < cp::kToggles; ++i)
        {
            toggles_.at(i) = picture(std::format("toggle{}", i));
            toggles_.at(i)->tooltip = std::move(toggle_helps.at(i));
        }
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
        split_ = picture("split");
        split_->visible = false;
        heights_ = picture("heights");
        heights_->placement = top_left(cp::kHeightsAt.x, cp::kHeightsAt.y, cp::kHeightsSize.x, cp::kHeightsSize.y);

        const auto label = [&](std::string widget, float top) {
            Label& l = add<Label>(std::move(widget), std::u16string { }, style.font);
            l.style.colour = style.text_colour;
            l.style.vertical = Align::Centre;
            l.placement = top_left(cp::kLabelLeft, top, cp::kLabelWidth, cp::kLabelHeight);
            return &l;
        };
        piece_ = label("piece", cp::kNameTop);
        cost_ = label("cost", cp::kCostTop);

        list_ = &add<ListBox>("list", style.list, style.list_bar);
        for (std::size_t i = 0; i < cp::kSpecials; ++i)
        {
            Button& b = *picture(std::format("special{}", i));
            b.toggle = true;
            specials_.at(i) = &b;
        }
        constexpr std::array<std::string_view, cp::kParams> kParamNames { "car_turn", "holding_time", "speed" };
        for (std::size_t i = 0; i < cp::kParams; ++i)
        {
            param_icons_.at(i) = picture(std::format("{}_icon", kParamNames.at(i)));
            param_icons_.at(i)->icon = style.param_icons.at(i);
            param_icons_.at(i)->inert = true;
            params_.at(i) = &add<Spinner>(std::string(kParamNames.at(i)), style.param);
        }
        facing_ = picture("facing");
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
            const bool pictured = !view_.named[i].icon.empty() && pictured_.size() < cp::kSpecials;
            (pictured ? pictured_ : listed_).push_back(i);
        }
        lay_out();
        restyle();
    }

    void ConstructionPanel::lay_out()
    {
        // The toggles the track has fill two rows by columns, the slope row's first, then the curve row's.
        std::size_t toggles = 0;
        for (std::size_t i = 0; i < cp::kToggles; ++i)
        {
            if (!view_.toggles.at(i))
                continue;
            const float top = toggles % 2 == 0 ? cp::kSlopeTop + cp::kSlopeSize.y - cp::kToggleSize.y : cp::kCurveTop;
            toggles_.at(i)->placement = top_left(cp::kToggleLeft + cp::kStep * static_cast<float>(toggles / 2), top, cp::kToggleSize.x, cp::kToggleSize.y);
            ++toggles;
        }
        // The cars' facing takes the next slot.
        if (view_.facing)
        {
            const float top = toggles % 2 == 0 ? cp::kSlopeTop + cp::kSlopeSize.y - cp::kToggleSize.y : cp::kCurveTop;
            facing_->placement = top_left(cp::kToggleLeft + cp::kStep * static_cast<float>(toggles / 2), top, cp::kToggleSize.x, cp::kToggleSize.y);
            ++toggles;
        }
        // The setting boxes stand right of the price, the last ending where the price's row does; the price
        // keeps what is left of the row.
        float right = cp::kParamsRight;
        for (std::size_t i = cp::kParams; i-- > 0;)
        {
            if (!view_.params.at(i))
                continue;
            const float left = right - cp::kParamWidths.at(i) - cp::kParamIconSize - 2;
            param_icons_.at(i)->placement = top_left(left, cp::kParamsTop, cp::kParamIconSize, cp::kParamIconSize);
            params_.at(i)->placement = top_left(left + cp::kParamIconSize + 2, cp::kParamsTop, cp::kParamWidths.at(i), cp::kParamHeight);
            right = left - cp::kParamGap;
        }
        cost_->placement = top_left(cp::kLabelLeft, cp::kCostTop, std::min(cp::kLabelWidth, right - cp::kLabelLeft), cp::kLabelHeight);
        const float dial = dial_left(toggles);
        for (std::size_t i = 0; i < cp::kBanks; ++i)
            banks_.at(i)->placement = top_left(dial + kBankSpots.at(i).x, cp::kDialTop + kBankSpots.at(i).y, cp::kBankSize, cp::kBankSize);
        const float specials = specials_left(toggles);
        for (std::size_t i = 0; i < cp::kSpecials; ++i)
            specials_.at(i)->placement = top_left(specials + cp::kSpecialStep * static_cast<float>(i / 2), cp::kSpecialAt.y + cp::kSpecialStep * static_cast<float>(i % 2), cp::kSpecialSize, cp::kSpecialSize);
        const float split = view_.new_segment ? cp::kNewSegmentAt.x + cp::kNewSegmentSize.x + 4 : cp::kNewSegmentAt.x;
        split_->placement = top_left(split, cp::kNewSegmentAt.y, cp::kSplitSize.x, cp::kSplitSize.y);
        const float buttons_right = view_.split ? split + cp::kSplitSize.x : view_.new_segment ? cp::kNewSegmentAt.x + cp::kNewSegmentSize.x : 0.0f;
        list_left_ = std::max({ cp::kListLeft, specials + 2 * cp::kSpecialStep + 6, buttons_right > 0 ? buttons_right + 6 : 0.0f });
        list_->placement = top_left(list_left_, cp::kListTop, cp::kListRight - list_left_, cp::kListHeight);
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
        for (std::size_t i = 0; i < cp::kToggles; ++i)
        {
            const auto& state = view_.toggles.at(i);
            toggles_.at(i)->visible = state && *state != IconState::Dis;
            if (toggles_.at(i)->visible)
                toggles_.at(i)->icon = icon_skin(style_.toggles.at(i), *state);
        }
        demolish_->enabled = view_.can_demolish;
        complete_->enabled = view_.can_complete;
        new_segment_->visible = view_.new_segment.has_value();
        new_segment_->enabled = view_.new_segment.value_or(false);
        split_->visible = view_.split.has_value();
        if (view_.split)
            split_->icon = style_.split.at(std::min(*view_.split, cp::kSplitPictures - 1));
        heights_->icon = view_.heights || view_.heights_locked ? style_.heights_on : style_.heights;
        heights_->tooltip = view_.heights_locked ? heights_locked_help : heights_help;
        piece_->text = view_.piece;
        cost_->text = view_.cost;
        list_->items.clear();
        list_->icons.clear();
        list_->greyed.clear();
        std::optional<std::size_t> picked;
        for (const std::size_t index : listed_)
        {
            const NamedPiece& piece = view_.named[index];
            if (view_.named_on == index)
                picked = list_->items.size();
            list_->items.push_back(piece.name);
            list_->icons.emplace_back(piece.marks.begin(), piece.marks.end());
            list_->greyed.push_back(!piece.offered);
        }
        // Selected only when it changes, so the player can scroll away from it.
        if (list_->selected() != picked)
            list_->select(picked);
        for (std::size_t i = 0; i < cp::kSpecials; ++i)
        {
            Button& b = *specials_.at(i);
            b.visible = i < pictured_.size();
            if (!b.visible)
                continue;
            const NamedPiece& piece = view_.named[pictured_[i]];
            b.icon = piece.icon;
            b.tooltip = piece.name;
            b.on = view_.named_on == pictured_[i];
            b.enabled = piece.offered;
            // With no face to light, the picked one shows by the others fading.
            b.icon_colour = b.on || !view_.named_on ? kWhite : hex("#ffffff66");
        }
        for (std::size_t i = 0; i < cp::kParams; ++i)
        {
            const std::optional<ParamBox>& box = view_.params.at(i);
            Spinner& s = *params_.at(i);
            param_icons_.at(i)->visible = s.visible = box.has_value();
            if (!box)
                continue;
            s.minimum = box->least;
            s.maximum = box->most;
            s.decimals = box->decimals;
            s.unit = box->unit.empty() ? std::u16string { } : u" " + box->unit;
            // Set only when it moved, so a number being typed is not written over.
            if (box->value != s.value())
                s.set_value(box->value);
        }
        facing_->visible = view_.facing.has_value();
        if (view_.facing)
            facing_->icon = std::format("{}{}", style_.facing, *view_.facing + 1);
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
        for (std::size_t i = 0; i < cp::kToggles; ++i)
        {
            if (toggles_.at(i)->take_click() && view_.toggles.at(i).value_or(IconState::Dis) != IconState::Dis)
                events_.push_back({ ConstructionAction::Toggle, i });
        }
        if (demolish_->take_click() && view_.can_demolish)
            events_.push_back({ ConstructionAction::Demolish, 0 });
        if (complete_->take_click() && view_.can_complete)
            events_.push_back({ ConstructionAction::Complete, 0 });
        if (new_segment_->take_click() && view_.new_segment.value_or(false))
            events_.push_back({ ConstructionAction::NewSegment, 0 });
        if (split_->take_click() && view_.split)
            events_.push_back({ ConstructionAction::Split, 0 });
        if (heights_->take_click() && !view_.heights_locked)
            events_.push_back({ ConstructionAction::Heights, 0 });
        // A picture piece clicked: picked, or built when it is picked already.
        for (std::size_t i = 0; i < cp::kSpecials; ++i)
        {
            if (!specials_.at(i)->take_click())
                continue;
            if (i < pictured_.size() && view_.named[pictured_[i]].offered)
                events_.push_back({ view_.named_on == pictured_[i] ? ConstructionAction::Build : ConstructionAction::Named, view_.named_on == pictured_[i] ? 0 : pictured_[i] });
            restyle(); // A toggle turns itself over; the view says what is picked
        }
        // A listed piece pressed is picked; pressed twice quickly, built.
        if (list_->take_change() && list_->selected() && *list_->selected() < listed_.size())
            events_.push_back({ ConstructionAction::Named, listed_[*list_->selected()] });
        if (list_->take_activate())
            events_.push_back({ ConstructionAction::Build, 0 });
        for (std::size_t i = 0; i < cp::kParams; ++i)
        {
            if (params_.at(i)->take_change() && view_.params.at(i))
                events_.push_back({ ConstructionAction::Param, i, params_.at(i)->value() });
        }
        if (facing_->take_click() && view_.facing)
            events_.push_back({ ConstructionAction::Facing, 0 });
    }

    void ConstructionPanel::draw(DrawContext& context) const
    {
        const Rect r = rect();
        const float dial = dial_left(static_cast<std::size_t>(std::ranges::count_if(view_.toggles, [](const auto& t) { return t.has_value(); })));
        draw_skin(context.list, context.skins.find(style_.dial), { r.x0 + dial, r.y0 + cp::kDialTop, r.x0 + dial + cp::kDialSize, r.y0 + cp::kDialTop + cp::kDialSize });
        // The lines that part the buttons from the name below them, and both from the list.
        context.list.fill({ r.x0 + cp::kLabelLeft - 4, r.y0 + cp::kDividerTop, r.x0 + list_left_ - 3, r.y0 + cp::kDividerTop + 1 }, style_.divider);
        context.list.fill({ r.x0 + list_left_ - 3, r.y0, r.x0 + list_left_ - 2, r.y1 }, style_.divider);
    }

} // namespace rct3::ui
