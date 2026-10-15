// SPDX-License-Identifier: GPL-3.0-or-later
//
// How people are put together. A person is made of three sections, a head, a body and legs, each picked
// from the sets the game offers for an age group and sex. A section lists its pieces at three levels of
// detail, finest first: some always worn, some optional (a wristband, say), each turned on by a bit of the
// person's choice. A piece is a mesh that moves by whole stored frames and the picture it is painted with,
// plus which of the person's three colours (skin, top, bottom) picks its cell of that picture. A head set
// can also name a head built differently: a skin of rigid parts carried by a few animated bones, used for
// the two finer levels, with a choice of faces.
//
// The same section is listed a second time for swimwear.
#pragma once

#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// Which part of a person a section dresses.
    enum class SectionKind : std::uint8_t
    {
        Head = 0,
        Body = 1,
        Legs = 2,
    };

    /// The pieces of a section at one level of detail.
    struct SectionLevel
    {
        std::vector<std::string_view> fixed;    ///< Pieces ("prt" items) always drawn
        std::vector<std::string_view> optional; ///< Pieces drawn when their bit is set, first piece bit 0
    };

    inline constexpr std::size_t kSectionLevels = 3;

    struct BodySection
    {
        std::uint8_t age_group = 0;          ///< Same numbers as the save's person types: child, teen, adult, staff
        std::uint8_t sex = 0;                ///< 0 male, 1 female
        std::uint8_t kind = 0;               ///< A `SectionKind` value, others kept as read
        std::vector<float> optional_chances; ///< For each optional piece, how likely a new person wears it
        std::array<SectionLevel, kSectionLevels> levels;
        std::string_view head; ///< "phd" item for the finer levels, empty when the pieces are all there is
        std::array<SectionLevel, kSectionLevels> swim_levels;
        std::string_view swim_head;
    };

    struct BodySectionItem
    {
        std::string_view name;
        std::optional<BodySection> section;
    };

    /// Which of the person's colours picks a piece's cell.
    enum class ColourChannel : std::uint8_t
    {
        Skin = 0,
        Top = 1,
        Bottom = 2,
    };

    struct BodyPiece
    {
        std::string_view texture;  ///< "tex" item, empty for pieces that are never painted
        std::string_view mesh;     ///< "mms" item
        std::uint32_t channel = 0; ///< A `ColourChannel` value, others kept as read
    };

    struct BodyPieceItem
    {
        std::string_view name;
        std::optional<BodyPiece> piece;
    };

    /// Faces a head can pull, in the order the head lists its skins for each level.
    inline constexpr std::size_t kHeadFaces = 6;
    inline constexpr std::size_t kHeadLevels = 2;

    struct HeadRig
    {
        std::string_view texture; ///< "tex" item
        std::string_view bones;   ///< "sal" item: the bones and their animations
        /// "ssk" items, level by level (finest first), each level listing its faces, the plain face first.
        std::array<std::string_view, kHeadLevels * kHeadFaces> skins;
    };

    struct HeadRigItem
    {
        std::string_view name;
        std::optional<HeadRig> head;
    };

    /// Items of one part of an overlay tagged "ppg", "prt" and "phd" in turn.
    [[nodiscard]] std::vector<BodySectionItem> read_body_sections(const Overlay& overlay, const Part& part);
    [[nodiscard]] std::vector<BodyPieceItem> read_body_pieces(const Overlay& overlay, const Part& part);
    [[nodiscard]] std::vector<HeadRigItem> read_head_rigs(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
