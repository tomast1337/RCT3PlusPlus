// SPDX-License-Identifier: GPL-3.0-or-later
//
// The models of zoo animals, read from the game's animal files (see ovl/animal_model.hpp and
// ovl/animal_motions.hpp for the items).
//
// A park names an animal's species by symbol; the species names a model and a motion set for each of its
// four animals (adult or young, male or female), the motion set names a model animation for each motion,
// and the model's materials name textures. None of these say which file they live in, and several species
// share files (one motion set can serve two big cats), so every file of the animals' folder is indexed up
// front and each name is taken from the file nearest the one asking for it. Everything needed is copied
// out, so the files can be let go once the park's animals are loaded.
#pragma once

#include "math/glm.hpp"
#include "ovl/animal_motions.hpp"
#include "ovl/texture_image.hpp"
#include "park/object_models.hpp"

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

    /// The frames of one motion, 30 a second. A bone neither list names keeps its resting place.
    struct AnimalClip
    {
        float seconds = 0;
        std::uint32_t frames = 0;
        std::vector<std::int32_t> moving { };  ///< Bones whose offsets the frames hold
        std::vector<std::int32_t> turning { }; ///< Bones whose turns the frames hold
        std::vector<glm::vec3> positions { };  ///< frames * moving.size(), frame by frame, from the parent bone
        std::vector<glm::quat> rotations { };  ///< frames * turning.size(), frame by frame, from the parent bone
    };

    inline constexpr float kAnimalFramesPerSecond = 30.0f;

    struct AnimalVertex
    {
        glm::vec3 position { 0.0f }; ///< Model space, the body at rest
        glm::vec3 normal { 0.0f };
        std::array<std::uint8_t, 4> bones { };
        std::array<std::uint8_t, 4> weights { }; ///< Out of 255
        glm::vec2 uv { 0.0f };
    };

    /// One mesh, painted with one picture.
    struct AnimalPiece
    {
        std::string picture { }; ///< Key of its picture, empty for none
        PartBlend blend = PartBlend::Opaque;
        bool one_sided = true;
        std::vector<AnimalVertex> vertices { };
        std::vector<std::uint16_t> indices { };
    };

    /// One animal's look: its bones, its meshes at each level of detail and its motions.
    struct AnimalBody
    {
        std::vector<std::int32_t> parents { };            ///< By bone, -1 for a root; parents come before children
        std::vector<glm::vec3> rest_positions { };        ///< By bone, from the parent
        std::vector<glm::quat> rest_rotations { };        ///< By bone, from the parent
        std::vector<std::vector<AnimalPiece>> levels { }; ///< Finest first; coarse ones may be empty
        /// By motion slot (ovl::AnimalMotion), nothing where the animal has no such motion.
        std::vector<std::optional<AnimalClip>> motions { };
    };

    /// How long motion `motion` of `body` lasts, 0 when it has none.
    [[nodiscard]] float motion_seconds(const AnimalBody& body, ovl::AnimalMotion motion);

    /// By bone: how `time` seconds into `motion` moves the bone and the vertices it carries, from where they
    /// rest to where they are (model space). The motion repeats; a motion the body lacks leaves it at rest.
    [[nodiscard]] std::vector<glm::mat4> animal_pose(const AnimalBody& body, ovl::AnimalMotion motion, float time);

    class AnimalLibrary
    {
    public:
        /// Indexes the animal files in `folders` (as spelled on disk) of the install at `root`.
        AnimalLibrary(const std::filesystem::path& root, std::span<const std::string_view> folders);
        ~AnimalLibrary();
        AnimalLibrary(AnimalLibrary&&) noexcept;
        AnimalLibrary& operator=(AnimalLibrary&&) noexcept;

        /// The body of the animal of species `species` (its symbol without the tag, in any case) of that age
        /// and sex, read the first time it is asked for. A species without that animal gives the other sex,
        /// then any it has. Nothing when no file holds it or it cannot be read.
        [[nodiscard]] std::optional<const AnimalBody&> body(std::string_view species, bool adult, bool male);

        /// The pictures the bodies name, by key, rows as stored.
        [[nodiscard]] const std::map<std::string, ovl::RgbaImage, std::less<>>& pictures() const
        {
            return pictures_;
        }

        /// Lets go of the files read so far. Bodies asked for later are read again from disk.
        void close_files();

    private:
        struct Files;

        std::unique_ptr<Files> files_;
        std::map<std::string, std::optional<AnimalBody>, std::less<>> bodies_; ///< By "species|variant", lower case
        std::map<std::string, ovl::RgbaImage, std::less<>> pictures_;

        std::optional<AnimalBody> read_body(std::string_view species, bool adult, bool male);
    };

} // namespace rct3::park
