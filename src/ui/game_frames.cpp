// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/game_frames.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <utility>

namespace rct3::ui::game_frames
{

    namespace
    {

        constexpr Color kNone = hex("#00000000");

        Shape solid(Color colour, Corners corners = { })
        {
            Shape s;
            s.top = colour;
            s.bottom = colour;
            s.corners = corners;
            return s;
        }

        Shape outlined(Color fill, Color outline, float width, Corners corners)
        {
            Shape s = solid(fill, corners);
            s.outline = outline;
            s.outline_width = width;
            return s;
        }

        Corners top_corners(float r)
        {
            return { r, r, 0, 0 };
        }

        Corners bottom_corners(float r)
        {
            return { 0, 0, r, r };
        }

        // A drop shadow `dx`, `dy` below and right of a body inset by as much from the far sides, shown only
        // where the body does not cover it: frames are hollow, so the shadow must not darken their middle.
        void add_side_shadow(CodedFrame& frame, float dx, float dy, Color colour, Corners corners)
        {
            const FrameBox shadow = inset_box(dx, dy, 0, 0);
            frame.layers.push_back({ shadow, solid(colour, corners), FrameBox { { 1, -dx }, { 0, 0 }, { 1, 0 }, { 1, 0 } } });
            frame.layers.push_back({ shadow, solid(colour, corners), FrameBox { { 0, 0 }, { 1, -dy }, { 1, -dx }, { 1, 0 } } });
        }

        // A triangle filling `box`, its point toward `way`: two halves of the box, each cut along one slanted
        // side, walked so the triangle lies on its right.
        void add_triangle(CodedFrame& frame, const FrameBox& box, Arrow way, Color colour)
        {
            const auto middle = [](FrameEdge a, FrameEdge b) { return FrameEdge { (a.share + b.share) / 2, (a.offset + b.offset) / 2 }; };
            FrameBox first = box;
            FrameBox second = box;
            std::array<CutLine, 2> sides;
            switch (way)
            {
                case Arrow::Up:
                    first.x1 = second.x0 = middle(box.x0, box.x1);
                    sides = { CutLine { { 0, 1 }, { 0.5f, 0 } }, CutLine { { 0.5f, 0 }, { 1, 1 } } };
                    break;
                case Arrow::Down:
                    first.x1 = second.x0 = middle(box.x0, box.x1);
                    sides = { CutLine { { 0.5f, 1 }, { 0, 0 } }, CutLine { { 1, 0 }, { 0.5f, 1 } } };
                    break;
                case Arrow::Left:
                    first.y1 = second.y0 = middle(box.y0, box.y1);
                    sides = { CutLine { { 0, 0.5f }, { 1, 0 } }, CutLine { { 1, 1 }, { 0, 0.5f } } };
                    break;
                case Arrow::Right:
                    first.y1 = second.y0 = middle(box.y0, box.y1);
                    sides = { CutLine { { 0, 0 }, { 1, 0.5f } }, CutLine { { 1, 0.5f }, { 0, 1 } } };
                    break;
            }
            frame.layers.push_back({ box, solid(colour), first, sides[0] });
            frame.layers.push_back({ box, solid(colour), second, sides[1] });
        }

        // A button `width` by `height` units: `rims` one unit each from the outside in, then `fill`, and an
        // arrow toward `way` in `mark` filling `mark_box`.
        CodedFrame arrow_button(float width, float height, std::initializer_list<Color> rims, Color fill, Arrow way, const FrameBox& mark_box, Color mark)
        {
            CodedFrame f;
            f.width = width;
            f.height = height;
            float in = 0;
            for (const Color rim : rims)
            {
                f.layers.push_back({ inset_box(in, in, in, in), solid(rim), { } });
                ++in;
            }
            f.layers.push_back({ inset_box(in, in, in, in), solid(fill), { } });
            add_triangle(f, mark_box, way, mark);
            return f;
        }

        // Where the arrow sits on a scroll arrow, 15 units square, moved in by `in` for a button with more rims:
        // 12 units across its base and 6 to its point, its base just off the middle.
        FrameBox arrow_mark(Arrow way, float in)
        {
            switch (way)
            {
                case Arrow::Up:
                    return corner_box(1.5f + in, 4 + in, 12, 6);
                case Arrow::Down:
                    return corner_box(1.5f + in, 5 + in, 12, 6);
                case Arrow::Left:
                    return corner_box(4 + in, 1.5f + in, 6, 12);
                case Arrow::Right:
                    return corner_box(5 + in, 1.5f + in, 6, 12);
            }
            return { };
        }

    } // namespace

    CodedFrame plain_box(const Palette& p)
    {
        CodedFrame f;
        f.layers.push_back({ inset_box(0, 0, 0, 0), outlined(kNone, p.box_line, 1, { }), { } });
        return f;
    }

    CodedFrame focused_box(const Palette& p)
    {
        CodedFrame f;
        f.layers.push_back({ inset_box(0, 0, 0, 0), outlined(kNone, p.accent, 2, { }), { } });
        return f;
    }

    CodedFrame round_button(const Palette& p)
    {
        CodedFrame f;
        f.width = 30;
        f.height = 30;
        f.layers.push_back({ inset_box(0, 0, 0, 0), outlined(p.paper, p.button_rim, 1, round_corners(15)), { } });
        return f;
    }

    CodedFrame scroll_rail(const Palette& p)
    {
        CodedFrame f;
        f.layers.push_back({ inset_box(0, 0, 0, 0), solid(p.paper), { } });
        return f;
    }

    CodedFrame scroll_rider(const Palette& p)
    {
        CodedFrame f;
        f.layers.push_back({ inset_box(0, 0, 0, 0), outlined(p.border, p.paper, 1, { }), { } });
        return f;
    }

    CodedFrame scroll_arrow(const Palette& p, Arrow way)
    {
        return arrow_button(15, 15, { p.paper }, p.border, way, arrow_mark(way, 0), p.text_on_band);
    }

    CodedFrame opener_disabled(const Palette& p)
    {
        return arrow_button(15, 15, { p.disabled_light }, p.disabled_fill, Arrow::Down, arrow_mark(Arrow::Down, 0), p.disabled_light);
    }

    CodedFrame square_arrow(const Palette& p, Arrow way, bool enabled)
    {
        if (enabled)
            return arrow_button(17, 17, { p.box_line, p.paper }, p.border, way, arrow_mark(way, 1), p.text_on_band);
        return arrow_button(17, 17, { p.disabled_rim, p.disabled_light }, p.disabled_fill, way, arrow_mark(way, 1), p.disabled_light);
    }

    CodedFrame spinner(const Palette& p, Arrow way, bool enabled)
    {
        // The arrow is 10 units across its base and 5 to its point, nearer the side it points to.
        const FrameBox mark = corner_box(5, way == Arrow::Up ? 1 : 2, 10, 5);
        if (enabled)
            return arrow_button(20, 8, { p.paper }, p.spinner, way, mark, p.text_on_band);
        return arrow_button(20, 8, { p.disabled_light }, p.disabled_spinner, way, mark, p.disabled_light);
    }

    CodedFrame tickbox(const Palette& p)
    {
        CodedFrame f;
        f.width = 12;
        f.height = 12;
        f.layers.push_back({ inset_box(0, 0, 0, 0), solid(p.paper), { } });
        f.layers.push_back({ inset_box(1, 1, 1, 1), outlined(p.paper, p.tick_line, 1, { }), { } });
        return f;
    }

    CodedFrame title_button(const Palette& p)
    {
        return colour_ring(p.strip, 14);
    }

    CodedFrame heading_box(const Palette& p, bool closed)
    {
        CodedFrame f;
        f.width = 7;
        f.height = 7;
        f.layers.push_back({ inset_box(0, 0, 0, 0), solid(p.heading_box, round_corners(1)), { } });
        f.layers.push_back({ corner_box(1, 3, 5, 1), solid(p.heading_sign), { } });
        if (closed)
            f.layers.push_back({ corner_box(3, 1, 1, 5), solid(p.heading_sign), { } });
        return f;
    }

    CodedFrame slider_thumb(const Palette& p)
    {
        CodedFrame f;
        f.width = 18;
        f.height = 13;
        add_triangle(f, corner_box(0, 0, 8, 13), Arrow::Left, p.rule);
        add_triangle(f, corner_box(1.5f, 1.5f, 5.5f, 10), Arrow::Left, p.slider_arrow);
        add_triangle(f, corner_box(10, 0, 8, 13), Arrow::Right, p.rule);
        add_triangle(f, corner_box(11, 1.5f, 5.5f, 10), Arrow::Right, p.slider_arrow);
        return f;
    }

    CodedFrame colour_ring(Color colour, float size)
    {
        CodedFrame f;
        f.width = size;
        f.height = size;
        f.layers.push_back({ inset_box(0, 0, 0, 0), solid(colour, round_corners(size / 2)), { } });
        return f;
    }

    CodedFrame colour_back(const Palette& p)
    {
        constexpr float kRound = 13;
        CodedFrame f;
        f.width = 34;
        f.height = 37;
        f.layers.push_back({ inset_box(2, 3, 0, 0), solid(hex("#00000070"), round_corners(kRound)), { } });
        f.layers.push_back({ inset_box(0, 0, 2, 3), outlined(p.colour_back, p.colour_back_line, 2, round_corners(kRound)), { } });
        return f;
    }

    namespace
    {
        constexpr Color kBarShadow = hex("#00000098");
        constexpr float kBarCorner = 13;
        constexpr float kBarShadowDrop = 3; // Below the band, and so the band stops this far above the bottom
        constexpr float kBarShadowShift = 2;
    } // namespace

    CodedFrame park_bar_left(const Palette& p)
    {
        CodedFrame f;
        f.layers.push_back({ inset_box(kBarShadowShift, kBarShadowDrop, 0, 0), solid(kBarShadow, { 0, 0, 0, kBarCorner }), { } });
        f.layers.push_back({ inset_box(0, 0, 0, kBarShadowDrop), solid(p.patch, { 0, 0, 0, kBarCorner }), { } });
        return f;
    }

    CodedFrame park_bar_middle(const Palette& p, float width, float height, float notch)
    {
        // The notch cuts the band and, shifted with it, the shadow, which then shows as a crescent inside the
        // notch's upper left.
        Shape band = solid(p.patch);
        band.cut_x = width / 2;
        band.cut_y = height;
        band.cut_radius = notch;
        CodedFrame f;
        Shape shadow = band;
        shadow.top = shadow.bottom = kBarShadow;
        f.layers.push_back({ FrameBox { { 0, kBarShadowShift }, { 0, kBarShadowDrop }, { 1, kBarShadowShift }, { 1, 0 } }, shadow, inset_box(0, 0, 0, 0) });
        f.layers.push_back({ inset_box(0, 0, 0, kBarShadowDrop), band, { } });
        return f;
    }

    CodedFrame park_bar_right(const Palette& p)
    {
        CodedFrame f;
        f.layers.push_back({ inset_box(0, kBarShadowDrop, 0, 0), solid(kBarShadow, { 0, 0, kBarCorner, 0 }), { } });
        f.layers.push_back({ inset_box(0, 0, kBarShadowShift, kBarShadowDrop), solid(p.patch, { 0, 0, kBarCorner, 0 }), { } });
        return f;
    }

    CodedFrame lozenge(const Palette& p, bool enabled)
    {
        // The body leaves two units on the right and three below to its shadow.
        constexpr float kRadius = 10;
        CodedFrame f;
        f.layers.push_back({ inset_box(2, 3, 0, 0), solid(hex("#0000005d"), round_corners(kRadius)), { } });
        f.layers.push_back({ inset_box(0, 0, 2, 3), outlined(p.paper, enabled ? p.border : p.disabled_fill, 2, round_corners(kRadius)), { } });
        return f;
    }

    CodedFrame selected_row(const Palette& p)
    {
        constexpr float kRadius = 7;
        CodedFrame f;
        Shape shadow = solid(hex("#0000007f"), round_corners(kRadius));
        shadow.softness = 1;
        f.layers.push_back({ inset_box(3, 3, 0, 0), shadow, { } });
        f.layers.push_back({ inset_box(0, 0, 3, 3), outlined(p.patch, p.patch_border, 1, round_corners(kRadius)), { } });
        return f;
    }

    CodedFrame button_face(const Palette& p)
    {
        // Measured on a face 32 units wide and 33 high, the disc a little above its middle with a shadow
        // under it.
        CodedFrame f;
        f.width = 32;
        f.height = 33;
        constexpr float kSize = 31.5f;
        // A dark edge all round, deepest below.
        Shape shadow = solid(hex("#000000a0"), round_corners(kSize / 2));
        shadow.softness = 1;
        f.layers.push_back({ corner_box(0.5f, 1.25f, kSize, kSize), shadow, { } });
        Shape disc = outlined(p.icon, p.face_rim, 2, round_corners(kSize / 2));
        disc.top = p.face_top;
        disc.fill_from = 0.28f;
        disc.fill_to = 0.65f;
        f.layers.push_back({ corner_box(0.25f, 0.4f, kSize, kSize), disc, { } });
        // A thin light ring just inside the rim.
        Shape light = solid(p.icon, round_corners(kSize / 2 - 2.25f));
        light.ring = 1.25f;
        f.layers.push_back({ corner_box(2.6f, 2.4f, kSize - 4.5f, kSize - 4.5f), light, { } });
        return f;
    }

    CodedFrame menu_circle(const Palette& p)
    {
        // Measured on a picture 78 by 80 units; the disc fills its top-left 76 by 76.
        CodedFrame f;
        f.width = 78;
        f.height = 80;
        f.layers.push_back({ corner_box(0, 0, 76, 76), outlined(p.patch, p.disc_border, 2, round_corners(38)), { } });
        return f;
    }

    CodedFrame quarter_circle(const Palette& p, Quarter quarter)
    {
        // Measured on a quarter 324 by 322 units. The disc's middle sits just past the quarter's inner corner;
        // the blue line runs round it 50 units in from its edge.
        constexpr float kWidth = 324;
        constexpr float kHeight = 322;
        constexpr float kRadius = 322;
        constexpr float kLine = 272;
        constexpr float kLineWidth = 2;
        const bool left = quarter == Quarter::TopLeft || quarter == Quarter::BottomLeft;
        const bool top = quarter == Quarter::TopLeft || quarter == Quarter::TopRight;
        const float cx = left ? kWidth : 0;
        const float cy = top ? kHeight + 2 : -2;
        CodedFrame f;
        f.width = kWidth;
        f.height = kHeight;
        const FrameBox whole = inset_box(0, 0, 0, 0);
        f.layers.push_back({ corner_box(cx - kRadius, cy - kRadius, 2 * kRadius, 2 * kRadius), solid(p.paper, round_corners(kRadius)), whole });
        // Inside the line the white takes a faint pink.
        f.layers.push_back({ corner_box(cx - kLine, cy - kLine, 2 * kLine, 2 * kLine), solid(p.paper_tint, round_corners(kLine)), whole });
        Shape line = solid(p.line, round_corners(kLine));
        line.ring = kLineWidth;
        f.layers.push_back({ corner_box(cx - kLine, cy - kLine, 2 * kLine, 2 * kLine), line, whole });
        return f;
    }

    // The drawer is white with thin grey sides and a shadow two units wide on its right. Down its middle runs
    // the panel the messages sit on, between grey lines 9 units in from the left side and 12 from the right.
    // Along the bottom, the lip closes the panel with round corners and carries three short black lines, the
    // grip, centred where its halves meet.
    namespace
    {

        constexpr Color kDrawerShadow = hex("#0000004b");
        constexpr float kDrawerShade = 2;
        constexpr float kPanelLeft = 9;
        constexpr float kPanelRight = 12;
        constexpr float kPanelBottom = 13; // From the lip's top
        constexpr float kPanelCorner = 4;
        constexpr float kLipCorner = 3;
        constexpr float kGripHalf = 5;
        constexpr float kPast = 24; // How far a half reaches past its far side, to be cut there

        void add_grip(const Palette& p, CodedFrame& f, FrameEdge x0, FrameEdge x1)
        {
            for (const float y : { 30.0f, 32.0f, 34.0f })
                f.layers.push_back({ { x0, { 0, y }, x1, { 0, y + 1 } }, solid(p.rule), { } });
        }

    } // namespace

    CodedFrame drawer_back(const Palette& p)
    {
        CodedFrame f;
        f.layers.push_back({ { { 1, -kDrawerShade }, { 0, 0 }, { 1, 0 }, { 1, 0 } }, solid(kDrawerShadow), { } });
        f.layers.push_back({ inset_box(0, 0, kDrawerShade, 0), solid(p.paper), { } });
        // Either side of the panel the white takes a faint pink.
        f.layers.push_back({ { { 0, 1 }, { 0, 0 }, { 0, kPanelLeft }, { 1, 0 } }, solid(p.paper_tint), { } });
        f.layers.push_back({ { { 1, 1 - kPanelRight }, { 0, 0 }, { 1, -kDrawerShade - 1 }, { 1, 0 } }, solid(p.paper_tint), { } });
        for (const auto& [x0, x1] : { std::pair { FrameEdge { 0, 0 }, FrameEdge { 0, 1 } }, { FrameEdge { 0, kPanelLeft }, FrameEdge { 0, kPanelLeft + 1 } }, { FrameEdge { 1, -kPanelRight }, FrameEdge { 1, 1 - kPanelRight } }, { FrameEdge { 1, -kDrawerShade - 1 }, FrameEdge { 1, -kDrawerShade } } })
            f.layers.push_back({ { x0, { 0, 0 }, x1, { 1, 0 } }, solid(p.side), { } });
        return f;
    }

    CodedFrame drawer_lip_left(const Palette& p)
    {
        // The left half: the drawer's left side and the panel's left corner; it reaches past its right edge,
        // where the right half carries on.
        CodedFrame f;
        const FrameBox own = inset_box(0, 0, 0, 0);
        f.layers.push_back({ { { 0, kDrawerShade }, { 0, -kPast }, { 1, kPast }, { 1, 0 } }, solid(kDrawerShadow, bottom_corners(kLipCorner)), own });
        f.layers.push_back({ { { 0, 0 }, { 0, -kPast }, { 1, kPast }, { 1, -kDrawerShade } }, outlined(p.paper, p.side, 1, bottom_corners(kLipCorner)), own });
        f.layers.push_back({ { { 0, kPanelLeft }, { 0, -kPast }, { 1, kPast }, { 0, kPanelBottom } }, outlined(p.paper, p.side, 1, bottom_corners(kPanelCorner)), own });
        add_grip(p, f, { 1, -kGripHalf }, { 1, 0 });
        return f;
    }

    CodedFrame drawer_lip_right(const Palette& p)
    {
        CodedFrame f;
        const FrameBox own = inset_box(0, 0, 0, 0);
        f.layers.push_back({ { { 0, -kPast + kDrawerShade }, { 0, -kPast }, { 1, 0 }, { 1, 0 } }, solid(kDrawerShadow, bottom_corners(kLipCorner)), own });
        f.layers.push_back({ { { 0, -kPast }, { 0, -kPast }, { 1, -kDrawerShade }, { 1, -kDrawerShade } }, outlined(p.paper, p.side, 1, bottom_corners(kLipCorner)), own });
        f.layers.push_back({ { { 0, -kPast }, { 0, -kPast }, { 1, 1 - kPanelRight }, { 0, kPanelBottom } }, outlined(p.paper, p.side, 1, bottom_corners(kPanelCorner)), own });
        add_grip(p, f, { 0, 0 }, { 0, kGripHalf });
        return f;
    }

    namespace
    {

        // What the small and large windows share: a hollow body with a blue band along its top and thin grey
        // sides, its shadow three units right and down.
        constexpr float kWindowShade = 3;
        constexpr float kBand = 18;
        constexpr float kBandCorner = 11.7f;

        CodedFrame window_body(const Palette& p, Color shadow, float bottom_corner)
        {
            CodedFrame f;
            add_side_shadow(f, kWindowShade, kWindowShade, shadow, { kBandCorner, kBandCorner, bottom_corner, bottom_corner });
            // The sides and bottom as an outline round nothing; its top is under the band.
            f.layers.push_back({ { { 0, 0 }, { 0, kBand / 2 }, { 1, -kWindowShade }, { 1, -kWindowShade } }, outlined(kNone, p.side, 1, bottom_corners(bottom_corner)), { } });
            return f;
        }

    } // namespace

    CodedFrame mini_window(const Palette& p)
    {
        CodedFrame f = window_body(p, hex("#0b0b0b99"), 4);
        f.layers.push_back({ { { 0, 0 }, { 0, 0 }, { 1, -kWindowShade }, { 0, kBand } }, solid(p.accent, top_corners(kBandCorner)), { } });
        return f;
    }

    CodedFrame window_background(const Palette& p)
    {
        constexpr float kTile = 16;
        constexpr float kPeriod = 4; // Rows from one pair of lines to the next
        CodedFrame f;
        f.width = kTile;
        f.height = kTile;
        f.layers.push_back({ inset_box(0, 0, 0, 0), solid(p.paper), { } });
        for (int pair = 0; pair < static_cast<int>(kTile / kPeriod); ++pair)
        {
            const float y = static_cast<float>(pair) * kPeriod;
            f.layers.push_back({ corner_box(0, y + 1, kTile, 1), solid(p.paper_line), { } });
            f.layers.push_back({ corner_box(0, y + 2, kTile, 1), solid(p.paper_line_deep), { } });
        }
        return f;
    }

    CodedFrame mini_window_panel(const Palette& p)
    {
        CodedFrame f;
        f.layers.push_back({ inset_box(0, 0, 0, 0), outlined(p.paper, p.panel_edge, 1, round_corners(3.5f)), { } });
        return f;
    }

    CodedFrame window(const Palette& p)
    {
        // The icon's disc sits in the top-left corner, overlapping the band, with a gap round it where the
        // band is cut away.
        constexpr float kDiscX = 29;
        constexpr float kDiscY = 40;
        constexpr float kDisc = 23;
        constexpr float kGap = 30;
        CodedFrame f = window_body(p, hex("#0000007f"), 3);
        Shape band = solid(p.accent, top_corners(kBandCorner));
        band.cut_x = kDiscX;
        band.cut_y = kDiscY;
        band.cut_radius = kGap;
        f.layers.push_back({ { { 0, 0 }, { 0, 0 }, { 1, -kWindowShade }, { 0, kBand } }, band, { } });
        f.layers.push_back({ corner_box(kDiscX - kDisc, kDiscY - kDisc, 2 * kDisc, 2 * kDisc), solid(p.icon, round_corners(kDisc)), { } });
        // The title rule: a dark line from right of the disc to near the right side, under the title.
        f.layers.push_back({ { { 0, 61 }, { 0, 41 }, { 1, -14 }, { 0, 43 } }, solid(p.rule), { } });
        return f;
    }

    namespace
    {

        // The front screen's backdrop, in design units. Everything is round one centre: the veil runs from
        // a white band under the ring out to the screen's edges, but for a circle that leaves the left edge
        // blue; the ring lies over the band, and the strip along the bottom closes it all.
        constexpr float kCentreX = 735.5f;
        constexpr float kCentreY = 241.5f;
        constexpr float kVeilOuter = 660;
        constexpr float kRingOuter = 441;
        /// Approximation: matched by eye to the running game on a wide screen; the arc's pictures alone give 22.
        constexpr float kRingWidth = 30;
        constexpr float kVeilInner = 442; ///< Inside it, a thinner white band under the ring
        constexpr float kUnderRingWidth = 19.5f;
        constexpr float kStripTop = 694; ///< A dark line, then the light band
        constexpr float kStripLight = 696;
        constexpr float kStripMiddle = 732; ///< A dark line with a thin light one under it
        constexpr float kStripThin = 737;
        constexpr float kStripThinEnd = 739;
        constexpr Rect kTitle { 260.5f, 707, 790, 759 }; ///< The lozenge; its right edge kept from the screen's
        constexpr Rect kTitleLine { 290, 736, 759, 738 };

        FrameBox units(const Rect& r)
        {
            return corner_box(r.x0, r.y0, r.width(), r.height());
        }

        // A box running along the strip: its left edge `left` units in, its right `right` units in from the
        // design's right edge, moved out to the screen's right edge at `screen_right`.
        FrameBox along_strip(float left, float top, float right, float bottom, float screen_right)
        {
            return units({ left, top, screen_right - (1024 - right), bottom });
        }

        Rect shifted(const Rect& r, float dx, float dy)
        {
            return { r.x0 + dx, r.y0 + dy, r.x1 + dx, r.y1 + dy };
        }

        Rect box_units(const FrameBox& b)
        {
            return { b.x0.offset, b.y0.offset, b.x1.offset, b.y1.offset };
        }

    } // namespace

    CodedFrame front_backdrop(const Palette& p, const Rect& screen)
    {
        CodedFrame f;
        f.width = 1024;
        f.height = 768;
        // The veil, everywhere above the strip outside the white ring; the blue then covers it to the left.
        const Rect above { screen.x0 - 1, screen.y0 - 1, screen.x1 + 1, kStripTop };
        Shape veil = solid(p.veil);
        veil.cut_x = kCentreX - above.x0;
        veil.cut_y = kCentreY - above.y0;
        veil.cut_radius = kVeilInner;
        f.layers.push_back({ units(above), veil, { } });
        // Blue to the left of the centre, outside the veil's circle.
        const Rect blue { screen.x0 - 1, screen.y0 - 1, kCentreX, kStripTop };
        Shape left = solid(p.backdrop);
        left.cut_x = kCentreX - blue.x0;
        left.cut_y = kCentreY - blue.y0;
        left.cut_radius = kVeilOuter;
        f.layers.push_back({ units(blue), left, { } });
        const FrameBox above_strip = units({ screen.x0, screen.y0, screen.x1, kStripTop });
        Shape under = solid(p.under_ring, round_corners(kVeilInner));
        under.ring = kUnderRingWidth;
        f.layers.push_back({ units({ kCentreX - kVeilInner, kCentreY - kVeilInner, kCentreX + kVeilInner, kCentreY + kVeilInner }), under, above_strip });
        // The strip: dark, with two light bands across it.
        f.layers.push_back({ units({ screen.x0, kStripTop, screen.x1, std::max(screen.y1, 768.0f) }), solid(p.backdrop), { } });
        f.layers.push_back({ units({ screen.x0, kStripLight, screen.x1, kStripMiddle }), solid(p.line), { } });
        f.layers.push_back({ units({ screen.x0, kStripThin, screen.x1, kStripThinEnd }), solid(p.line), { } });
        // The lozenge the help sits on, and the rule between its two lines.
        f.layers.push_back({ along_strip(kTitle.x0, kTitle.y0, kTitle.x1, kTitle.y1, screen.x1), outlined(p.title_paper, p.backdrop, 2, round_corners(kTitle.height() / 2)), { } });
        f.layers.push_back({ along_strip(kTitleLine.x0, kTitleLine.y0, kTitleLine.x1, kTitleLine.y1, screen.x1), solid(p.rule), { } });
        return f;
    }

    CodedFrame front_arc(const Palette& p)
    {
        CodedFrame f;
        f.width = 1024;
        f.height = 768;
        Shape ring = solid(p.ring, round_corners(kRingOuter));
        ring.ring = kRingWidth;
        f.layers.push_back({ units({ kCentreX - kRingOuter, kCentreY - kRingOuter, kCentreX + kRingOuter, kCentreY + kRingOuter }), ring, { } });
        return f;
    }

    CodedFrame front_piece(const CodedFrame& whole, const Rect& piece)
    {
        CodedFrame f;
        f.width = piece.width();
        f.height = piece.height();
        const Rect inside { 0, 0, piece.width(), piece.height() };
        for (const FrameLayer& layer : whole.layers)
        {
            FrameLayer moved = layer;
            moved.box = units(shifted(box_units(layer.box), -piece.x0, -piece.y0));
            Rect clip = inside;
            if (layer.clip)
                clip = intersect(clip, shifted(box_units(*layer.clip), -piece.x0, -piece.y0));
            if (clip.empty())
                continue;
            moved.clip = units(clip);
            f.layers.push_back(moved);
        }
        return f;
    }

    CodedFrame label_patch(const Palette& p)
    {
        // Measured on a picture 95 by 97 units: a disc 95 across with its right half cut off at 46.
        CodedFrame f;
        f.width = 95;
        f.height = 97;
        f.layers.push_back({ corner_box(0, 0, 95, 95), solid(p.label_band, round_corners(47.5f)), corner_box(0, 0, 46, 97) });
        return f;
    }

    CodedFrame label_end(const Palette& p)
    {
        // Measured on a picture 95 by 85 units, the triangle filling its left 85.
        CodedFrame f;
        f.width = 95;
        f.height = 85;
        FrameLayer triangle { corner_box(0, 0, 85, 85), solid(p.label_band), { } };
        triangle.cut_line = kBelowDiagonal;
        f.layers.push_back(triangle);
        return f;
    }

    CodedFrame curved_strip(const Palette& p)
    {
        constexpr float kRadius = 20;
        constexpr float kShadow = 3;
        const Corners right { 0, kRadius, kRadius, 0 };
        CodedFrame f;
        f.layers.push_back({ inset_box(kShadow, kShadow, 0, 0), solid(hex("#0000005d"), right), { } });
        f.layers.push_back({ inset_box(0, 0, kShadow, kShadow), outlined(p.patch, p.disc_border, 1.5f, right), { } });
        return f;
    }

} // namespace rct3::ui::game_frames
