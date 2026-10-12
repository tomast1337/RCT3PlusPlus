// SPDX-License-Identifier: GPL-3.0-or-later
//
// Named images uploaded as device textures, with the sizes skins need to turn pixels into texture
// coordinates.
#pragma once

#include "ovl/texture_image.hpp"
#include "render/device.hpp"
#include "ui/skin_library.hpp"
#include "ui/skin_set.hpp"

#include <expected>
#include <map>
#include <string>

namespace rct3::ui
{

    struct UiTexture
    {
        render::TextureHandle handle;
        float width = 0;
        float height = 0;
    };

    using UiTextures = std::map<std::string, UiTexture, std::less<>>;

    /// Uploads every image; on failure, destroys what it uploaded and names the image that failed.
    [[nodiscard]] std::expected<UiTextures, std::string> upload_textures(render::Device& device, const std::map<std::string, ovl::RgbaImage, std::less<>>& images);

    void destroy_textures(render::Device& device, UiTextures& textures);

    /// The skins whose texture is among `textures`, ready for widgets.
    [[nodiscard]] SkinLibrary skin_library(const std::map<std::string, NamedSkin, std::less<>>& skins, const UiTextures& textures);

} // namespace rct3::ui
