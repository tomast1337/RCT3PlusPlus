// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/ui_textures.hpp"

#include "ui/ui_renderer.hpp"

#include <span>

namespace rct3::ui
{

    std::expected<UiTextures, std::string> upload_textures(render::Device& device, const std::map<std::string, ovl::RgbaImage, std::less<>>& images)
    {
        UiTextures textures;
        for (const auto& [name, image] : images)
        {
            const auto pixels = std::as_bytes(std::span(image.pixels));
            auto texture = device.create_texture({ image.width, image.height, render::TextureFormat::Rgba8, pixels, name });
            if (!texture)
            {
                destroy_textures(device, textures);
                return std::unexpected(name + ": " + texture.error());
            }
            textures.emplace(name, UiTexture { *texture, static_cast<float>(image.width), static_cast<float>(image.height) });
        }
        return textures;
    }

    SkinLibrary skin_library(const std::map<std::string, NamedSkin, std::less<>>& skins, const UiTextures& textures)
    {
        SkinLibrary library;
        for (const auto& [name, skin] : skins)
        {
            const auto texture = textures.find(skin.texture);
            if (texture != textures.end())
                library.add(name, { skin.layout, texture_id(texture->second.handle), texture->second.width, texture->second.height });
        }
        return library;
    }

    void destroy_textures(render::Device& device, UiTextures& textures)
    {
        for (auto& [name, texture] : textures)
            device.destroy_texture(texture.handle);
        textures.clear();
    }

} // namespace rct3::ui
