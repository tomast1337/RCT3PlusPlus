// SPDX-License-Identifier: GPL-3.0-or-later
//
// Real interface skins from the game install, drawn through the UI path: a mock dialog assembled from the
// window frame, background, title buttons, scroll bar, tick boxes, edit box and buttons, centred and sized
// from the window so resizing shows how the skins stretch. A check of the skin data and the draw path until
// real windows exist.
#pragma once

#include "render/device.hpp"
#include "ui/draw_list.hpp"
#include "ui/skin_set.hpp"
#include "ui/ui_renderer.hpp"

#include <array>
#include <expected>
#include <filesystem>
#include <map>
#include <string>

namespace rct3::scene
{

    struct GalleryTexture
    {
        render::TextureHandle handle;
        float width = 0;
        float height = 0;
    };

    struct SkinGallery
    {
        ui::UiRenderer renderer;
        std::map<std::string, ui::NamedSkin, std::less<>> skins;
        std::map<std::string, GalleryTexture, std::less<>> textures;
        ui::DrawList list;
    };

    /// Loads the skins of the overlay at `path` and uploads the images they use.
    [[nodiscard]] std::expected<SkinGallery, std::string> create_skin_gallery(render::Device& device, const std::filesystem::path& path);

    /// Rebuilds the gallery's draw list and records it into the current pass. `seconds` drives the animation.
    void draw_skin_gallery(render::Device& device, SkinGallery& gallery, double seconds);

    void destroy_skin_gallery(render::Device& device, SkinGallery& gallery);

} // namespace rct3::scene
