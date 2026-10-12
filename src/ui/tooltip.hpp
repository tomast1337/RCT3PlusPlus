// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tooltips: a short help text that appears beside the pointer after it rests on a widget for a moment. The
// text is the widget's own tooltip, or the nearest ancestor's. The tooltip stays while the pointer is over
// widgets with the same text, and goes when the text changes, a button is pressed or the pointer leaves.
//
// Tooltip is plain state: the desktop feeds it what is under the pointer each update and draws it last, so
// it covers every window and never takes the pointer itself.
#pragma once

#include "ui/widget.hpp"

#include <string>
#include <string_view>

namespace rct3::ui
{

    struct TooltipStyle
    {
        std::string skin; ///< Stretched behind the text
        FontRole font = FontRole::Small;
        Color colour { 0, 0, 0, 255 };
        float delay = 0.5f;          ///< Seconds the pointer must rest before the tooltip shows
        float still_distance = 0.1f; ///< Movement up to this many units still counts as resting
        Rect padding { 4, 4, 4, 4 }; ///< Between the box edges and the text
        float max_width = 240;       ///< Text wider than this wraps
        Point offset { 0, 20 };      ///< From the pointer to the box's top-left corner
    };

    /// The tooltip text for the pointer resting on `w`: its own, or its nearest ancestor's.
    [[nodiscard]] std::u16string_view tooltip_text(const Widget* w);

    class Tooltip
    {
    public:
        /// `under` is the widget under the pointer (null when none), `pressed` whether the button is down,
        /// `elapsed` the seconds since the last update.
        void update(const Widget* under, Point pointer, bool pressed, float elapsed, const TooltipStyle& style);

        [[nodiscard]] bool shown() const
        {
            return shown_;
        }
        [[nodiscard]] std::u16string_view text() const
        {
            return text_;
        }
        /// Where the pointer rested when the tooltip appeared.
        [[nodiscard]] Point anchor() const
        {
            return anchor_;
        }

        /// The box the tooltip fills at `anchor`, kept inside `view`. Empty when the style's font is missing.
        [[nodiscard]] Rect box(const TooltipStyle& style, const FontSet& fonts, const Rect& view) const;

        /// Nothing when not shown.
        void draw(DrawContext& context, const TooltipStyle& style, const Rect& view) const;

    private:
        std::u16string text_; // Copied: the widget that owned it may close while the tooltip is up
        Point last_;
        Point anchor_;
        float rested_ = 0;
        bool shown_ = false;
        bool blocked_ = false; // A press hid it; stays hidden until the text under the pointer changes
    };

} // namespace rct3::ui
