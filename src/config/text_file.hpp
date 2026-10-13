// SPDX-License-Identifier: GPL-3.0-or-later
//
// The small text files the program keeps its configuration in: read whole, and written whole beside the
// old one first, so a failed write leaves the old file as it was.
#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace rct3::config
{

    /// Something in a configuration file that could not be used; the rest of the file still applies.
    struct FileProblem
    {
        std::string message;
    };

    /// The file's contents; nothing when there is no file.
    [[nodiscard]] std::expected<std::optional<std::string>, std::string> read_text_file(const std::filesystem::path& path);

    /// Writes `text` to `path`, creating its folder.
    [[nodiscard]] std::expected<void, std::string> write_text_file(const std::filesystem::path& path, std::string_view text);

} // namespace rct3::config
