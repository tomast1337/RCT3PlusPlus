// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/extra_skins.hpp"

#include "logging/log.hpp"
#include "ui/skin_set.hpp"
#include "ui/ui_textures.hpp"

#include <utility>

namespace rct3::ui
{

    std::size_t add_extra_skins(render::Device& device, InterfaceAssets& assets, std::span<const std::string_view> files)
    {
        const auto log = logging::logger("ui");
        std::size_t added = 0;
        for (const std::string_view name : files)
        {
            const auto path = assets.install ? assets.install->find(name) : std::nullopt;
            auto set = path ? load_skin_set(*path) : std::unexpected(std::string("not found"));
            if (!set)
            {
                log.warning("skins {}: {}", name, set.error());
                continue;
            }
            // Only pictures the interface does not have yet are uploaded.
            std::erase_if(set->images, [&](const auto& image) { return assets.textures.contains(image.first); });
            auto textures = upload_textures(device, set->images);
            if (!textures)
            {
                log.warning("skins {}: {}", name, textures.error());
                continue;
            }
            assets.textures.merge(*textures);
            const SkinLibrary library = skin_library(set->skins, assets.textures);
            for (const auto& [skin, _] : set->skins)
            {
                auto ref = library.find(skin);
                if (!ref || assets.skins.find(skin))
                    continue;
                assets.skins.add(skin, *ref);
                ++added;
            }
        }
        return added;
    }

} // namespace rct3::ui
