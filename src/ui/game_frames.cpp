// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/game_frames.hpp"

#include <algorithm>
#include <utility>

namespace rct3::ui::game_frames
{

    namespace
    {

        // The interface's colours, as we measure them on screen.
        constexpr Color kFrameBlue = hex("#2484be");   // Window bands
        constexpr Color kBorderBlue = hex("#085fa2");  // The lozenge's border
        constexpr Color kPatchBlue = hex("#56abe5");   // Selected rows, the front screen's discs
        constexpr Color kPatchBorder = hex("#4099d6"); // Round a selected row
        constexpr Color kDiscBorder = hex("#2a79af");  // Round the front screen's discs
        constexpr Color kIconBlue = hex("#44c7ff");    // The window's icon disc, the light on button faces
        constexpr Color kFaceTop = hex("#3a94c4");     // A button face's darker top
        constexpr Color kFaceRim = hex("#2072b0");
        constexpr Color kLineBlue = hex("#52aae7"); // The line round the front screen's white disc
        constexpr Color kGrey = hex("#c8c5c2");     // The thin sides of windows and the drawer
        constexpr Color kPanelGrey = hex("#cdcdcd");
        constexpr Color kNone = hex("#00000000");
        constexpr Color kFaintPink = hex("#fffbff"); // Where the pictures have fine pink lines on white

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

    } // namespace

    CodedFrame lozenge()
    {
        // The body leaves two units on the right and three below to its shadow.
        constexpr float kRadius = 10;
        CodedFrame f;
        f.layers.push_back({ inset_box(2, 3, 0, 0), solid(hex("#0000005d"), round_corners(kRadius)), { } });
        f.layers.push_back({ inset_box(0, 0, 2, 3), outlined(kWhite, kBorderBlue, 2, round_corners(kRadius)), { } });
        return f;
    }

    CodedFrame selected_row()
    {
        constexpr float kRadius = 7;
        CodedFrame f;
        Shape shadow = solid(hex("#0000007f"), round_corners(kRadius));
        shadow.softness = 1;
        f.layers.push_back({ inset_box(3, 3, 0, 0), shadow, { } });
        f.layers.push_back({ inset_box(0, 0, 3, 3), outlined(kPatchBlue, kPatchBorder, 1, round_corners(kRadius)), { } });
        return f;
    }

    CodedFrame button_face()
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
        Shape disc = outlined(kIconBlue, kFaceRim, 2, round_corners(kSize / 2));
        disc.top = kFaceTop;
        disc.fill_from = 0.28f;
        disc.fill_to = 0.65f;
        f.layers.push_back({ corner_box(0.25f, 0.4f, kSize, kSize), disc, { } });
        // A thin light ring just inside the rim.
        Shape light = solid(kIconBlue, round_corners(kSize / 2 - 2.25f));
        light.ring = 1.25f;
        f.layers.push_back({ corner_box(2.6f, 2.4f, kSize - 4.5f, kSize - 4.5f), light, { } });
        return f;
    }

    CodedFrame menu_circle()
    {
        // Measured on a picture 78 by 80 units; the disc fills its top-left 76 by 76.
        CodedFrame f;
        f.width = 78;
        f.height = 80;
        f.layers.push_back({ corner_box(0, 0, 76, 76), outlined(kPatchBlue, kDiscBorder, 2, round_corners(38)), { } });
        return f;
    }

    CodedFrame quarter_circle(Quarter quarter)
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
        f.layers.push_back({ corner_box(cx - kRadius, cy - kRadius, 2 * kRadius, 2 * kRadius), solid(kWhite, round_corners(kRadius)), whole });
        // Inside the line the white takes a faint pink.
        f.layers.push_back({ corner_box(cx - kLine, cy - kLine, 2 * kLine, 2 * kLine), solid(kFaintPink, round_corners(kLine)), whole });
        Shape line = solid(kLineBlue, round_corners(kLine));
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

        void add_grip(CodedFrame& f, FrameEdge x0, FrameEdge x1)
        {
            for (const float y : { 30.0f, 32.0f, 34.0f })
                f.layers.push_back({ { x0, { 0, y }, x1, { 0, y + 1 } }, solid(hex("#000000")), { } });
        }

    } // namespace

    CodedFrame drawer_back()
    {
        CodedFrame f;
        f.layers.push_back({ { { 1, -kDrawerShade }, { 0, 0 }, { 1, 0 }, { 1, 0 } }, solid(kDrawerShadow), { } });
        f.layers.push_back({ inset_box(0, 0, kDrawerShade, 0), solid(kWhite), { } });
        // Either side of the panel the white takes a faint pink.
        f.layers.push_back({ { { 0, 1 }, { 0, 0 }, { 0, kPanelLeft }, { 1, 0 } }, solid(kFaintPink), { } });
        f.layers.push_back({ { { 1, 1 - kPanelRight }, { 0, 0 }, { 1, -kDrawerShade - 1 }, { 1, 0 } }, solid(kFaintPink), { } });
        for (const auto& [x0, x1] : { std::pair { FrameEdge { 0, 0 }, FrameEdge { 0, 1 } }, { FrameEdge { 0, kPanelLeft }, FrameEdge { 0, kPanelLeft + 1 } }, { FrameEdge { 1, -kPanelRight }, FrameEdge { 1, 1 - kPanelRight } }, { FrameEdge { 1, -kDrawerShade - 1 }, FrameEdge { 1, -kDrawerShade } } })
            f.layers.push_back({ { x0, { 0, 0 }, x1, { 1, 0 } }, solid(kGrey), { } });
        return f;
    }

    CodedFrame drawer_lip_left()
    {
        // The left half: the drawer's left side and the panel's left corner; it reaches past its right edge,
        // where the right half carries on.
        CodedFrame f;
        const FrameBox own = inset_box(0, 0, 0, 0);
        f.layers.push_back({ { { 0, kDrawerShade }, { 0, -kPast }, { 1, kPast }, { 1, 0 } }, solid(kDrawerShadow, bottom_corners(kLipCorner)), own });
        f.layers.push_back({ { { 0, 0 }, { 0, -kPast }, { 1, kPast }, { 1, -kDrawerShade } }, outlined(kWhite, kGrey, 1, bottom_corners(kLipCorner)), own });
        f.layers.push_back({ { { 0, kPanelLeft }, { 0, -kPast }, { 1, kPast }, { 0, kPanelBottom } }, outlined(kWhite, kGrey, 1, bottom_corners(kPanelCorner)), own });
        add_grip(f, { 1, -kGripHalf }, { 1, 0 });
        return f;
    }

    CodedFrame drawer_lip_right()
    {
        CodedFrame f;
        const FrameBox own = inset_box(0, 0, 0, 0);
        f.layers.push_back({ { { 0, -kPast + kDrawerShade }, { 0, -kPast }, { 1, 0 }, { 1, 0 } }, solid(kDrawerShadow, bottom_corners(kLipCorner)), own });
        f.layers.push_back({ { { 0, -kPast }, { 0, -kPast }, { 1, -kDrawerShade }, { 1, -kDrawerShade } }, outlined(kWhite, kGrey, 1, bottom_corners(kLipCorner)), own });
        f.layers.push_back({ { { 0, -kPast }, { 0, -kPast }, { 1, 1 - kPanelRight }, { 0, kPanelBottom } }, outlined(kWhite, kGrey, 1, bottom_corners(kPanelCorner)), own });
        add_grip(f, { 0, 0 }, { 0, kGripHalf });
        return f;
    }

    namespace
    {

        // What the small and large windows share: a hollow body with a blue band along its top and thin grey
        // sides, its shadow three units right and down.
        constexpr float kWindowShade = 3;
        constexpr float kBand = 18;
        constexpr float kBandCorner = 11.7f;

        CodedFrame window_body(Color shadow, float bottom_corner)
        {
            CodedFrame f;
            add_side_shadow(f, kWindowShade, kWindowShade, shadow, { kBandCorner, kBandCorner, bottom_corner, bottom_corner });
            // The sides and bottom as an outline round nothing; its top is under the band.
            f.layers.push_back({ { { 0, 0 }, { 0, kBand / 2 }, { 1, -kWindowShade }, { 1, -kWindowShade } }, outlined(kNone, kGrey, 1, bottom_corners(bottom_corner)), { } });
            return f;
        }

    } // namespace

    CodedFrame mini_window()
    {
        CodedFrame f = window_body(hex("#0b0b0b99"), 4);
        f.layers.push_back({ { { 0, 0 }, { 0, 0 }, { 1, -kWindowShade }, { 0, kBand } }, solid(kFrameBlue, top_corners(kBandCorner)), { } });
        return f;
    }

    CodedFrame window_background()
    {
        constexpr float kTile = 16;
        constexpr float kPeriod = 4; // Rows from one pair of lines to the next
        constexpr Color kLine = hex("#fff7ff");
        constexpr Color kDeeperLine = hex("#fff4ff"); // The second line of each pair
        CodedFrame f;
        f.width = kTile;
        f.height = kTile;
        f.layers.push_back({ inset_box(0, 0, 0, 0), solid(kWhite), { } });
        for (int pair = 0; pair < static_cast<int>(kTile / kPeriod); ++pair)
        {
            const float y = static_cast<float>(pair) * kPeriod;
            f.layers.push_back({ corner_box(0, y + 1, kTile, 1), solid(kLine), { } });
            f.layers.push_back({ corner_box(0, y + 2, kTile, 1), solid(kDeeperLine), { } });
        }
        return f;
    }

    CodedFrame mini_window_panel()
    {
        CodedFrame f;
        f.layers.push_back({ inset_box(0, 0, 0, 0), outlined(kWhite, kPanelGrey, 1, round_corners(3.5f)), { } });
        return f;
    }

    CodedFrame window()
    {
        // The icon's disc sits in the top-left corner, overlapping the band, with a gap round it where the
        // band is cut away.
        constexpr float kDiscX = 29;
        constexpr float kDiscY = 40;
        constexpr float kDisc = 23;
        constexpr float kGap = 30;
        CodedFrame f = window_body(hex("#0000007f"), 3);
        Shape band = solid(kFrameBlue, top_corners(kBandCorner));
        band.cut_x = kDiscX;
        band.cut_y = kDiscY;
        band.cut_radius = kGap;
        f.layers.push_back({ { { 0, 0 }, { 0, 0 }, { 1, -kWindowShade }, { 0, kBand } }, band, { } });
        f.layers.push_back({ corner_box(kDiscX - kDisc, kDiscY - kDisc, 2 * kDisc, 2 * kDisc), solid(kIconBlue, round_corners(kDisc)), { } });
        // The title rule: a dark line from right of the disc to near the right side, under the title.
        f.layers.push_back({ { { 0, 61 }, { 0, 41 }, { 1, -14 }, { 0, 43 } }, solid(hex("#010101")), { } });
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
        constexpr Color kUnderRing = hex("#ffffffee");
        constexpr float kStripTop = 694; ///< A dark line, then the light band
        constexpr float kStripLight = 696;
        constexpr float kStripMiddle = 732; ///< A dark line with a thin light one under it
        constexpr float kStripThin = 737;
        constexpr float kStripThinEnd = 739;
        constexpr Rect kTitle { 260.5f, 707, 790, 759 }; ///< The lozenge; its right edge kept from the screen's
        constexpr Rect kTitleLine { 290, 736, 759, 738 };
        constexpr Color kBackdropBlue = hex("#2979ad");
        constexpr Color kVeil = hex("#fbf9f8c0"); // Fine stripes on the pictures, here their average
        constexpr Color kRingWhite = hex("#f7f3f7");
        constexpr Color kTitleWhite = hex("#fbf8f5");
        constexpr Color kBandWhite = hex("#f7f2f2"); // The menu labels' bands

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

    CodedFrame front_backdrop(const Rect& screen)
    {
        CodedFrame f;
        f.width = 1024;
        f.height = 768;
        // The veil, everywhere above the strip outside the white ring; the blue then covers it to the left.
        const Rect above { screen.x0 - 1, screen.y0 - 1, screen.x1 + 1, kStripTop };
        Shape veil = solid(kVeil);
        veil.cut_x = kCentreX - above.x0;
        veil.cut_y = kCentreY - above.y0;
        veil.cut_radius = kVeilInner;
        f.layers.push_back({ units(above), veil, { } });
        // Blue to the left of the centre, outside the veil's circle.
        const Rect blue { screen.x0 - 1, screen.y0 - 1, kCentreX, kStripTop };
        Shape left = solid(kBackdropBlue);
        left.cut_x = kCentreX - blue.x0;
        left.cut_y = kCentreY - blue.y0;
        left.cut_radius = kVeilOuter;
        f.layers.push_back({ units(blue), left, { } });
        const FrameBox above_strip = units({ screen.x0, screen.y0, screen.x1, kStripTop });
        Shape under = solid(kUnderRing, round_corners(kVeilInner));
        under.ring = kUnderRingWidth;
        f.layers.push_back({ units({ kCentreX - kVeilInner, kCentreY - kVeilInner, kCentreX + kVeilInner, kCentreY + kVeilInner }), under, above_strip });
        // The strip: dark, with two light bands across it.
        f.layers.push_back({ units({ screen.x0, kStripTop, screen.x1, std::max(screen.y1, 768.0f) }), solid(kBackdropBlue), { } });
        f.layers.push_back({ units({ screen.x0, kStripLight, screen.x1, kStripMiddle }), solid(kLineBlue), { } });
        f.layers.push_back({ units({ screen.x0, kStripThin, screen.x1, kStripThinEnd }), solid(kLineBlue), { } });
        // The lozenge the help sits on, and the rule between its two lines.
        f.layers.push_back({ along_strip(kTitle.x0, kTitle.y0, kTitle.x1, kTitle.y1, screen.x1), outlined(kTitleWhite, kBackdropBlue, 2, round_corners(kTitle.height() / 2)), { } });
        f.layers.push_back({ along_strip(kTitleLine.x0, kTitleLine.y0, kTitleLine.x1, kTitleLine.y1, screen.x1), solid(hex("#000000")), { } });
        return f;
    }

    CodedFrame front_arc()
    {
        CodedFrame f;
        f.width = 1024;
        f.height = 768;
        Shape ring = solid(kRingWhite, round_corners(kRingOuter));
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

    CodedFrame label_patch()
    {
        // Measured on a picture 95 by 97 units: a disc 95 across with its right half cut off at 46.
        CodedFrame f;
        f.width = 95;
        f.height = 97;
        f.layers.push_back({ corner_box(0, 0, 95, 95), solid(kBandWhite, round_corners(47.5f)), corner_box(0, 0, 46, 97) });
        return f;
    }

    CodedFrame label_end()
    {
        // Measured on a picture 95 by 85 units, the triangle filling its left 85.
        CodedFrame f;
        f.width = 95;
        f.height = 85;
        FrameLayer triangle { corner_box(0, 0, 85, 85), solid(kBandWhite), { } };
        triangle.below_diagonal = true;
        f.layers.push_back(triangle);
        return f;
    }

    namespace
    {

        // The loading bar is a pill 15 units high; each end shows its half, measured on ends 10 and 9 wide.
        constexpr Color kBarLight = hex("#a5d0f2");
        constexpr Color kBarRim = hex("#3578ac");

        Shape bar_pill()
        {
            // The pictures' edges are soft: a thin rim, blurred into the fill and the background.
            Shape s = outlined(kBarLight, kBarRim, 1, round_corners(7.5f));
            s.softness = 1.5f;
            return s;
        }

    } // namespace

    CodedFrame bar_end_left()
    {
        CodedFrame f;
        f.width = 10;
        f.height = 16;
        f.layers.push_back({ corner_box(0.4f, 1, 30, 15), bar_pill(), corner_box(0, 0, 10, 16) });
        return f;
    }

    CodedFrame bar_end_right()
    {
        CodedFrame f;
        f.width = 9;
        f.height = 16;
        f.layers.push_back({ corner_box(8.9f - 30, 1, 30, 15), bar_pill(), corner_box(0, 0, 9, 16) });
        return f;
    }

    CodedFrame curved_strip()
    {
        constexpr float kRadius = 20;
        constexpr float kShadow = 3;
        const Corners right { 0, kRadius, kRadius, 0 };
        CodedFrame f;
        f.layers.push_back({ inset_box(kShadow, kShadow, 0, 0), solid(hex("#0000005d"), right), { } });
        f.layers.push_back({ inset_box(0, 0, kShadow, kShadow), outlined(kPatchBlue, kDiscBorder, 1.5f, right), { } });
        return f;
    }

} // namespace rct3::ui::game_frames
