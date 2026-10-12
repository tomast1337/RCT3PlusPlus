// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/skin_gallery.hpp"

#include "ui/scale.hpp"
#include "ui/skin.hpp"

#include <cmath>
#include <cstddef>
#include <span>

namespace rct3::scene
{

    namespace
    {

        // Draws a named skin; missing names draw nothing, so the gallery degrades instead of failing.
        struct Painter
        {
            const SkinGallery& gallery;
            ui::DrawList& list;

            void skin(std::string_view name, const ui::Rect& rect, ui::Color tint = ui::kWhite) const
            {
                const auto skin = gallery.skins.find(name);
                if (skin == gallery.skins.end())
                    return;
                const auto texture = gallery.textures.find(skin->second.texture);
                if (texture == gallery.textures.end())
                    return;
                ui::draw_skin(list, skin->second.layout, rect, ui::texture_id(texture->second.handle), texture->second.width, texture->second.height, tint);
            }

            /// The skin's size in the texture, for skins drawn at their natural size.
            [[nodiscard]] std::array<float, 2> size(std::string_view name) const
            {
                const auto skin = gallery.skins.find(name);
                if (skin == gallery.skins.end())
                    return { 0, 0 };
                const ui::SkinLayout& l = skin->second.layout;
                return { l.xs[l.x_count - 1] - l.xs[0], l.ys[l.y_count - 1] - l.ys[0] };
            }

            void natural(std::string_view name, float x, float y, ui::Color tint = ui::kWhite) const
            {
                const auto [w, h] = size(name);
                skin(name, { x, y, x + w, y + h }, tint);
            }
        };

    } // namespace

    std::expected<SkinGallery, std::string> create_skin_gallery(render::Device& device, const std::filesystem::path& path)
    {
        auto set = ui::load_skin_set(path);
        if (!set)
            return std::unexpected(set.error());

        SkinGallery gallery;
        auto renderer = ui::create_ui_renderer(device);
        if (!renderer)
            return std::unexpected(renderer.error());
        gallery.renderer = *renderer;

        for (const auto& [name, image] : set->images)
        {
            const auto pixels = std::as_bytes(std::span(image.pixels));
            auto texture = device.create_texture({ image.width, image.height, render::TextureFormat::Rgba8, pixels, name });
            if (!texture)
            {
                destroy_skin_gallery(device, gallery);
                return std::unexpected(name + ": " + texture.error());
            }
            gallery.textures.emplace(name, GalleryTexture { *texture, static_cast<float>(image.width), static_cast<float>(image.height) });
        }
        gallery.skins = std::move(set->skins);
        return gallery;
    }

    void draw_skin_gallery(render::Device& device, SkinGallery& gallery, double seconds)
    {
        const render::Extent extent = device.extent();
        ui::DrawList& list = gallery.list;
        list.clear();
        list.set_scale(ui::auto_scale(static_cast<float>(extent.height)));

        // Layout in logical units: the dialog takes a share of the window and breathes a little so the
        // stretching is visible without resizing.
        const float scale = list.scale();
        const float view_w = static_cast<float>(extent.width) / scale;
        const float view_h = static_cast<float>(extent.height) / scale;
        const float breathe = 1.0f + 0.05f * std::sin(static_cast<float>(seconds));
        const float w = std::round(std::min(view_w - 40, 520.0f) * breathe);
        const float h = std::round(std::min(view_h - 40, 360.0f) * breathe);
        const float left = std::round((view_w - w) / 2);
        const float top = std::round((view_h - h) / 2);
        const ui::Rect window { left, top, left + w, top + h };

        const Painter paint { gallery, list };
        paint.skin("GUI2MiniDialog_Background", { window.x0 + 6, window.y0 + 6, window.x1 - 6, window.y1 - 6 });
        paint.skin("GUI2Dialog_Frame", window);

        // Title bar buttons in the top-right corner.
        const auto [bw, bh] = paint.size("GUI2Dialog_TitleButtonUp");
        paint.natural("GUI2Dialog_TitleButtonUp", window.x1 - 2 * bw - 14, window.y0 + 6);
        paint.natural("GUI2Dialog_TitleButtonUp", window.x1 - bw - 10, window.y0 + 6, { 255, 230, 160, 255 });

        // Content: a list box with a vertical scroll bar.
        const ui::Rect box { window.x0 + 20, window.y0 + 40, window.x1 - 20, window.y1 - 90 };
        paint.skin("EditBox_NoFocus", box);
        const auto [aw, ah] = paint.size("ScrollBar_UpArrow");
        const float bar_x = box.x1 - aw - 2;
        paint.natural("ScrollBar_UpArrow", bar_x, box.y0 + 2);
        paint.natural("ScrollBar_DownArrow", bar_x, box.y1 - ah - 2);
        const float track = (box.y1 - ah - 2) - (box.y0 + 2 + ah);
        const float rider_y = box.y0 + 2 + ah + (track * 0.3f) * (1 + std::sin(static_cast<float>(seconds) * 0.8f)) / 2;
        paint.skin("VScrollBar_RiderUp", { bar_x, rider_y, bar_x + aw, rider_y + track * 0.4f });

        // Tick boxes and an edit box below.
        const auto [tw, th] = paint.size("Tickbox_Up");
        const float row_y = window.y1 - 78;
        paint.natural("Tickbox_Up", window.x0 + 20, row_y);
        paint.natural("Tickbox_Up", window.x0 + 28 + tw, row_y);
        paint.natural("Tickbox_Tick", window.x0 + 28 + tw, row_y);
        paint.skin("EditBox_Focus", { window.x0 + 40 + 2 * tw, row_y, window.x1 - 20, row_y + th });

        // Buttons in three tints, the way the game shows button states.
        const auto [gw, gh] = paint.size("GUI2Button");
        constexpr std::array<ui::Color, 3> kTints = { { { 255, 255, 255, 255 }, { 255, 235, 170, 255 }, { 160, 160, 160, 255 } } };
        for (std::size_t i = 0; i < kTints.size(); ++i)
        {
            const float x = window.x0 + 20 + static_cast<float>(i) * (gw + 8);
            paint.natural("GUI2Button", x, window.y1 - 20 - gh, kTints[i]);
        }

        ui::render_draw_list(device, gallery.renderer, list);
    }

    void destroy_skin_gallery(render::Device& device, SkinGallery& gallery)
    {
        for (auto& [name, texture] : gallery.textures)
            device.destroy_texture(texture.handle);
        ui::destroy_ui_renderer(device, gallery.renderer);
        gallery = { };
    }

} // namespace rct3::scene
