// SPDX-License-Identifier: GPL-3.0-or-later
//
// What every interface screen draws with, loaded from the game install: the skins and their textures, the
// strings of one language and its fonts, and the renderer that draws them.
#pragma once

#include "config/game_install.hpp"
#include "render/device.hpp"
#include "ui/font_set.hpp"
#include "ui/interface_fonts.hpp"
#include "ui/skin_library.hpp"
#include "ui/string_table.hpp"
#include "ui/ui_renderer.hpp"
#include "ui/ui_textures.hpp"

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rct3::ui
{

    struct InterfaceAssets
    {
        UiRenderer renderer;
        UiTextures textures;
        SkinLibrary skins;
        std::optional<config::GameInstall> install; ///< Where font sets come from as the width changes
        InterfaceFonts fonts;
        /// On the heap: windows keep a reference, and the assets move.
        std::unique_ptr<StringTable> strings = std::make_unique<StringTable>();
    };

    /// Loads the skins of the main overlay and of `more_skins` (paths in the install; missing ones are
    /// skipped), the strings of `language` (English's when it has none) with the main overlay's, and the
    /// fallback font. Fonts load as they are asked for (fonts_for).
    [[nodiscard]] std::expected<InterfaceAssets, std::string> load_interface_assets(render::Device& device, const config::GameInstall& install, const std::string& language, const std::filesystem::path& fallback_font, std::span<const std::string_view> more_skins = { });

    /// The fonts for an interface `view_width` units wide.
    [[nodiscard]] const FontSet& fonts_for(render::Device& device, InterfaceAssets& assets, float view_width);

    void destroy_interface_assets(render::Device& device, InterfaceAssets& assets);

} // namespace rct3::ui
