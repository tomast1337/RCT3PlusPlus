// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ride/styles.hpp"

#include "strings/ascii.hpp"

#include <algorithm>

namespace rct3::park::ride
{

    namespace
    {
        /// Overlays are kept with either kind of slash.
        bool same_overlay(std::string_view a, std::string_view b)
        {
            return std::ranges::equal(a, b, [](char x, char y) { return strings::ascii_lower(x == '\\' ? '/' : x) == strings::ascii_lower(y == '\\' ? '/' : y); });
        }

        /// Adds the style to `styles` once; a theme's own item names it over one that only borrows it.
        void add(std::vector<ExtraStyle>& styles, ExtraStyle style, bool own)
        {
            const auto same = std::ranges::find_if(styles, [&](const ExtraStyle& s) { return strings::equals_ignoring_case(s.symbol, style.symbol) && same_overlay(s.overlay, style.overlay); });
            if (same == styles.end())
                styles.push_back(std::move(style));
            else if (own && !style.title.empty())
                same->title = std::move(style.title);
        }
    } // namespace

    RideStyles ride_styles(std::span<const BuildItem> items)
    {
        RideStyles styles;
        for (const BuildItem& item : items)
        {
            if (item.kind != BuildKind::FlatRide && item.kind != BuildKind::TrackedRide)
                continue;
            if (!item.fence.empty() && !item.fence_overlay.empty())
                add(styles.fences, { .title = item.style_title, .symbol = item.fence, .overlay = item.fence_overlay }, same_overlay(item.overlay, item.fence_overlay));
            if (!item.entrance.empty() && !item.exit.empty())
                add(styles.doors, { .title = item.style_title, .symbol = item.entrance, .exit_symbol = item.exit, .overlay = item.door_overlay }, same_overlay(item.overlay, item.door_overlay));
        }
        return styles;
    }

    std::optional<usize> style_of(std::span<const ExtraStyle> styles, std::string_view symbol, std::string_view overlay, DoorEnd end)
    {
        const auto found = std::ranges::find_if(styles, [&](const ExtraStyle& s) { return strings::equals_ignoring_case(end == DoorEnd::Exit ? s.exit_symbol : s.symbol, symbol) && same_overlay(s.overlay, overlay); });
        return found == styles.end() ? std::nullopt : std::optional { static_cast<usize>(found - styles.begin()) };
    }

} // namespace rct3::park::ride
