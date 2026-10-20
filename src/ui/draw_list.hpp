// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 2D draw list the game interface is drawn with. Widgets append rectangles in painter's order every
// frame; the list turns them into quads, cuts them to the current clip rectangle on the CPU and groups
// runs of quads that use the same texture. The renderer uploads the result once and issues one draw per
// group. Nothing is kept between frames: clear() and draw again.
//
// Callers work in logical units, origin at the top-left corner, y down. The list's scale turns them into
// screen pixels, so one layout serves every window size and pixel density; edges are then rounded to whole
// pixels so that point-sampled skins land on the pixel grid. Output and clip() are in pixels.
//
// Shapes drawn in code (shape.hpp) go into a list of their own, with the measures their shader needs; their
// batches index that list. Batches stay in painter's order whichever list they draw from.
#pragma once

#include "core/maybe.hpp"
#include "math/hex_colour.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::ui
{

    struct Color
    {
        std::uint8_t r = 255;
        std::uint8_t g = 255;
        std::uint8_t b = 255;
        std::uint8_t a = 255;
        bool operator==(const Color&) const = default;
    };

    /// The colour `text` ("#rrggbb" or "#rrggbbaa") names; see math/hex_colour.hpp.
    consteval Color hex(std::string_view text)
    {
        const HexColour c = rct3::hex(text);
        return { c.r, c.g, c.b, c.a };
    }

    inline constexpr Color kWhite = hex("#ffffff");

    /// Axis-aligned rectangle from (x0, y0) to (x1, y1). Empty when x1 <= x0 or y1 <= y0.
    struct Rect
    {
        float x0 = 0;
        float y0 = 0;
        float x1 = 0;
        float y1 = 0;

        [[nodiscard]] float width() const
        {
            return x1 - x0;
        }
        [[nodiscard]] float height() const
        {
            return y1 - y0;
        }
        [[nodiscard]] bool empty() const
        {
            return !(x1 > x0 && y1 > y0);
        }
        bool operator==(const Rect&) const = default;
    };

    [[nodiscard]] Rect intersect(const Rect& a, const Rect& b);

    /// Renderer texture the quads sample. 0 means none: the quad is drawn in its colour alone (the renderer
    /// binds a white texture for it).
    using TextureId = std::uint32_t;

    /// The vertex the renderer receives. 20 bytes: position, colour (r, g, b, a), texture coordinate.
    struct Vertex
    {
        float x = 0;
        float y = 0;
        std::array<std::uint8_t, 4> color { };
        float u = 0;
        float v = 0;
    };

    /// The vertex of a shape's quad (shape.hpp), 96 bytes. The shape's measures repeat on each corner so one draw
    /// can hold many shapes. Lengths are in pixels.
    struct ShapeVertex
    {
        float x = 0;
        float y = 0;
        std::array<float, 4> local { }; ///< From the shape's middle; then its half width and half height
        std::array<float, 4> radii { }; ///< Of the corners, clockwise from the top-left
        std::array<float, 4> edge { };  ///< Softness, outline width, light width, ring width
        std::array<std::uint8_t, 4> top { };
        std::array<std::uint8_t, 4> bottom { };
        std::array<std::uint8_t, 4> outline { };
        std::array<std::uint8_t, 4> light { };
        std::array<float, 4> cut { };  ///< Centre of the disc cut out, from the shape's middle; its radius; unused
        std::array<float, 2> fill { }; ///< Shares of the height where the fill starts and stops changing
    };

    enum class BatchKind : std::uint8_t
    {
        Quads,  ///< Indices into vertices(), drawn with the batch's texture
        Shapes, ///< Indices into shape_vertices(), drawn with the shape shader
    };

    /// A run of indices drawn with one texture.
    struct Batch
    {
        TextureId texture = 0;
        std::uint32_t first_index = 0;
        std::uint32_t index_count = 0;
        BatchKind kind = BatchKind::Quads;
    };

    /// Corner colours, clockwise from the top-left.
    struct CornerColors
    {
        Color top_left;
        Color top_right;
        Color bottom_right;
        Color bottom_left;
    };

    struct Shape;

    class DrawList
    {
    public:
        /// Starts a new frame: drops all geometry and the clip stack. Keeps the scale.
        void clear();

        /// Pixels per logical unit, applied to every rectangle drawn or clipped after this. Default 1.
        void set_scale(float scale)
        {
            scale_ = scale;
        }
        [[nodiscard]] float scale() const
        {
            return scale_;
        }

        void fill(const Rect& rect, Color color);
        void gradient(const Rect& rect, const CornerColors& colors);
        /// `uv` is the part of the texture to show, in 0..1 texture coordinates; `tint` multiplies it.
        void image(const Rect& rect, TextureId texture, const Rect& uv, Color tint = kWhite);
        void quad(const Rect& rect, TextureId texture, const Rect& uv, const CornerColors& colors);
        /// The round part of an image: the ellipse that fits in `rect`, showing the same part of the texture
        /// image() would. It is not cut by the clip, so it is left out unless it lies wholly inside it.
        void disc_image(const Rect& rect, TextureId texture, const Rect& uv, Color tint = kWhite);
        /// A shape drawn in code (shape.hpp), cut to the clip like a quad.
        void shape(const Shape& shape);
        /// A convex polygon in one colour, its corners (x, y) in order either way round, cut to the clip. Its
        /// edges are not smoothed; for outlines and markers over pictures, such as a view drawn on a map.
        void polygon(std::span<const std::array<float, 2>> corners, Color color);

        /// Narrows drawing to `rect` within the current clip until the matching pop_clip. Returns false when
        /// nothing would be visible, so the caller may skip what it was about to draw (it still has to pop).
        bool push_clip(const Rect& rect);
        void pop_clip();
        /// The rectangle drawing is limited to, or nothing when no clip is pushed.
        [[nodiscard]] std::optional<const Rect&> clip() const;

        [[nodiscard]] std::span<const Vertex> vertices() const
        {
            return vertices_;
        }
        [[nodiscard]] std::span<const std::uint32_t> indices() const
        {
            return indices_;
        }
        [[nodiscard]] std::span<const ShapeVertex> shape_vertices() const
        {
            return shape_vertices_;
        }
        [[nodiscard]] std::span<const Batch> batches() const
        {
            return batches_;
        }

    private:
        void add_indices(BatchKind kind, TextureId texture, std::uint32_t base, std::span<const std::uint32_t> corners);

        std::vector<Vertex> vertices_;
        std::vector<ShapeVertex> shape_vertices_;
        std::vector<std::uint32_t> indices_;
        std::vector<Batch> batches_;
        std::vector<Rect> clips_;
        float scale_ = 1;
    };

} // namespace rct3::ui
