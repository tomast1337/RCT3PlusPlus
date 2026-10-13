// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pictures shown behind the front screen, one after another. The list comes from folders of picture
// files: the game's own and the user's screenshots. After each folder's files are added the whole list is
// shuffled, so the user's pictures mix with the game's. The show then steps through it in order, wrapping.
//
// Each new picture fades in over the one before for a second, then stays for three, the game's timing.
// At the start the second picture fades in over the first; a single picture fades in over itself. The
// pictures fill the screen's top right, beyond the edge of the front screen's artwork: from 74/1024 of the
// width in from the left, down to 74/768 of the height above the bottom, a gap that shrinks with the
// screen's width beyond 4:3 (the game's measure). They are stretched to fit, not cropped.
//
// This is the order and timing only; the owner loads the pictures shown() names.
#pragma once

#include "ui/draw_list.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace rct3::ui
{

    struct SlideshowTiming
    {
        float fade = 1; ///< Seconds a picture takes to fade in
        float hold = 3; ///< Seconds it then stays
    };

    /// The picture files in `folders`, the list shuffled with `seed` after each folder's are added.
    [[nodiscard]] std::vector<std::filesystem::path> slideshow_pictures(std::span<const std::filesystem::path> folders, std::uint32_t seed);

    /// Where the pictures go on a screen `width` by `height`, in any unit, top-left origin.
    [[nodiscard]] Rect slideshow_rect(float width, float height);

    class Slideshow
    {
    public:
        /// No pictures.
        Slideshow() = default;
        explicit Slideshow(std::vector<std::filesystem::path> pictures, SlideshowTiming timing = { });

        [[nodiscard]] const std::vector<std::filesystem::path>& pictures() const
        {
            return pictures_;
        }
        /// Shows another list, starting at its beginning; a fade under way carries on with the new pictures.
        void set_pictures(std::vector<std::filesystem::path> pictures);

        void update(float elapsed);

        /// What to draw: `under` whole, then `over` with `over_alpha`. Indices into pictures().
        struct Shown
        {
            std::optional<std::size_t> under;
            std::optional<std::size_t> over;
            float over_alpha = 0;
        };
        [[nodiscard]] Shown shown() const;

    private:
        std::vector<std::filesystem::path> pictures_;
        SlideshowTiming timing_;
        std::size_t under_ = 0;
        std::size_t over_ = 0;
        float time_ = 0; ///< Since `over_` began to fade in
    };

} // namespace rct3::ui
