// SPDX-License-Identifier: GPL-3.0-or-later
//
// A common/unique OVL pair loaded from disk and parsed as one unit.
#pragma once

#include "ovl/ovl.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <vector>

namespace rct3::ovl
{

    /// One file of a pair: its bytes and everything read from them.
    struct Part
    {
        std::vector<std::byte> bytes;
        File file;
        std::vector<Symbol> symbols; ///< Names are views into `bytes` (or the common file's bytes)
        LoaderTable loaders;
        std::uint32_t base = 0; ///< Where this part's address space starts: 0 for common, common's size for unique
    };

    /// A loaded `<name>.common.ovl` with its `<name>.unique.ovl`. Owns all bytes. Movable, not copyable,
    /// because symbol names are views into the owned bytes.
    class Overlay
    {
    public:
        Overlay(const Overlay&) = delete;
        Overlay& operator=(const Overlay&) = delete;
        Overlay(Overlay&&) noexcept = default;
        Overlay& operator=(Overlay&&) noexcept = default;

        /// Loads the pair that `path` belongs to. `path` may name either file (`X.common.ovl` or
        /// `X.unique.ovl`) or the stem (`X`). The common file is required, the unique file is optional.
        [[nodiscard]] static std::expected<Overlay, Error> load(const std::filesystem::path& path);

        /// Same as `load` for bytes already in memory, without any file access.
        [[nodiscard]] static std::expected<Overlay, Error> from_bytes(std::vector<std::byte> common, std::optional<std::vector<std::byte>> unique);

        [[nodiscard]] const Part& common() const noexcept
        {
            return common_;
        }
        [[nodiscard]] const Part* unique() const noexcept
        {
            return unique_ ? &*unique_ : nullptr;
        }

        /// Address space covering both parts. The result refers to this object, so it must not outlive it or
        /// survive a move of it.
        [[nodiscard]] AddressSpace space() const;

    private:
        Overlay() = default;

        Part common_;
        std::optional<Part> unique_;
    };

} // namespace rct3::ovl
