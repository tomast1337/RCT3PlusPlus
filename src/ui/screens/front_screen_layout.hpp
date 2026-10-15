// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the parts of the front screen go. The screen is designed for 1024 by 768 units. With the game's
// pictures the design is stretched to the real size: positions and the sizes of pictures scale with width
// and height separately, so the artwork always covers the screen, while the round buttons scale by the
// smaller of the two so they stay round. With the backdrop drawn in code the design scales by that one
// smaller scale both ways, from the bottom-left corner, so its circles stay round; the shapes then reach
// on to the screen's other edges themselves. Every measurement below is in that 1024 by 768 frame, y down.
//
// The artwork: a blue band down the left, a white arc curving from the top edge round to the bottom right,
// the game's logo in the top-left corner, two children below the menu (main menu only) and a band along
// the bottom with two lines of help. The top right is left open; the park behind shows through.
//
// The menu buttons sit on a wide circle whose centre is right of the middle of the screen, so its rim
// curves down the left edge; each button's label sits in a box to its right. Back and Quit sit on a smaller
// circle near the bottom of the arc.
#pragma once

#include "ui/draw_list.hpp"
#include "ui/widget.hpp"

#include <array>
#include <cstddef>

namespace rct3::ui::front_screen
{

    inline constexpr float kDesignWidth = 1024;
    inline constexpr float kDesignHeight = 768;

    /// How the design frame maps onto a real screen.
    struct Frame
    {
        Rect screen;
        Point origin;    ///< Where the design's top-left corner lands
        float sx = 1;    ///< Screen units per design unit across
        float sy = 1;    ///< And down
        float round = 1; ///< For things that must stay round: the smaller of the two

        /// A design point on the screen.
        [[nodiscard]] Point at(Point design) const
        {
            return { origin.x + design.x * sx, origin.y + design.y * sy };
        }
        /// The screen's edges in design units: on a frame that keeps one scale, they can lie past the design.
        [[nodiscard]] Rect design_screen() const
        {
            return { (screen.x0 - origin.x) / sx, (screen.y0 - origin.y) / sy, (screen.x1 - origin.x) / sx, (screen.y1 - origin.y) / sy };
        }
        /// Something that runs along the bottom strip: its left edge kept from the screen's left as designed
        /// and its right edge from the screen's right, so it spans the screen on any frame.
        [[nodiscard]] Rect strip(const Rect& design) const
        {
            const Point a = at({ design.x0, design.y0 });
            const Point b = at({ design.x1, design.y1 });
            return { a.x, a.y, screen.x1 - (kDesignWidth - design.x1) * sx, b.y };
        }
        /// A design rectangle on the screen, stretched both ways.
        [[nodiscard]] Rect stretched(const Rect& design) const
        {
            const Point a = at({ design.x0, design.y0 });
            const Point b = at({ design.x1, design.y1 });
            return { a.x, a.y, b.x, b.y };
        }
    };

    /// The design on `screen`: stretched to it, or `uniform`ly at the smaller scale from its bottom-left corner.
    [[nodiscard]] Frame frame_for(const Rect& screen, bool uniform = false);

    // The artwork, as design rectangles. Pictures not given a size here are drawn at their own size in
    // design units from the top-left corner given.
    inline constexpr Rect kBandTop { 0, 0, 512, 512 };         ///< The left band above the bottom strip
    inline constexpr Rect kBandBottom { 0, 512, 512, 768 };    ///< The left part of the bottom strip
    inline constexpr Rect kStripRight { 512, 513, 1024, 768 }; ///< The rest of the bottom strip
    inline constexpr Point kLogo { 0, 18 };
    inline constexpr Rect kChildren { 0, 310, 384, 694 };
    /// The arc, in five pieces from the top edge down and round to the right edge.
    inline constexpr std::array<Point, 5> kArcPieces = { { { 295, 0 }, { 327, 413 }, { 385, 510 }, { 430, 564 }, { 609, 546 } } };
    /// Their sizes, as the pictures are drawn.
    inline constexpr std::array<Point, 5> kArcPieceSizes = { { { 97, 413 }, { 96, 99 }, { 88, 56 }, { 181, 100 }, { 415, 138 } } };
    inline constexpr Rect kHelpTitle { 256, 712, 768, 736 }; ///< The larger line of help, on the bottom strip
    inline constexpr Rect kHelpText { 256, 740, 768, 756 };  ///< The smaller line under it

    /// A point on a circle round the buttons' centre: `angle` in radians, anticlockwise from the right.
    inline constexpr Point kRingCentre { 734, 241 };
    [[nodiscard]] Point on_ring(float angle, float radius);

    /// The circle the menu buttons sit on, the first at the top-left and the next ones further down.
    inline constexpr float kMenuRadius = 650;
    inline constexpr float kMenuFirstAngle = 3.06f;
    inline constexpr float kMenuAngleStep = 0.165f;
    inline constexpr float kMenuButtonSize = 83;
    inline constexpr std::size_t kMaxMenuEntries = 5;
    [[nodiscard]] float menu_angle(std::size_t index);

    /// How much a round button grows under the pointer, times its size, and how fast, in sizes a second.
    inline constexpr float kSwellFactor = 1.46f;
    inline constexpr float kSwellRate = 5;

    /// Back and Quit, side by side on a smaller circle.
    inline constexpr float kSmallRadius = 520;
    inline constexpr std::array<float, 2> kSmallAngles = { 4.02f, 4.1f };
    inline constexpr float kSmallButtonSize = 32;

    /// A square button of `size` design units centred on a design point.
    [[nodiscard]] Rect button_rect(const Frame& frame, Point centre, float size);

    /// The box beside menu button `index` where its label shows, and its parts. The box starts a little up
    /// and left of the button's centre, so the round patch in it sits behind the button. The patch and the
    /// box's height scale by the round scale; the band's far end and the text's stretch across.
    inline constexpr Point kLabelBoxOffset { -47.7f, -52.3f };
    inline constexpr Point kLabelBoxSize { 400, 100 };
    inline constexpr Rect kLabelPatch { 0, 3, 97, 100 }; ///< In the box: a white ring round the button
    inline constexpr Rect kLabelBand { 46, 3, 268, 97 }; ///< A light band from the ring on to the right
    inline constexpr Rect kLabelText { 105, 3, 240, 97 };
    /// The band's pointed end: the lower ones reach further, `kLabelEndStep` times the index squared.
    inline constexpr float kLabelEndStep = 10;
    struct LabelBox
    {
        Rect patch;
        Rect band;
        Rect end;
        Rect text;
    };
    [[nodiscard]] LabelBox label_box(const Frame& frame, std::size_t index);

    /// The badges of installed expansion packs, in slots: their lower-left corners on a circle below and
    /// right of the buttons' centre, each slot turned a little further clockwise than the last. The corner
    /// moves with the screen, snapped to whole units, but the badge keeps its size; the last pack
    /// installed takes the first slot.
    inline constexpr float kBadgeSize = 100;
    inline constexpr float kBadgeRadius = 475;
    inline constexpr Point kBadgeFirstDirection { 0.7f, 1 }; ///< Turned by one step for the first badge
    inline constexpr float kBadgeStep = 0.28f;
    [[nodiscard]] Rect badge_rect(const Frame& frame, std::size_t slot);

    // How the menu buttons fly in. Each button moves on the circle by angle and radius, easing towards
    // where its menu wants it: on its place on the ring when its menu is open, twice as far out when its
    // menu is the one the open menu came from, else parked above the top of the screen. The angle eases
    // first and the radius only once the angle is close.
    struct Polar
    {
        float angle = 0;
        float radius = 0;
    };
    inline constexpr Polar kStart { 0, 480 };       ///< Off the right of the screen, where every button begins
    inline constexpr Polar kParked { 1.885f, 480 }; ///< Off the top
    inline constexpr float kParentRadiusFactor = 2;
    inline constexpr float kEaseRate = 10; ///< Per second
    inline constexpr float kAngleClose = 0.05f;
    inline constexpr float kRadiusClose = 0.5f;
    inline constexpr float kLabelFadeRate = 4; ///< Labels fade in over a quarter second once their button arrives

    /// One step of the easing; returns whether the button has arrived.
    bool ease(Polar& current, const Polar& goal, float elapsed);

} // namespace rct3::ui::front_screen
