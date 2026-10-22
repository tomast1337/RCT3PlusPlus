// SPDX-License-Identifier: GPL-3.0-or-later
//
// What particle effects need from the game install: the sprites particles draw (particle skins and the atlas
// pictures they cut from, in the particle overlay) and the effect files, read when first asked for by name.
#pragma once

#include "ovl/texture_image.hpp"
#include "particles/effect.hpp"
#include "strings/ascii.hpp"

#include <array>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::particles
{

    /// A particle's picture: rectangles of an atlas (left, top, right, bottom, 0 to 1).
    struct Sprite
    {
        std::string atlas { };
        std::vector<std::array<float, 4>> frames { };
        bool blended = false; ///< Mixed by opacity; otherwise added
        bool animated = false;
        float loop_time = 1;

        /// The frame to show `age` seconds into a particle's life; `pick` (0 to 1) chooses one of a still
        /// sprite's frames, so particles differ.
        [[nodiscard]] std::size_t frame(float age, float pick) const noexcept;
    };

    class Library
    {
    public:
        /// The sprites and atlases of the install at `root`, and where its effect files are.
        [[nodiscard]] static std::expected<Library, std::string> load(const std::filesystem::path& root);

        /// The effect named `name` (any case, no extension); nothing when there is no such file or it cannot be
        /// read. Read once, then kept: the reference lasts as long as the library.
        [[nodiscard]] const Effect* effect(std::string_view name);
        /// The sprite named `name`, any case.
        [[nodiscard]] const Sprite* sprite(std::string_view name) const;
        [[nodiscard]] const std::map<std::string, ovl::RgbaImage>& atlases() const noexcept
        {
            return atlases_;
        }

    private:
        std::map<std::string, Sprite, strings::LessIgnoringCase> sprites_;
        std::map<std::string, ovl::RgbaImage> atlases_;                                 ///< By the name sprites give
        std::map<std::string, std::filesystem::path, strings::LessIgnoringCase> files_; ///< Effect files by name
        std::map<std::string, std::optional<Effect>, strings::LessIgnoringCase> effects_;
    };

} // namespace rct3::particles
