// SPDX-License-Identifier: GPL-3.0-or-later
//
// Our career file: binary, little-endian, made of chunks, so later versions can add
// to it and older readers keep what they do not understand.
//
//     "RC3C"  u16 version (1)  u16 flags (0)
//     chunk*: tag (4 ASCII bytes), u32 length, payload, u32 CRC-32 of tag and payload
//
// "HEAD" comes first: the name, the game ("base", "soaked", "wild"), the id of the scenario last picked and
// when the career began. One "SCEN" per scenario: its id, status, last save, when each level was reached and
// the time played. "END " closes the file. Text is a u32 byte count and UTF-8; times are i64 seconds since
// 1970. A chunk may be longer than this version reads, and other chunks are kept as they are to be written
// back. A file that does not check out (wrong magic, a newer version, a bad CRC, a cut-off chunk) is not
// read at all.
#pragma once

#include "career/career.hpp"
#include "config/text_file.hpp"

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::career
{

    /// A chunk this version does not read, kept to be written back.
    struct CareerChunk
    {
        std::array<char, 4> tag { };
        std::vector<std::byte> payload;
    };

    struct CareerData
    {
        Career career;
        std::vector<CareerChunk> unknown;
    };

    enum class CareerFileError : std::uint8_t
    {
        NotACareer,   ///< Wrong magic
        NewerVersion, ///< Written by a later version this one cannot read
        Truncated,    ///< Ends inside a chunk, or before "END "
        BadChecksum,
        Malformed, ///< A chunk this version reads does not hold what it should
    };

    [[nodiscard]] const char* to_string(CareerFileError e) noexcept;

    [[nodiscard]] std::vector<std::byte> write_career(const CareerData& data);
    [[nodiscard]] std::expected<CareerData, CareerFileError> read_career(std::span<const std::byte> bytes);

    /// The file name ending of our career files.
    inline constexpr std::string_view kCareerExtension = ".rct3career";

    struct CareerEntry
    {
        std::filesystem::path path;
        CareerData data;
    };

    /// The careers of game `pack` in `folder`, in the order of their file names. Files that do not read are
    /// reported and left out.
    [[nodiscard]] std::vector<CareerEntry> list_careers(const std::filesystem::path& folder, int pack, std::optional<std::vector<config::FileProblem>&> problems);

    /// The first file name "career_NNNN" in `folder` that neither a career nor its folder of games uses.
    [[nodiscard]] std::filesystem::path free_career_path(const std::filesystem::path& folder);

    /// Writes beside the old file first, so a failed write leaves the old one as it was.
    [[nodiscard]] std::expected<void, std::string> save_career(const std::filesystem::path& path, const CareerData& data);

    /// Deletes the career's file and the folder of the same name beside it where its games are kept.
    [[nodiscard]] std::expected<void, std::string> delete_career(const std::filesystem::path& path);

    /// The game's id for pack `pack` in the file, and back; nothing for one it does not know.
    [[nodiscard]] std::string_view pack_id(int pack);
    [[nodiscard]] std::optional<int> pack_from_id(std::string_view id);

} // namespace rct3::career
