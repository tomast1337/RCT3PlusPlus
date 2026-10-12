// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interface's fonts for one language, a set for each width of interface (see ui/font_sets.hpp), loaded
// from the install when first asked for and kept on the device. Each font gets the fallback font, when
// there is one, for the characters it lacks.
#pragma once

#include "config/game_install.hpp"
#include "render/device.hpp"
#include "ui/font_set.hpp"
#include "ui/truetype_font.hpp"
#include "ui/ui_textures.hpp"

#include <expected>
#include <map>
#include <optional>
#include <string>

namespace rct3::ui
{

    struct InterfaceFonts
    {
        std::string language;
        std::optional<TrueTypeFile> fallback;
        std::map<int, FontSet> sets; ///< By set width, as loaded
        UiTextures pages;            ///< Of every loaded set
    };

    /// The fonts of the set for an interface `view_width` units wide, loading the set first if needed. A
    /// set that cannot be loaded is reported once and left empty, so its text is not drawn.
    [[nodiscard]] const FontSet& fonts_for(render::Device& device, const config::GameInstall& install, InterfaceFonts& fonts, float view_width);

    /// Loads one set: every font of its overlay that fills a role.
    [[nodiscard]] std::expected<FontSet, std::string> load_font_set(render::Device& device, const config::GameInstall& install, InterfaceFonts& fonts, int width);

    void destroy_interface_fonts(render::Device& device, InterfaceFonts& fonts);

} // namespace rct3::ui
