// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/skin.hpp"

#include <algorithm>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        struct Lines
        {
            std::size_t x;
            std::size_t y;
        };

        std::optional<Lines> line_counts(ovl::GuiSkinKind kind)
        {
            switch (kind)
            {
                case ovl::GuiSkinKind::Rect:
                    return Lines { 2, 2 };
                case ovl::GuiSkinKind::Solid:
                    return Lines { 0, 0 };
                case ovl::GuiSkinKind::NinePatch:
                case ovl::GuiSkinKind::Frame:
                    return Lines { 4, 4 };
                case ovl::GuiSkinKind::HorizontalThree:
                    return Lines { 4, 2 };
                case ovl::GuiSkinKind::VerticalThree:
                    return Lines { 2, 4 };
            }
            return std::nullopt;
        }

        // One axis cut into cells: where each cell goes on screen and which texture span it shows.
        struct Span
        {
            float from;
            float to;
        };

        struct Axis
        {
            std::array<Span, 3> target { };
            std::array<Span, 3> source { };
            std::size_t count = 0;
        };

        Axis cut(std::span<const float> lines, float from, float to)
        {
            Axis axis;
            if (lines.size() == 2)
            {
                axis.target[0] = { from, to };
                axis.source[0] = { lines[0], lines[1] };
                axis.count = 1;
                return axis;
            }
            float start = lines[1] - lines[0];
            float end = lines[3] - lines[2];
            const float length = to - from;
            if (start + end > length && start + end > 0)
            {
                const float scale = std::max(length, 0.0f) / (start + end);
                start *= scale;
                end *= scale;
            }
            axis.target = { { { from, from + start }, { from + start, to - end }, { to - end, to } } };
            axis.source = { { { lines[0], lines[1] }, { lines[1], lines[2] }, { lines[2], lines[3] } } };
            axis.count = 3;
            return axis;
        }

    } // namespace

    std::optional<SkinLayout> skin_layout(const ovl::GuiSkin& skin)
    {
        const auto kind = static_cast<ovl::GuiSkinKind>(skin.kind);
        const auto counts = line_counts(kind);
        if (!counts || skin.coordinates.size() != counts->x + counts->y)
            return std::nullopt;

        SkinLayout layout { kind, { }, { }, counts->x, counts->y };
        // A Rect lists x0, y0, x1, y1; the other kinds list all x lines, then all y lines.
        if (kind == ovl::GuiSkinKind::Rect)
        {
            layout.xs = { static_cast<float>(skin.coordinates[0]), static_cast<float>(skin.coordinates[2]) };
            layout.ys = { static_cast<float>(skin.coordinates[1]), static_cast<float>(skin.coordinates[3]) };
            layout.mirror_x = layout.xs[0] > layout.xs[1];
            layout.mirror_y = layout.ys[0] > layout.ys[1];
            if (layout.mirror_x)
                std::swap(layout.xs[0], layout.xs[1]);
            if (layout.mirror_y)
                std::swap(layout.ys[0], layout.ys[1]);
        }
        else
        {
            for (std::size_t i = 0; i < counts->x; ++i)
                layout.xs[i] = static_cast<float>(skin.coordinates[i]);
            for (std::size_t i = 0; i < counts->y; ++i)
                layout.ys[i] = static_cast<float>(skin.coordinates[counts->x + i]);
        }
        if (!std::ranges::is_sorted(std::span(layout.xs).first(layout.x_count)) || !std::ranges::is_sorted(std::span(layout.ys).first(layout.y_count)))
            return std::nullopt;
        return layout;
    }

    SkinLayout flipped_vertically(const SkinLayout& layout, float texture_height)
    {
        SkinLayout flipped = layout;
        for (std::size_t i = 0; i < layout.y_count; ++i)
            flipped.ys[i] = texture_height - layout.ys[layout.y_count - 1 - i];
        return flipped;
    }

    SkinPieces skin_pieces(const SkinLayout& layout, const Rect& target, float texture_width, float texture_height)
    {
        SkinPieces out;
        if (layout.x_count == 0 || layout.y_count == 0 || texture_width <= 0 || texture_height <= 0)
            return out;
        const Axis xs = cut(std::span(layout.xs).first(layout.x_count), target.x0, target.x1);
        const Axis ys = cut(std::span(layout.ys).first(layout.y_count), target.y0, target.y1);
        for (std::size_t row = 0; row < ys.count; ++row)
        {
            for (std::size_t column = 0; column < xs.count; ++column)
            {
                if (layout.kind == ovl::GuiSkinKind::Frame && row == 1 && column == 1)
                    continue;
                Rect uv { xs.source[column].from / texture_width, ys.source[row].from / texture_height, xs.source[column].to / texture_width, ys.source[row].to / texture_height };
                if (layout.mirror_x)
                    std::swap(uv.x0, uv.x1);
                if (layout.mirror_y)
                    std::swap(uv.y0, uv.y1);
                out.items[out.count++] = { { xs.target[column].from, ys.target[row].from, xs.target[column].to, ys.target[row].to }, uv };
            }
        }
        return out;
    }

    void draw_skin(DrawList& list, const SkinLayout& layout, const Rect& target, TextureId texture, float texture_width, float texture_height, Color tint)
    {
        const SkinPieces pieces = skin_pieces(layout, target, texture_width, texture_height);
        for (const SkinPiece& piece : pieces.pieces())
            list.image(piece.target, texture, piece.uv, tint);
    }

} // namespace rct3::ui
