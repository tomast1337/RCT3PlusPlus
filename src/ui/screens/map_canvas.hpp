// SPDX-License-Identifier: GPL-3.0-or-later
//
// The map itself, inside the map window: the park's picture fitted into the widget without stretching it,
// the people's picture over it, and the camera drawn on top as the ground it sees and a marker where it
// looks, pointing the way it faces.
//
// Pressing or dragging on the map asks to move the camera there; the owner takes the point and moves the
// camera. The wheel zooms in and out about the pointer; zoomed in, the view follows the camera so its
// marker stays in sight.
//
// Map points run from 0 to 1 across and down the whole map, as park::to_map gives them.
#pragma once

#include "ui/widget.hpp"

#include <optional>
#include <vector>

namespace rct3::ui
{

    namespace map_view
    {
        inline constexpr float kMaxZoom = 8;
        inline constexpr float kZoomPerNotch = 1.25f;
        /// Zoomed in, the camera's marker is kept this far from the edges, as a share of the view.
        inline constexpr float kFollowMargin = 0.2f;

        /// What part of the map shows: its middle, as a map point, and how many times larger than when the
        /// whole map fits.
        struct View
        {
            Point centre { 0.5f, 0.5f };
            float zoom = 1;
        };

        /// Where the whole map lies on the screen for `view`, in `area`, for a map `aspect` times as wide as
        /// it is tall: at zoom 1 as large as fits, centred.
        [[nodiscard]] Rect map_rect(const Rect& area, float aspect, const View& view);
        /// The map point at a screen point, and back.
        [[nodiscard]] Point to_map(const Rect& map, Point screen);
        [[nodiscard]] Point to_screen(const Rect& map, Point at);
        /// `view` kept from showing beyond the map where the map fills the area, and centred where it does not.
        [[nodiscard]] View clamped(const Rect& area, float aspect, View view);
        /// `view` zoomed `factor` times about the screen point `pointer`, which stays over the same map point.
        [[nodiscard]] View zoomed(const Rect& area, float aspect, View view, float factor, Point pointer);
        /// `view` moved the least that brings the map point `at` at least kFollowMargin inside the area.
        [[nodiscard]] View following(const Rect& area, float aspect, View view, Point at);
    } // namespace map_view

    struct MapCanvasStyle
    {
        Color back = hex("#24323d");
        float corner = 6;
        Color view_fill = hex("#ffffff30");
        Color view_line = hex("#ffffffd0");
        float view_line_width = 1.5f;
        Color marker = hex("#ffffff");
        Color marker_line = hex("#1d5fbf");
        float marker_size = 12;
        float marker_outline = 2.5f;
        float pointer_length = 14; ///< From the marker's middle along the way the camera faces
    };

    /// The camera as the map shows it, in map points.
    struct MapCamera
    {
        Point target;
        Point facing { 0, -1 };  ///< A step the way it looks, on the map; need not be of length 1
        std::vector<Point> sees; ///< The corners of the ground it sees, in order round it; empty when unknown
    };

    class MapCanvas : public Widget
    {
    public:
        MapCanvas(std::string name, MapCanvasStyle style);

        TextureId picture = 0;
        TextureId people = 0;
        float aspect = 1; ///< Of the map: its width over its height
        MapCamera camera;

        [[nodiscard]] const map_view::View& view() const
        {
            return view_;
        }
        /// The map point pressed or dragged to since the last call, if any.
        [[nodiscard]] std::optional<Point> take_pick();

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        /// Pressing or dragging aims the camera at a point.
        [[nodiscard]] CursorKind cursor(Point) const override
        {
            return CursorKind::Crosshair;
        }
        void on_press(Point p) override;
        void on_drag(Point p) override;
        void on_hover(Point p) override;
        bool on_wheel(Point notches) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        void pick(Point p);

        MapCanvasStyle style_;
        map_view::View view_;
        std::optional<Point> pick_;
        Point pointer_;
    };

} // namespace rct3::ui
