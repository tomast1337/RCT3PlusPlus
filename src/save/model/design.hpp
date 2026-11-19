// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a saved ride design (a ".trk" file, the park save's container holding one tracked ride) says about
// itself, for a list of designs to show without laying the ride: the ride kind it is for, the ground it
// covers, its price, its ratings and test results, whether a career scenario has to be completed first, the
// train it suggests and a picture of its track seen from above.
//
// Written, a design holds the ride with its track, the stations' doors and the asset entries they name, and
// only the attributes this side models: a reader that binds attributes by name gives every other one the
// value a new ride has. The header comes first, as the file's root.
//
// The picture is one level per pixel, opaque where the track is and clear around it. It is stored either as
// one byte a pixel (0 clear, else the level out of 255), or packed: a byte from 1 to 63 is one opaque pixel
// of that level out of 63, a byte from 64 up skips that many less 63 clear pixels, rows run on into the next
// at the picture's width, and two zero bytes end it (a zero followed by anything else is skipped with it).
#pragma once

#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "save/save_file.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::save::model
{

    struct DesignSummary
    {
        std::string ride_symbol; ///< Of the ride kind, as a park's rides name it ("Giga:trr")
        std::int32_t width = 0;  ///< Tiles the design covers across
        std::int32_t length = 0; ///< ... and along
        units::Money cost { };
        Assessment assessment;                    ///< Not valid when the design was never rated
        std::optional<TestResults> results;       ///< None when it was never tested
        std::optional<std::int32_t> scenario { }; ///< The career scenario to complete first, by place in the list
        std::int32_t level = 0;                   ///< ... at this level or better: 0 apprentice, 1 entrepreneur, 2 tycoon
        std::int32_t train = 0;                   ///< The train it suggests, by place in its kind's list of trains
        std::uint32_t picture_width = 0;
        std::uint32_t picture_height = 0;
        std::vector<std::uint8_t> picture; ///< RGBA, top row first, grey levels; empty when absent
    };

    enum class DesignError
    {
        NotASave, ///< The container does not read
        NoHeader, ///< A save without a design's header
        NoTrack,  ///< Writing: the ride has no track pieces
        NoAsset,  ///< Writing: a piece or door is not among the objects, or names no asset
    };

    [[nodiscard]] const char* to_string(DesignError e) noexcept;

    /// Reads the container without checking its checksum, as a list of designs does.
    [[nodiscard]] std::expected<DesignSummary, DesignError> read_design_summary(std::span<const std::byte> data);

    /// The picture `width` by `height` from its packed bytes, or from its unpacked ones when there are no
    /// packed; RGBA as DesignSummary::picture. Empty when neither fits the size.
    [[nodiscard]] std::vector<std::uint8_t> design_picture(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> packed, std::span<const std::uint8_t> unpacked);

    /// A design file of `ride`, as it stands among `objects` (the placed objects its pieces, platforms and
    /// doors are), with the header `summary` says: read back, read_design_summary gives `summary` again, its
    /// picture packed from the RGBA one (clear where alpha is 0, else the red level). Piece positions are
    /// written as they stand.
    [[nodiscard]] std::expected<std::vector<std::byte>, DesignError> write_design(const Ride& ride, std::span<const PlacedObject> objects, const DesignSummary& summary);

    /// The packed form of an RGBA picture as DesignSummary::picture holds it, which design_picture unpacks.
    [[nodiscard]] std::vector<std::uint8_t> pack_design_picture(std::span<const std::uint8_t> rgba);

} // namespace rct3::save::model
