// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a save says about itself, for a list of saves to show without loading the park: the park's name, the
// date and time in the park, how many guests it has, a small picture of it and the add-on packs it needs.
//
// The game keeps this in its own small object in the save. A file may hold several; one object names the
// save's own, and without it the first describing a park is taken. Fields the file lacks (older files have
// no day, no kind and no packs) keep their defaults.
#pragma once

#include "save/save_file.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace rct3::save
{

    /// The size of the picture in a save's summary.
    inline constexpr std::uint32_t kPreviewWidth = 74;
    inline constexpr std::uint32_t kPreviewHeight = 58;

    struct SaveSummary
    {
        std::string park_name;
        std::int32_t day = 0;     ///< Of the month, from 1; 0 when the file does not say
        std::int32_t month = 0;   ///< From 0, the first month of the park's season
        std::int32_t year = 0;    ///< From 1
        std::int32_t minutes = 0; ///< Since midnight
        std::int32_t guests = 0;
        std::int32_t kind = 0;             ///< 0 for a park; other values describe other things the game saves
        std::vector<std::uint8_t> preview; ///< RGBA, kPreviewWidth by kPreviewHeight, top row first; empty when absent
        std::vector<std::string> packs;    ///< Names of the add-on packs the save needs
    };

    enum class SummaryError
    {
        NotASave,  ///< The container does not read
        NoSummary, ///< A save without a summary
    };

    [[nodiscard]] const char* to_string(SummaryError e) noexcept;

    [[nodiscard]] std::expected<SaveSummary, SummaryError> read_summary(const SaveFile& file);
    /// Reads the container without checking its checksum, as a list of saves does.
    [[nodiscard]] std::expected<SaveSummary, SummaryError> read_summary(std::span<const std::byte> data);

} // namespace rct3::save
