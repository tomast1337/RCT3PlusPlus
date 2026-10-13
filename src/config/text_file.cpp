// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/text_file.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

namespace rct3::config
{

    namespace fs = std::filesystem;

    std::expected<std::optional<std::string>, std::string> read_text_file(const fs::path& path)
    {
        std::error_code ec;
        if (!fs::exists(path, ec))
            return std::nullopt;
        std::ifstream in(path, std::ios::binary);
        // Opening a folder succeeds on some systems, so check what it is too.
        if (!in || !fs::is_regular_file(path, ec))
            return std::unexpected("cannot read " + path.string());
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }

    std::expected<void, std::string> write_text_file(const fs::path& path, std::string_view text)
    {
        std::error_code ec;
        if (path.has_parent_path())
            fs::create_directories(path.parent_path(), ec);
        if (ec)
            return std::unexpected("cannot create " + path.parent_path().string() + ": " + ec.message());
        fs::path temporary = path;
        temporary += ".new";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out || !out.write(text.data(), static_cast<std::streamsize>(text.size())))
                return std::unexpected("cannot write " + temporary.string());
        }
        fs::rename(temporary, path, ec);
        if (ec)
            return std::unexpected("cannot replace " + path.string() + ": " + ec.message());
        return { };
    }

} // namespace rct3::config
