// SPDX-License-Identifier: GPL-3.0-or-later
//
// Files built into the program: everything under the repository's assets/ folder, so the program ships as
// a single file. The build generates the table (see nob.cpp); each file is known by its path below assets/,
// with forward slashes, for example "lang/en-US.json".
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace rct3::assets
{

    struct EmbeddedFile
    {
        std::string_view path;
        std::span<const std::byte> bytes;
    };

    /// Every built-in file, sorted by path.
    [[nodiscard]] std::span<const EmbeddedFile> embedded_files();

    /// The built-in file at `path`, or nothing.
    [[nodiscard]] std::optional<std::span<const std::byte>> embedded_file(std::string_view path);

    /// A file's bytes as text.
    [[nodiscard]] std::string_view as_text(std::span<const std::byte> bytes);

} // namespace rct3::assets
