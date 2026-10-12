// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/overlay.hpp"

#include <fstream>
#include <string>
#include <system_error>

namespace rct3::ovl
{

    namespace
    {

        namespace fs = std::filesystem;

        constexpr std::string_view kCommonSuffix = ".common.ovl";
        constexpr std::string_view kUniqueSuffix = ".unique.ovl";

        bool ends_with(const std::string& s, std::string_view suffix)
        {
            return s.size() >= suffix.size() && std::string_view(s).substr(s.size() - suffix.size()) == suffix;
        }

        struct PairPaths
        {
            fs::path common;
            fs::path unique;
        };

        // `X.common.ovl`, `X.unique.ovl` and the bare stem `X` all name the same pair.
        PairPaths pair_paths(const fs::path& path)
        {
            std::string name = path.string();
            if (ends_with(name, kCommonSuffix))
                name.resize(name.size() - kCommonSuffix.size());
            else if (ends_with(name, kUniqueSuffix))
                name.resize(name.size() - kUniqueSuffix.size());
            return { fs::path(name + std::string(kCommonSuffix)), fs::path(name + std::string(kUniqueSuffix)) };
        }

        std::expected<std::vector<std::byte>, Error> read_file(const fs::path& path)
        {
            std::error_code ec;
            const auto size = fs::file_size(path, ec);
            if (ec)
                return std::unexpected(Error::Io);

            std::ifstream in(path, std::ios::binary);
            if (!in)
                return std::unexpected(Error::Io);
            std::vector<std::byte> bytes(size);
            in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
            if (static_cast<std::uintmax_t>(in.gcount()) != size)
                return std::unexpected(Error::Io);
            return bytes;
        }

        // Parses one file. `common` is the already loaded common part when this is the unique file.
        std::expected<Part, Error> make_part(std::vector<std::byte> bytes, std::uint32_t base, const Part* common)
        {
            Part part;
            part.bytes = std::move(bytes);
            part.base = base;

            auto file = parse(part.bytes);
            if (!file)
                return std::unexpected(file.error());
            part.file = std::move(*file);

            // Names usually live in the common file's string table, so both files share one space.
            AddressSpace space;
            if (common)
                space.add(common->file, common->bytes, common->base);
            space.add(part.file, part.bytes, base);

            auto symbols = read_symbols(part.file, part.bytes, space);
            if (!symbols)
                return std::unexpected(symbols.error());
            part.symbols = std::move(*symbols);

            auto loaders = read_loaders(part.file, part.bytes);
            if (!loaders)
                return std::unexpected(loaders.error());
            part.loaders = std::move(*loaders);
            return part;
        }

    } // namespace

    std::expected<Overlay, Error> Overlay::from_bytes(std::vector<std::byte> common, std::optional<std::vector<std::byte>> unique)
    {
        Overlay overlay;

        auto common_part = make_part(std::move(common), 0, nullptr);
        if (!common_part)
            return std::unexpected(common_part.error());
        overlay.common_ = std::move(*common_part);

        if (unique)
        {
            auto unique_part = make_part(std::move(*unique), overlay.common_.file.data_size, &overlay.common_);
            if (!unique_part)
                return std::unexpected(unique_part.error());
            overlay.unique_ = std::move(*unique_part);
        }
        return overlay;
    }

    std::expected<Overlay, Error> Overlay::load(const fs::path& path)
    {
        const PairPaths paths = pair_paths(path);

        std::error_code ec;
        if (!fs::exists(paths.common, ec))
            return std::unexpected(Error::MissingFile);
        auto common = read_file(paths.common);
        if (!common)
            return std::unexpected(common.error());

        std::optional<std::vector<std::byte>> unique;
        if (fs::exists(paths.unique, ec))
        {
            auto bytes = read_file(paths.unique);
            if (!bytes)
                return std::unexpected(bytes.error());
            unique = std::move(*bytes);
        }
        return from_bytes(std::move(*common), std::move(unique));
    }

    AddressSpace Overlay::space() const
    {
        AddressSpace space;
        space.add(common_.file, common_.bytes, common_.base);
        if (unique_)
            space.add(unique_->file, unique_->bytes, unique_->base);
        return space;
    }

} // namespace rct3::ovl
