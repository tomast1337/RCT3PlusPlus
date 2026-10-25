// SPDX-License-Identifier: GPL-3.0-or-later
#include "parkfile/structure_file.hpp"

#include "parkfile/binary.hpp"

#include <format>

namespace rct3::parkfile
{

    using namespace detail;

    std::vector<std::byte> write_structure_file(const save::Structure& structure)
    {
        Writer w;
        write_header(w, 1);
        put_chunk(w, "STRC", 1, structure);
        return w.take();
    }

    std::expected<save::Structure, std::string> read_structure_file(std::span<const std::byte> bytes)
    {
        std::optional<save::Structure> out;
        auto read = read_chunks(bytes, [&](std::string_view tag, std::uint32_t version, Reader& body) -> std::expected<void, std::string> {
            if (tag != "STRC")
            {
                body.slice(body.left()); // A chunk of a newer build is skipped
                return { };
            }
            if (out || version != 1)
                return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
            return get_chunk(tag, body, out.emplace());
        });
        if (!read)
            return std::unexpected(read.error());
        if (!out)
            return std::unexpected("no structure in the file");
        if (out->cols <= 0 || out->rows <= 0 || out->tiles.size() != static_cast<std::size_t>(out->cols) * static_cast<std::size_t>(out->rows))
            return std::unexpected("the structure's tiles do not match its box");
        return std::move(*out);
    }

} // namespace rct3::parkfile
