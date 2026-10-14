// SPDX-License-Identifier: GPL-3.0-or-later
#include "assets/embedded_files.hpp"

#include <algorithm>

namespace rct3::assets
{

    std::optional<std::span<const std::byte>> embedded_file(std::string_view path)
    {
        const std::span<const EmbeddedFile> files = embedded_files();
        const auto it = std::ranges::lower_bound(files, path, { }, &EmbeddedFile::path);
        if (it == files.end() || it->path != path)
            return std::nullopt;
        return it->bytes;
    }

    std::string_view as_text(std::span<const std::byte> bytes)
    {
        return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    }

} // namespace rct3::assets
