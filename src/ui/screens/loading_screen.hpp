// SPDX-License-Identifier: GPL-3.0-or-later
//
// The screen shown while the game starts and while a park loads: the game's band picture (its logo in a
// disc at the left, a rule and the game's name on the right) in the middle of a backdrop of two blues, with
// a heading, a line naming the stage under way and a bar of how far loading has got.
//
// The band keeps its shape on every screen: it is drawn at one scale both ways, as wide as a share of the
// screen's width but never taller than a share of its height, and centred. The backdrop carries the band's
// two blues out to the edges of the screen, dark above the middle of the band and light below, with a thin
// pale line just above the split, so the band's rounded ends sit on the colours they were painted against.
//
// Text and the bar go in the band's empty right half, below its rule: their places are given in the band
// picture's own pixels, y down, and scale with it. Text keeps the size of its font, as the rest of the
// interface does. Nothing here moves or fades; the owner draws the screen whenever the stage or the progress
// changes.
#pragma once

#include "ui/draw_list.hpp"
#include "ui/font.hpp"
#include "ui/skin_library.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace rct3::ui::loading_screen
{

    /// The file with the band, in the install.
    inline constexpr std::string_view kSkinFile = "gui/LoadingScreen/LoadingScreen.common.ovl";
    /// The band's names: one for most languages, and two with the game's name in Chinese.
    inline constexpr std::string_view kBand = "LoadingScreen";
    inline constexpr std::string_view kBandTraditional = "LoadingScreen_Chinese";
    inline constexpr std::string_view kBandSimplified = "LoadingScreen_SChinese";
    /// The ends of the bar; the part between them is drawn in plain colours.
    inline constexpr std::string_view kBarLeft = "Blender_Bar_Left";
    inline constexpr std::string_view kBarRight = "Blender_Bar_Right";

    /// The heading's text.
    inline constexpr std::string_view kHeadingText = "LoadingScreen_NowLoading";

    /// How much of the screen the band may take: this share of its width, unless that would make it taller
    /// than the other share of its height.
    inline constexpr float kWidthShare = 0.45f;
    inline constexpr float kHeightShare = 0.25f;

    inline constexpr Color kDark = hex("#2979ad");  ///< Above the band's middle, picked from its ends
    inline constexpr Color kLight = hex("#52aae7"); ///< Below it
    inline constexpr Color kLine = kLight;          ///< The thin line above the split
    inline constexpr Color kBarBack = hex("#a5d0f2");
    inline constexpr Color kBarFill = hex("#70abf1");
    inline constexpr Color kBarEdge = hex("#3578ac");
    inline constexpr Color kText = hex("#000000");

    // In the band picture's pixels, from its top-left corner.
    inline constexpr float kLineTop = 91; ///< The thin line, as tall as two pixels of the band
    inline constexpr float kLineBottom = 93;
    inline constexpr float kSplit = 98;                  ///< Where the light blue begins
    inline constexpr Rect kHeading { 185, 62, 568, 92 }; ///< Above the rule, from where the band's title starts
    inline constexpr Rect kStep { 189, 122, 568, 146 };  ///< Below the band's title
    inline constexpr Rect kBar { 183, 150, 483, 172 };   ///< Below that; its height is the ends' when they load

    /// Where everything goes on one screen, in the screen's units.
    struct Layout
    {
        float scale = 1; ///< Screen units per band pixel
        Rect band;
        Rect dark;  ///< The backdrop above the split
        Rect light; ///< And below
        Rect line;
        Rect heading;
        Rect step;
        Rect bar;

        /// A rectangle given in band pixels, on the screen.
        [[nodiscard]] Rect on_band(const Rect& r) const
        {
            return { band.x0 + r.x0 * scale, band.y0 + r.y0 * scale, band.x0 + r.x1 * scale, band.y0 + r.y1 * scale };
        }
    };

    /// The layout for `screen`, with a band picture `band_width` by `band_height` pixels. A band without a
    /// size is taken as having none: everything collapses to the screen's centre, and only the backdrop shows.
    [[nodiscard]] Layout layout_for(const Rect& screen, float band_width, float band_height);

    /// The band for a language, by the name the game gives the language.
    [[nodiscard]] std::string_view band_for(std::string_view language);

    /// How far a load made of `total` stages of about equal size has got once `done` of them are done.
    [[nodiscard]] float stage_progress(std::size_t done, std::size_t total);

    /// A stage's name followed by its progress in whole percent, as in "Terrain (25%)".
    [[nodiscard]] std::u16string step_line(std::u16string_view stage, float progress);

    struct Look
    {
        const SkinRef* band = nullptr; ///< Without it only the backdrop shows
        const SkinRef* bar_left = nullptr;
        const SkinRef* bar_right = nullptr;
        const Font* heading_font = nullptr; ///< Without a font, its text is left out
        const Font* step_font = nullptr;
    };

    struct Content
    {
        std::u16string_view heading;
        std::u16string_view step;
        std::optional<float> progress; ///< 0..1; without it there is no bar
    };

    /// Draws the whole screen over `screen`, in the list's units.
    void draw(DrawList& list, const Rect& screen, const Look& look, const Content& content);

} // namespace rct3::ui::loading_screen
