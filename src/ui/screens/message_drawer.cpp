// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/message_drawer.hpp"

#include "simulation/awards.hpp"
#include "strings/unicode.hpp"
#include "ui/text_layout.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::ui
{

    namespace
    {

        using namespace message_drawer;

        constexpr std::string_view kDragHelp = "MessageConsoleClickDrag";
        constexpr std::string_view kOlderHelp = "TT_GUI2_MessageConsolePrevoiusMessages"; // Misspelt in the game
        constexpr std::string_view kNewerHelp = "TT_GUI2_MessageConsoleNextMessages";
        constexpr std::u16string_view kNameField = u"NAME";

        // Where things sit, in units from the drawer's left and bottom edges.
        constexpr float kLipHeight = 39;
        constexpr float kLipSplit = kWidth / 2; ///< Where the lip's left half ends and its right half starts
        constexpr float kBackHeight = 36;
        constexpr float kBackReach = kOpen - kLipHeight; ///< How far up the back is repeated
        constexpr float kFirstRow = 26;                  ///< Bottom of the newest row
        constexpr float kStripeLeft = 10;
        constexpr float kStripeRight = 470;
        constexpr float kCameraLeft = 18;
        constexpr float kCameraSize = 18;
        constexpr float kTextLeft = 42;
        constexpr float kTextRight = 468;
        constexpr float kRowPadding = 4; ///< Added to the text's height; the text starts half of it down
        constexpr float kEmptyRow = 18;
        constexpr float kArrowSize = 17;
        constexpr float kArrowBottom = 5;
        constexpr float kNewerLeft = 330;
        constexpr float kOlderLeft = 350;
        /// Rows filling more than this from the first skipped one leave room to step to older messages.
        constexpr float kPage = 290;
        /// Above the newest rows when sliding down for them, for the part of the drawer behind the bar.
        constexpr float kUnderBar = 20;

        /// Removes `field` (with its percent signs) from `text` at `at`, with the space before it, and the
        /// separator before that when the field ended the text.
        void drop_field(std::u16string& text, std::size_t at, std::size_t length)
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

    std::u16string message_text(const simulation::ParkMessage& message, const StringTable& strings)
    {
        std::u16string text(strings.get(message.text_key));
        // An award's message is named by the award.
        const std::u16string name = message.award ? std::u16string(strings.get(simulation::award_name(*message.award))) : strings::to_utf16(message.subject_name);
        // Fields are words between percent signs. The subject's name fills %NAME%; the rest are not known yet.
        std::size_t from = 0;
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
            drop_field(text, open, close - open + 1);
            from = std::min(open, text.size());
        }
        return text;
    }

    std::vector<DrawerMessage> drawer_messages(const simulation::ParkMessages& messages, const StringTable& strings)
    {
        std::vector<DrawerMessage> out;
        out.reserve(messages.list.size());
        for (const auto& m : messages.list)
            out.push_back({ .serial = m.serial, .text = message_text(m, strings) });
        return out;
    }

    MessageDrawer::MessageDrawer(std::string name, MessageDrawerStyle style, const StringTable& strings)
        : Widget(std::move(name))
        , style_(std::move(style))
    {
        tooltip = strings.get(kDragHelp);
        newer_ = &add<Button>("newer", style_.newer);
        newer_->placement = bottom_left(kNewerLeft, kArrowBottom, kArrowSize, kArrowSize);
        newer_->tooltip = strings.get(kNewerHelp);
        older_ = &add<Button>("older", style_.older);
        older_->placement = bottom_left(kOlderLeft, kArrowBottom, kArrowSize, kArrowSize);
        older_->tooltip = strings.get(kOlderHelp);
        set_height(kClosed);
    }

    void MessageDrawer::show(std::span<const DrawerMessage> messages)
    {
        messages_.assign(messages.rbegin(), messages.rend());
        std::uint64_t newest = 0;
        std::size_t fresh = 0;
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

    void MessageDrawer::set_height(float height)
    {
        height_ = std::clamp(height, kClosed, kOpen);
        placement = { { 0.5f, 0, 0.5f, 0 }, { kLeft, kTop, kLeft + kWidth, kTop + height_ } };
    }

    float MessageDrawer::rows_top(std::size_t count) const
    {
        float top = kFirstRow;
        for (std::size_t i = skipped_; i < std::min(skipped_ + count, row_heights_.size()); ++i)
            top += row_heights_[i];
        return top;
    }

    void MessageDrawer::step(std::ptrdiff_t older)
    {
        const auto last = static_cast<std::ptrdiff_t>(messages_.empty() ? 0 : messages_.size() - 1);
        skipped_ = static_cast<std::size_t>(std::clamp(static_cast<std::ptrdiff_t>(skipped_) + older, std::ptrdiff_t { 0 }, last));
    }

    std::optional<std::uint64_t> MessageDrawer::camera_at(Point p) const
    {
        const Rect r = rect();
        if (p.x < r.x0 + kCameraLeft || p.x >= r.x0 + kCameraLeft + kCameraSize)
            return std::nullopt;
        float bottom = r.y1 - kFirstRow;
        for (std::size_t i = skipped_; i < messages_.size() && i < row_heights_.size() && bottom > r.y0; ++i)
        {
            const float top = bottom - row_heights_[i];
            if (messages_[i].located && p.y >= std::max(top, r.y0) && p.y < top + kCameraSize)
                return messages_[i].serial;
            bottom = top;
        }
        return std::nullopt;
    }

    void MessageDrawer::on_press(Point p)
    {
        if (const auto serial = camera_at(p))
        {
            located_ = serial;
            return;
        }
        const bool twice = since_press_ <= style_.double_press;
        since_press_ = twice ? 1e9f : 0;
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
        since_press_ += context.elapsed;

        // Each message's row is as tall as its text wrapped to the row's width.
        const auto font = context.fonts ? context.fonts->get(FontRole::Small) : std::nullopt;
        row_heights_.clear();
        for (const auto& m : messages_)
        {
            float height = kEmptyRow;
            if (font)
            {
                if (const auto lines = break_lines(*font, m.text, kTextRight - kTextLeft).size(); lines > 0)
                    height = static_cast<float>(lines) * font->line_height() + kRowPadding;
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
        const float after_first = rows_top(messages_.size()) - rows_top(1);
        older_->enabled = after_first >= kPage && skipped_ + 1 < messages_.size();
        newer_->enabled = skipped_ > 0 && !messages_.empty();

        const float speed = kSlideSpeed * context.elapsed;
        switch (slide_)
        {
            case Slide::Resting:
                break;
            case Slide::Opening:
            {
                const float want = std::min(rows_top(unseen_) + kUnderBar, kOpen);
                set_height(std::max(std::min(height_ + speed, want), height_));
                if (height_ >= want)
                {
                    slide_ = Slide::Holding;
                    held_ = 0;
                }
                break;
            }
            case Slide::Holding:
                held_ += context.elapsed;
                if (held_ >= kHoldSeconds)
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
        const float lip = r.y1 - kLipHeight;
        draw_skin(context.list, context.skins.find(style.lip_left), { r.x0, lip, r.x0 + kLipSplit, r.y1 });
        draw_skin(context.list, context.skins.find(style.lip_right), { r.x0 + kLipSplit, lip, r.x0 + kWidth, r.y1 });
        const auto back = context.skins.find(style.back);
        for (float y = lip; y > lip - kBackReach && y > r.y0; y -= kBackHeight)
            draw_skin(context.list, back, { r.x0, y - kBackHeight, r.x0 + kWidth, y });
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
        const auto font = context.fonts.get(FontRole::Small);
        float bottom = r.y1 - kFirstRow;
        for (std::size_t i = skipped_; i < messages_.size() && i < row_heights_.size() && bottom > r.y0; ++i)
        {
            const float top = bottom - row_heights_[i];
            const bool even = (i - skipped_) % 2 == 0;
            context.list.fill({ r.x0 + kStripeLeft, top, r.x0 + kStripeRight, bottom }, even ? style_.stripe : style_.stripe_alt);
            if (messages_[i].located)
                draw_skin(context.list, context.skins.find(style_.locate), { r.x0 + kCameraLeft, top, r.x0 + kCameraLeft + kCameraSize, top + kCameraSize });
            if (font)
                draw_text(context.list, *font, messages_[i].text, { r.x0 + kTextLeft, top + kRowPadding / 2, r.x0 + kTextRight, bottom }, { style_.text_colour, Align::Start, Align::Start, true });
            bottom = top;
        }
    }

} // namespace rct3::ui
