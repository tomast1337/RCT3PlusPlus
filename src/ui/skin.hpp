// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drawing a GUI skin item into a rectangle. A skin cuts a texture area into cells along up to four lines
// per axis. Along an axis with four lines the two outer cells are caps drawn at their size in the texture
// and the middle cell stretches to fill the rest; along an axis with two lines the single cell stretches.
// Nothing is tiled.
#pragma once

#include "ovl/gui_skin.hpp"
#include "ui/draw_list.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace rct3::ui
{

    /// A skin item's cut lines in texture pixels, y growing down the picture as it is shown.
    struct SkinLayout
    {
        ovl::GuiSkinKind kind = ovl::GuiSkinKind::Rect;
        std::array<float, 4> xs { };
        std::array<float, 4> ys { };
        std::size_t x_count = 0; ///< 0, 2 or 4
        std::size_t y_count = 0;
    };

    /// Splits a skin item's coordinates into x and y lines. Nothing for unknown kinds, a coordinate count
    /// that does not match the kind, or lines that go backwards.
    [[nodiscard]] std::optional<SkinLayout> skin_layout(const ovl::GuiSkin& skin);

    /// The skin as a picture stored with its rows the other way up: y lines are mirrored about the texture
    /// height and put back in increasing order.
    [[nodiscard]] SkinLayout flipped_vertically(const SkinLayout& layout, float texture_height);

    struct SkinPiece
    {
        Rect target; ///< Screen pixels
        Rect uv;     ///< Texture coordinates, 0..1
    };

    struct SkinPieces
    {
        std::array<SkinPiece, 9> items { };
        std::size_t count = 0;
        [[nodiscard]] std::span<const SkinPiece> pieces() const&
        {
            return { items.data(), count };
        }
        std::span<const SkinPiece> pieces() const&& = delete; // Would point into a temporary
    };

    /// Cuts `target` and the texture area the same way. Caps shrink in proportion when `target` is smaller
    /// than they are. Pieces left empty are still listed; DrawList drops them.
    [[nodiscard]] SkinPieces skin_pieces(const SkinLayout& layout, const Rect& target, float texture_width, float texture_height);

    void draw_skin(DrawList& list, const SkinLayout& layout, const Rect& target, TextureId texture, float texture_width, float texture_height, Color tint = kWhite);

} // namespace rct3::ui
