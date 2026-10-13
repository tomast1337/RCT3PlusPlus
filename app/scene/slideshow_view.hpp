// SPDX-License-Identifier: GPL-3.0-or-later
//
// The slideshow behind the front screen (ui/slideshow.hpp) on the device: the pictures it shows are read
// and uploaded as they come up, two at a time, and freed when they are gone. A picture that cannot be read
// is logged once and leaves its layer empty.
#pragma once

#include "render/device.hpp"
#include "ui/draw_list.hpp"
#include "ui/slideshow.hpp"

#include <cstddef>
#include <set>
#include <vector>

namespace rct3::scene
{

    struct SlideshowView
    {
        ui::Slideshow show;

        struct Loaded
        {
            std::size_t index = 0;
            render::TextureHandle texture;
        };
        std::vector<Loaded> loaded;
        std::set<std::size_t> failed;
    };

    /// Moves the show on and loads what it now shows.
    void update_slideshow_view(render::Device& device, SlideshowView& view, float elapsed);

    /// Adds the pictures to `list`, in `area`.
    void draw_slideshow_view(const SlideshowView& view, ui::DrawList& list, const ui::Rect& area);

    void destroy_slideshow_view(render::Device& device, SlideshowView& view);

} // namespace rct3::scene
