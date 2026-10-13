// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/slideshow_view.hpp"

#include "image/picture_file.hpp"
#include "logging/log.hpp"
#include "ui/ui_renderer.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::scene
{

    namespace
    {

        const SlideshowView::Loaded* find_loaded(const SlideshowView& view, std::size_t index)
        {
            const auto it = std::ranges::find(view.loaded, index, &SlideshowView::Loaded::index);
            return it == view.loaded.end() ? nullptr : &*it;
        }

        void load(render::Device& device, SlideshowView& view, std::size_t index)
        {
            if (find_loaded(view, index) != nullptr || view.failed.contains(index))
                return;
            const auto& path = view.show.pictures().at(index);
            auto image = image::load_picture(path);
            if (!image)
            {
                logging::logger("ui").warning("slideshow: {}", image.error());
                view.failed.insert(index);
                return;
            }
            const std::string label = path.filename().string();
            auto texture = device.create_texture({ image->width, image->height, render::TextureFormat::Rgba8, std::as_bytes(std::span(image->pixels)), label });
            if (!texture)
            {
                logging::logger("ui").warning("slideshow: {}: {}", path.string(), texture.error());
                view.failed.insert(index);
                return;
            }
            view.loaded.push_back({ index, *texture });
        }

    } // namespace

    void update_slideshow_view(render::Device& device, SlideshowView& view, float elapsed)
    {
        view.show.update(elapsed);
        const auto shown = view.show.shown();
        const auto wanted = [&](std::size_t index) { return index == shown.under || index == shown.over; };
        std::erase_if(view.loaded, [&](const SlideshowView::Loaded& l) {
            if (wanted(l.index))
                return false;
            device.destroy_texture(l.texture);
            return true;
        });
        for (const auto index : { shown.under, shown.over })
        {
            if (index)
                load(device, view, *index);
        }
    }

    void draw_slideshow_view(const SlideshowView& view, ui::DrawList& list, const ui::Rect& area)
    {
        const auto shown = view.show.shown();
        const auto draw = [&](std::optional<std::size_t> index, float alpha) {
            const SlideshowView::Loaded* loaded = index ? find_loaded(view, *index) : nullptr;
            if (loaded == nullptr || alpha <= 0)
                return;
            const auto a = static_cast<std::uint8_t>(std::lround(alpha * 255));
            list.image(area, ui::texture_id(loaded->texture), { 0, 0, 1, 1 }, { 255, 255, 255, a });
        };
        draw(shown.under, 1);
        draw(shown.over, shown.over_alpha);
    }

    void destroy_slideshow_view(render::Device& device, SlideshowView& view)
    {
        for (const auto& l : view.loaded)
            device.destroy_texture(l.texture);
        view = { };
    }

} // namespace rct3::scene
