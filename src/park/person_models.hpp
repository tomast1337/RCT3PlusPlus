// SPDX-License-Identifier: GPL-3.0-or-later
//
// The models of a park's people, read from the game's character files (see ovl/person_parts.hpp for how a
// person is put together).
//
// A saved person names the three sets its head, body and legs come from by symbol. Character files do not
// name the files they take from, so every file of the characters' folder is indexed up front; files are
// only read whole for the sets the park uses. A name used in several files is taken from the one nearest
// the file asking for it. Everything needed is copied out, so the files can be let go once the park's
// people are loaded.
//
// Staff pieces name uniform pictures that are kept outside the characters' folder, with the staff uniforms,
// so the library indexes every folder it is given.
#pragma once

#include "core/maybe.hpp"
#include "ovl/frame_mesh.hpp"
#include "ovl/simple_anim.hpp"
#include "ovl/simple_skin.hpp"
#include "ovl/texture_image.hpp"
#include "park/asset_index.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::park
{

    /// Which two keys of a motion to blend at some time, and how far from the first to the second.
    struct KeyBlend
    {
        std::size_t from = 0;
        std::size_t to = 0;
        float along = 0;

        bool operator==(const KeyBlend&) const = default;
    };

    /// The keys to blend `time` seconds into a motion whose keys after the first come at `times`, the last
    /// time being its length. A repeating motion starts again at its length, blending its last key back into
    /// its first on the way; one that plays once holds its last key from then on.
    [[nodiscard]] KeyBlend blend_keys(std::span<const float> times, float time, bool repeats);

    /// For each motion number, which of a model's motions plays it. Numbers a model has no motion for play its
    /// first one.
    class MotionIndex
    {
    public:
        MotionIndex() = default;
        /// Ties each of `names` (a model's motions, in its order) to the number it is known by.
        explicit MotionIndex(std::span<const std::string_view> names);

        [[nodiscard]] std::size_t operator[](std::int32_t number) const noexcept;

    private:
        std::vector<std::uint16_t> by_number_;
    };

    /// One mesh of a person's section.
    struct PieceModel
    {
        std::string texture;       ///< Key of its picture, empty for none
        std::uint32_t channel = 0; ///< Which of the person's colours picks the picture's cell
        ovl::FrameMesh mesh;       ///< Its motions' names are not kept
        MotionIndex motions;
        bool left_hand = false; ///< A socket for what the left hand holds, its one triangle at the hand; not drawn
    };

    /// A head carried by bones, at its two finer levels.
    struct HeadModel
    {
        std::string texture;
        std::vector<std::int32_t> parents; ///< Of each bone
        std::vector<std::optional<ovl::SimpleAnim>> motions;
        MotionIndex motion_index;
        std::array<std::optional<ovl::SimpleSkin>, 2> skins; ///< The plain face at each level
    };

    /// In a level's optional pieces, one that could not be read.
    inline constexpr std::size_t kNoPiece = static_cast<std::size_t>(-1);

    struct PartLevel
    {
        std::vector<std::size_t> fixed;    ///< Pieces of the library
        std::vector<std::size_t> optional; ///< Each drawn when its bit of the person's choice is set, or kNoPiece
    };

    inline constexpr std::size_t kPersonLevels = 3;

    /// A head, body or legs set.
    struct PartSet
    {
        std::array<PartLevel, kPersonLevels> levels;
        std::optional<std::size_t> head; ///< Of the library, used at the two finer levels
        std::array<PartLevel, kPersonLevels> swim_levels;
        std::optional<std::size_t> swim_head;
    };

    class PersonLibrary
    {
    public:
        /// Indexes the character files in `folders` (as spelled on disk) of the install at `root`.
        PersonLibrary(const std::filesystem::path& root, std::span<const std::string_view> folders);
        ~PersonLibrary();
        PersonLibrary(PersonLibrary&&) noexcept;
        PersonLibrary& operator=(PersonLibrary&&) noexcept;

        /// The set named `symbol` (without its tag, in any case), read the first time it is asked for.
        /// Nothing when no file holds it or it cannot be read.
        [[nodiscard]] std::optional<const PartSet&> part_set(std::string_view symbol);

        [[nodiscard]] const PieceModel& piece(std::size_t index) const
        {
            return pieces_[index];
        }
        [[nodiscard]] const HeadModel& head(std::size_t index) const
        {
            return heads_[index];
        }
        /// The pictures the pieces and heads name, by key, rows as stored.
        [[nodiscard]] const std::map<std::string, ovl::RgbaImage, std::less<>>& pictures() const
        {
            return pictures_;
        }

        /// Lets go of the files read so far. Sets asked for later are read again from disk.
        void close_files();

    private:
        struct Files;

        std::unique_ptr<Files> files_;
        std::vector<PieceModel> pieces_;
        std::vector<HeadModel> heads_;
        std::map<std::string, std::optional<PartSet>, std::less<>> sets_; ///< Lower-case names
        std::map<std::string, std::optional<std::size_t>, std::less<>> loaded_pieces_;
        std::map<std::string, std::optional<std::size_t>, std::less<>> loaded_heads_;
        std::map<std::string, ovl::RgbaImage, std::less<>> pictures_;

        std::optional<PartSet> read_set(std::string_view symbol);
        std::optional<PartLevel> read_level(std::span<const std::string_view> fixed, std::span<const std::string_view> optional, std::string_view near);
        std::optional<std::size_t> read_piece(std::string_view name, std::string_view near);
        std::optional<std::size_t> read_head(std::string_view name, std::string_view near);
        std::string read_picture(std::string_view name, std::string_view near);
    };

} // namespace rct3::park
