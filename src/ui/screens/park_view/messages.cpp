// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/park_view/messages.hpp"

#include "core/types.hpp"
#include "simulation/awards.hpp"
#include "strings/unicode.hpp"
#include "ui/screens/info/attraction.hpp"
#include "ui/text_layout.hpp"
#include "ui/thought_text.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::ui::screens::park_view
{

    namespace
    {

        using namespace message_drawer;

        constexpr std::string_view kDragHelp = "MessageConsoleClickDrag";
        constexpr std::string_view kOlderHelp = "TT_GUI2_MessageConsolePrevoiusMessages"; // Misspelt in the game
        constexpr std::string_view kNewerHelp = "TT_GUI2_MessageConsoleNextMessages";
        constexpr std::u16string_view kNameField = u"NAME";
        constexpr std::u16string_view kOtherNameField = u"NAME2";
        // The text keys of the scenario goals' messages and of the park inspector's reports start so.
        constexpr std::string_view kGoalKeys = "ObjectiveMessage_";
        constexpr std::string_view kInspectorKeys = "Thought_PI_";
        constexpr std::string_view kDisplayKeys = "FireworkDisplay";
        /// How a firework display keeps the name it has not been given: the default name, then any number.
        constexpr std::u16string_view kOwnName = u"%ME%";
        constexpr std::string_view kDisplayDefaultName = "FireworkDisplayDefaultName";

        // Where things sit, in units from the drawer's left and bottom edges.
        constexpr f32 kLipHeight = 39;
        constexpr f32 kLipSplit = kWidth / 2; ///< Where the lip's left half ends and its right half starts
        constexpr f32 kBackHeight = 36;
        constexpr f32 kBackReach = kOpen - kLipHeight; ///< How far up the back is repeated
        constexpr f32 kFirstRow = 26;                  ///< Bottom of the newest row
        constexpr f32 kStripeLeft = 10;
        constexpr f32 kStripeRight = 470;
        constexpr f32 kIconLeft = 18;
        constexpr f32 kIconSize = 18;
        constexpr f32 kTextLeft = 42;
        constexpr f32 kMoodSize = 12;  ///< A thought's face, at kTextLeft
        constexpr f32 kMoodShift = 16; ///< Of the text, past the face
        constexpr f32 kTextRight = 468;
        constexpr f32 kRowPadding = 4; ///< Added to the text's height; the text starts half of it down
        constexpr f32 kEmptyRow = 18;
        constexpr f32 kArrowSize = 17;
        constexpr f32 kArrowBottom = 5;
        constexpr f32 kNewerLeft = 330;
        constexpr f32 kOlderLeft = 350;
        /// Rows filling more than this from the first skipped one leave room to step to older messages.
        constexpr f32 kPage = 290;
        /// Above the newest rows when sliding down for them, for the part of the drawer behind the bar.
        constexpr f32 kUnderBar = 20;

        /// Removes `field` (with its percent signs) from `text` at `at`, with the space before it, and the
        /// separator before that when the field ended the text.
        void drop_field(std::u16string& text, usize at, usize length)
        {
            text.erase(at, length);
            if (at > 0 && text[at - 1] == u' ')
                text.erase(--at, 1);
            if (at == text.size())
            {
                while (!text.empty() && (text.back() == u':' || text.back() == u' '))
                    text.pop_back();
            }
        }

    } // namespace

    std::u16string message_text(const simulation::park::ParkMessage& message, const StringTable& strings)
    {
        std::u16string text(strings.get(message.text_key));
        // An award's message is named by the award.
        std::u16string name = message.award ? std::u16string(strings.get(simulation::award_name(*message.award))) : strings::to_utf16(message.subject_name);
        if (message.text_key.starts_with(kDisplayKeys) && name.starts_with(kOwnName))
            name.replace(0, kOwnName.size(), strings.get(kDisplayDefaultName));
        if (const auto at = text.find(u"%d"); message.number && at != std::u16string::npos)
            text.replace(at, 2, strings::to_utf16(std::to_string(*message.number)));
        // Fields are words between percent signs. The subject's name fills %NAME%, the field key any other.
        usize from = 0;
        while (from < text.size())
        {
            const auto open = text.find(u'%', from);
            if (open == std::u16string::npos)
                break;
            const auto close = text.find(u'%', open + 1);
            if (close == std::u16string::npos)
                break;
            const std::u16string_view field = std::u16string_view(text).substr(open + 1, close - open - 1);
            if (field.empty() || std::ranges::any_of(field, [](char16_t c) { return c == u' '; }))
            {
                from = open + 1;
                continue;
            }
            if (field == kNameField && !name.empty())
            {
                text.replace(open, close - open + 1, name);
                from = open + name.size();
                continue;
            }
            if (field == kOtherNameField && !message.other_name.empty())
            {
                const std::u16string other = strings::to_utf16(message.other_name);
                text.replace(open, close - open + 1, other);
                from = open + other.size();
                continue;
            }
            if (!message.field_key.empty())
            {
                const std::u16string_view value = strings.get(message.field_key);
                text.replace(open, close - open + 1, value);
                from = open + value.size();
                continue;
            }
            drop_field(text, open, close - open + 1);
            from = std::min(open, text.size());
        }
        return text;
    }

    void name_subjects(simulation::park::ParkMessages& messages, std::span<const save::model::Ride> rides, std::span<const save::model::Stall> stalls, const StringTable& strings)
    {
        for (simulation::park::ParkMessage& m : messages.list)
        {
            if (!m.subject)
                continue;
            const auto ride = std::ranges::find(rides, *m.subject, [](const save::model::Ride& r) { return r.base.id; });
            const auto stall = std::ranges::find(stalls, *m.subject, [](const save::model::Stall& s) { return s.base.id; });
            const save::model::AttractionCommon* base = ride != rides.end() ? &ride->base : stall != stalls.end() ? &stall->base : nullptr;
            if (base != nullptr && (m.subject_name.empty() || m.subject_name == base->name))
                m.subject_name = strings::to_utf8(info::attraction_name(*base, strings));
        }
    }

    std::vector<DrawerMessage> drawer_messages(const simulation::park::ParkMessages& messages, const StringTable& strings, const MessageDrawerStyle& style)
    {
        std::vector<DrawerMessage> out;
        out.reserve(messages.list.size());
        for (const auto& m : messages.list)
        {
            const std::string& icon = m.award ? style.award : m.text_key.starts_with(kGoalKeys) ? style.objective : m.text_key.starts_with(kInspectorKeys) ? style.inspector : style.locate;
            const auto mood = key_mood(m.text_key);
            out.push_back({ .serial = m.serial, .text = message_text(m, strings), .icon = icon, .mood = mood ? mood_icon(*mood) : std::string { }, .clickable = m.award.has_value() });
        }
        return out;
    }

    MessageDrawer::MessageDrawer(std::string name, MessageDrawerStyle style, const StringTable& strings)
        : Widget(std::move(name))
        , style_(std::move(style))
    {
        tooltip = strings.get(kDragHelp);
        newer_ = &add<widgets::Button>("newer", style_.newer);
        newer_->placement = bottom_left(kNewerLeft, kArrowBottom, kArrowSize, kArrowSize);
        newer_->tooltip = strings.get(kNewerHelp);
        older_ = &add<widgets::Button>("older", style_.older);
        older_->placement = bottom_left(kOlderLeft, kArrowBottom, kArrowSize, kArrowSize);
        older_->tooltip = strings.get(kOlderHelp);
        set_height(kClosed);
    }

    void MessageDrawer::show(std::span<const DrawerMessage> messages)
    {
        messages_.assign(messages.rbegin(), messages.rend());
        u64 newest = 0;
        usize fresh = 0;
        for (const auto& m : messages_)
        {
            newest = std::max(newest, m.serial);
            if (shown_ && m.serial > seen_)
                ++fresh;
        }
        shown_ = true;
        seen_ = std::max(seen_, newest);
        skipped_ = std::min(skipped_, messages_.empty() ? 0 : messages_.size() - 1);
        if (fresh == 0)
            return;
        alert_ = true;
        unseen_ = std::min(unseen_ + fresh, messages_.size());
        if (!dragging_)
            slide_ = Slide::Opening;
    }

    void MessageDrawer::set_height(f32 height)
    {
        height_ = std::clamp(height, kClosed, kOpen);
        placement = { { 0.5f, 0, 0.5f, 0 }, { kLeft, kTop, kLeft + kWidth, kTop + height_ } };
    }

    f32 MessageDrawer::rows_top(usize count) const
    {
        f32 top = kFirstRow;
        for (usize i = skipped_; i < std::min(skipped_ + count, row_heights_.size()); ++i)
            top += row_heights_[i];
        return top;
    }

    void MessageDrawer::step(std::ptrdiff_t older)
    {
        const auto last = static_cast<std::ptrdiff_t>(messages_.empty() ? 0 : messages_.size() - 1);
        skipped_ = static_cast<usize>(std::clamp(static_cast<std::ptrdiff_t>(skipped_) + older, std::ptrdiff_t { 0 }, last));
    }

    std::optional<u64> MessageDrawer::icon_at(Point p) const
    {
        const Rect r = rect();
        if (p.x < r.x0 + kIconLeft || p.x >= r.x0 + kIconLeft + kIconSize)
            return std::nullopt;
        f32 bottom = r.y1 - kFirstRow;
        for (usize i = skipped_; i < messages_.size() && i < row_heights_.size() && bottom > r.y0; ++i)
        {
            const f32 top = bottom - row_heights_[i];
            if (messages_[i].clickable && p.y >= std::max(top, r.y0) && p.y < top + kIconSize)
                return messages_[i].serial;
            bottom = top;
        }
        return std::nullopt;
    }

    void MessageDrawer::on_press(Point p)
    {
        if (const auto serial = icon_at(p))
        {
            clicked_ = serial;
            return;
        }
        const bool twice = since_press_ && *since_press_ <= style_.double_press;
        since_press_ = twice ? std::nullopt : std::optional { units::Seconds { } };
        slide_ = Slide::Resting;
        unseen_ = 0;
        if (twice)
        {
            // Opens fully from closed, and closes from anywhere else, back to the newest messages.
            dragging_ = false;
            const bool open = height_ <= kClosed;
            set_height(open ? kOpen : kClosed);
            rest_ = height_;
            if (!open)
                skipped_ = 0;
            return;
        }
        dragging_ = true;
        drag_from_ = p.y - height_;
    }

    void MessageDrawer::on_drag(Point p)
    {
        if (dragging_)
            set_height(p.y - drag_from_);
    }

    void MessageDrawer::on_release(Point, bool)
    {
        if (dragging_)
            rest_ = height_;
        dragging_ = false;
    }

    bool MessageDrawer::on_wheel(Point notches)
    {
        if (notches.y == 0)
            return false;
        step(notches.y > 0 ? 1 : -1);
        return true;
    }

    void MessageDrawer::update(const UpdateContext& context)
    {
        if (since_press_)
            *since_press_ += context.elapsed;

        // Each message's row is as tall as its text wrapped to the row's width.
        const auto font = context.fonts ? context.fonts->get(font::FontRole::Small) : std::nullopt;
        row_heights_.clear();
        for (const auto& m : messages_)
        {
            f32 height = kEmptyRow;
            if (font)
            {
                if (const auto lines = break_lines(*font, m.text, kTextRight - kTextLeft - (m.mood.empty() ? 0 : kMoodShift)).size(); lines > 0)
                    height = static_cast<f32>(lines) * font->line_height() + kRowPadding;
            }
            row_heights_.push_back(height);
        }

        // The arrows step one message at a time. Stepping newer never leaves the rows short of a page when
        // there are older ones to fill it.
        if (older_->take_click())
            step(1);
        if (newer_->take_click())
            step(-1);
        while (skipped_ > 0 && rows_top(messages_.size()) - kFirstRow <= kPage)
            --skipped_;
        const f32 after_first = rows_top(messages_.size()) - rows_top(1);
        older_->enabled = after_first >= kPage && skipped_ + 1 < messages_.size();
        newer_->enabled = skipped_ > 0 && !messages_.empty();

        const f32 speed = kSlideSpeed.value * context.elapsed.value;
        switch (slide_)
        {
            case Slide::Resting:
                break;
            case Slide::Opening:
            {
                const f32 want = std::min(rows_top(unseen_) + kUnderBar, kOpen);
                set_height(std::max(std::min(height_ + speed, want), height_));
                if (height_ >= want)
                {
                    slide_ = Slide::Holding;
                    held_ = { };
                }
                break;
            }
            case Slide::Holding:
                held_ += context.elapsed;
                if (held_ >= kHold)
                {
                    slide_ = Slide::Closing;
                    unseen_ = 0;
                }
                break;
            case Slide::Closing:
                set_height(std::max(height_ - speed, rest_));
                if (height_ <= rest_)
                    slide_ = Slide::Resting;
                break;
        }
    }

    void draw_drawer_frame(DrawContext& context, const MessageDrawerStyle& style, const Rect& r)
    {
        // The lip along the bottom in two halves, the back repeated above it.
        const f32 lip = r.y1 - kLipHeight;
        skin::draw_skin(context.list, context.skins.find(style.lip_left), { r.x0, lip, r.x0 + kLipSplit, r.y1 });
        skin::draw_skin(context.list, context.skins.find(style.lip_right), { r.x0 + kLipSplit, lip, r.x0 + kWidth, r.y1 });
        const auto back = context.skins.find(style.back);
        for (f32 y = lip; y > lip - kBackReach && y > r.y0; y -= kBackHeight)
            skin::draw_skin(context.list, back, { r.x0, y - kBackHeight, r.x0 + kWidth, y });
    }

    void MessageDrawer::draw(DrawContext& context) const
    {
        const Rect r = rect();
        if (context.list.push_clip(r))
            draw_inside(context, r);
        context.list.pop_clip();
    }

    void MessageDrawer::draw_inside(DrawContext& context, const Rect& r) const
    {
        draw_drawer_frame(context, style_, r);

        // The newest message at the bottom, older ones above it.
        const auto font = context.fonts.get(font::FontRole::Small);
        f32 bottom = r.y1 - kFirstRow;
        for (usize i = skipped_; i < messages_.size() && i < row_heights_.size() && bottom > r.y0; ++i)
        {
            const f32 top = bottom - row_heights_[i];
            const bool even = (i - skipped_) % 2 == 0;
            context.list.fill({ r.x0 + kStripeLeft, top, r.x0 + kStripeRight, bottom }, even ? style_.stripe : style_.stripe_alt);
            if (!messages_[i].icon.empty())
                skin::draw_skin(context.list, context.skins.find(messages_[i].icon), { r.x0 + kIconLeft, top, r.x0 + kIconLeft + kIconSize, top + kIconSize });
            f32 text_left = kTextLeft;
            if (!messages_[i].mood.empty())
            {
                const f32 y = top + kRowPadding / 2 + 1;
                skin::draw_skin(context.list, context.skins.find(messages_[i].mood), { r.x0 + kTextLeft, y, r.x0 + kTextLeft + kMoodSize, y + kMoodSize });
                text_left += kMoodShift;
            }
            if (font)
                draw_text(context.list, *font, messages_[i].text, { r.x0 + text_left, top + kRowPadding / 2, r.x0 + kTextRight, bottom }, { style_.text_colour, Align::Start, Align::Start, true });
            bottom = top;
        }
    }

} // namespace rct3::ui::screens::park_view
