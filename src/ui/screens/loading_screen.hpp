// SPDX-License-Identifier: GPL-3.0-or-later
//
// The screen shown while the game starts and while a park loads. Drawn in code, flat as the game's own: two
// plain blues, dark above the middle of the game's logo and light below, a white band across the screen behind
// the logo (its picture is on white) with a thin pale line just above it; the logo (the one picture taken from the game), and under it a heading with the share
// done, a bar with round ends, the stages of the load each marked done, under way or waiting, and a line of
// detail: the last thing loading reported, and how many warnings it gave, which the log holds in full.
//
// Everything stands in one column in the middle of the screen, sized from the screen so it reads the same on
// every window; text keeps its font's size, as the rest of the interface does. What has no content (no logo,
// no fonts, no stages, no progress) is left out and the rest closes up. Nothing moves; the owner draws the
// screen again whenever something changes.
#pragma once

#include "ui/draw_list.hpp"
#include "ui/font.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rct3::ui::loading_screen
{

    /// The game's logo, in the startup logos' overlay.
    inline constexpr std::string_view kLogoFile = "gui/Banner/Banner.common.ovl";
    inline constexpr std::string_view kLogo = "RCT3Logo";
    /// The heading's text.
    inline constexpr std::string_view kHeadingText = "LoadingScreen_NowLoading";

    // Flat colours, as the game's screen has.
    inline constexpr Color kDark = hex("#2979ad");  ///< Above the split
    inline constexpr Color kLight = hex("#52aae7"); ///< Below it
    inline constexpr Color kBand = hex("#ffffff");  ///< Behind the logo
    inline constexpr Color kLine = kLight;          ///< The thin line above the band
    inline constexpr Color kText = hex("#000000");
    inline constexpr Color kFaint = hex("#00000080"); ///< Stages waiting, and the detail
    inline constexpr Color kWarning = hex("#7a1c00");
    inline constexpr Color kTrack = hex("#a5d0f2");
    inline constexpr Color kFill = hex("#70abf1");
    inline constexpr Color kEdge = hex("#3578ac"); ///< Round the bar, and the stages' markers

    // Our own measures, in screen units at a scale of 1 (a 1280 by 720 screen).
    inline constexpr float kColumn = 560;     ///< The column's width, at most
    inline constexpr float kMargin = 24;      ///< Kept clear round the column
    inline constexpr float kLogoHeight = 220; ///< The logo's height, at most
    inline constexpr float kHeadingHeight = 34;
    inline constexpr float kBarHeight = 16;
    inline constexpr float kRowHeight = 26; ///< A stage's row
    inline constexpr float kMarker = 10;    ///< A stage's disc
    inline constexpr float kDetailHeight = 22;
    inline constexpr float kGap = 14; ///< Between the parts
    inline constexpr float kLineHeight = 2;
    inline constexpr float kLineAbove = 5; ///< From the line's bottom to the band

    enum class StageState : std::uint8_t
    {
        Waiting,
        Current,
        Done,
    };

    struct Stage
    {
        std::u16string_view name;
        StageState state = StageState::Waiting;
    };

    struct Picture
    {
        TextureId texture = 0;
        float width = 0;
        float height = 0;
    };

    struct Look
    {
        std::optional<Picture> logo;
        const Font* heading_font = nullptr; ///< Without a font, its text is left out
        const Font* stage_font = nullptr;
        const Font* detail_font = nullptr;
    };

    struct Content
    {
        std::u16string_view heading;
        std::span<const Stage> stages;
        std::u16string_view detail;
        std::u16string_view warning;   ///< Shown under the detail, in its own colour
        std::optional<float> progress; ///< 0..1; without it there is no bar and no share
    };

    /// Where everything goes on one screen, in the screen's units. Parts that are left out are empty.
    struct Layout
    {
        float scale = 1; ///< Of the measures above
        Rect dark;       ///< The backdrop above the split
        Rect light;      ///< And below
        Rect band;       ///< Across the screen behind the logo; empty without one
        Rect line;
        Rect logo;
        Rect heading;
        Rect bar;
        Rect stages; ///< All the rows
        float row = 0;
        Rect detail; ///< The detail and the warning, one over the other
    };

    /// The layout for `screen` and what is shown: a logo `logo_width` by `logo_height` (0 for none), whether
    /// there is text, a bar, how many stages and detail lines.
    struct Parts
    {
        float logo_width = 0;
        float logo_height = 0;
        bool heading = false;
        bool bar = false;
        std::size_t stages = 0;
        std::size_t detail_lines = 0;
    };
    [[nodiscard]] Layout layout_for(const Rect& screen, const Parts& parts);

    /// The bar's filled part for `progress`: from its left end, never narrower than it is tall, so both ends
    /// stay round; empty at 0.
    [[nodiscard]] Rect bar_fill(const Rect& bar, float progress);

    /// How far a load made of `total` stages of about equal size has got once `done` of them are done.
    [[nodiscard]] float stage_progress(std::size_t done, std::size_t total);

    /// The progress in whole percent, as in "25%".
    [[nodiscard]] std::u16string percent_text(float progress);

    /// Draws the whole screen over `screen`, in the list's units.
    void draw(DrawList& list, const Rect& screen, const Look& look, const Content& content);

} // namespace rct3::ui::loading_screen
