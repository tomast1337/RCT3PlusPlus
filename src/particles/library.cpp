// SPDX-License-Identifier: GPL-3.0-or-later
#include "particles/library.hpp"

#include "core/maybe.hpp"
#include "ovl/overlay.hpp"
#include "ovl/particle_skin.hpp"
#include "ovl/texture.hpp"
#include "ovl/texture_pixels.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <set>

namespace rct3::particles
{

    namespace
    {
        constexpr std::string_view kSprites = "Particles/Particles.common.ovl";
        constexpr std::array<std::string_view, 2> kEffectFolders { "ParticleEffects", "Fireworks" };
    } // namespace

    std::size_t Sprite::frame(float age, float pick) const noexcept
    {
        if (frames.size() < 2)
            return 0;
        const float run = animated && loop_time > 0 ? age / loop_time : pick;
        const float share = run - std::floor(run);
        return std::min(static_cast<std::size_t>(share * static_cast<float>(frames.size())), frames.size() - 1);
    }

    std::expected<Library, std::string> Library::load(const std::filesystem::path& root)
    {
        auto overlay = ovl::Overlay::load(root / kSprites);
        if (!overlay)
            return std::unexpected(std::format("{}: {}", kSprites, ovl::to_string(overlay.error())));
        Library library;
        std::set<std::string, std::less<>> wanted;
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
                continue;
            for (const ovl::ParticleSkinItem& item : ovl::read_particle_skins(*overlay, *part))
            {
                if (!item.skin || item.skin->texture.empty())
                    continue;
                wanted.emplace(item.skin->texture);
                library.sprites_.insert_or_assign(std::string(item.name), Sprite { .atlas = std::string(item.skin->texture), .frames = item.skin->frames, .blended = item.skin->blended, .animated = item.skin->animated, .loop_time = item.skin->loop_time });
            }
        }
        const ovl::TexturePixels pixels = ovl::TexturePixels::index(*overlay);
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
                continue;
            for (const ovl::TextureItem& item : ovl::read_textures(*overlay, *part))
                if (item.texture && wanted.contains(item.name) && !library.atlases_.contains(std::string(item.name)))
                    if (auto image = pixels.first_frame(*item.texture))
                        library.atlases_.emplace(std::string(item.name), std::move(*image));
        }
        if (library.sprites_.empty())
            return std::unexpected(std::format("{} holds no particle sprites", kSprites));

        for (const std::string_view folder : kEffectFolders)
        {
            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(root / folder, error))
                if (strings::equals_ignoring_case(entry.path().extension().string(), ".frw"))
                    library.files_.emplace(entry.path().stem().string(), entry.path());
        }
        return library;
    }

    const Effect* Library::effect(std::string_view name)
    {
        auto known = effects_.find(name);
        if (known == effects_.end())
        {
            std::optional<Effect> read;
            if (const auto file = files_.find(name); file != files_.end())
            {
                std::ifstream in(file->second, std::ios::binary);
                const std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                if (auto effect = read_effect(std::as_bytes(std::span(chars))))
                    read = std::move(*effect);
            }
            known = effects_.emplace(std::string(name), std::move(read)).first;
        }
        return known->second ? &*known->second : nullptr;
    }

    const Sprite* Library::sprite(std::string_view name) const
    {
        const auto it = sprites_.find(name);
        return it == sprites_.end() ? nullptr : &it->second;
    }

} // namespace rct3::particles
