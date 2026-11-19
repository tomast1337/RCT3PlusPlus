// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wild birds' models and their posing. A bird is one frame mesh (see ovl/frame_mesh.hpp) that changes shape
// from key to key of a motion; its picture holds four colourings in a two by two grid, and a bird wears one.
#pragma once

#include "core/types.hpp"
#include "ovl/frame_mesh.hpp"
#include "ovl/texture/pixels.hpp"
#include "park/object/mesh.hpp"
#include "park/terrain_mesh.hpp"
#include "units/units.hpp"

#include <array>
#include <filesystem>
#include <glm/glm.hpp>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::park
{

    enum class BirdMotion : u8
    {
        Swim,
        Fly,
        Glide,
        TakeOff,
        Land,
        Stand,
        Peck,
        TakeOffGround,
        LandGround,
        Count,
    };

    /// Whether `motion` plays round and round, rather than once.
    [[nodiscard]] constexpr bool repeats(BirdMotion motion) noexcept
    {
        using enum BirdMotion;
        return motion == Swim || motion == Fly || motion == Glide || motion == Stand || motion == Peck;
    }

    struct BirdModel
    {
        static constexpr usize kNone = ~usize { 0 };

        ovl::FrameMesh mesh;
        std::array<usize, static_cast<usize>(BirdMotion::Count)> motions { }; ///< Index into `mesh.animations`, `kNone` for none
        ovl::texture::RgbaImage picture;
        std::string picture_key;
    };

    /// Reads the bird in the overlay `overlay` (a stem, or either of its files) whose finest mesh is named
    /// `mesh`, with the picture `picture`. Nothing when either is missing.
    [[nodiscard]] std::optional<BirdModel> read_bird_model(const std::filesystem::path& overlay, std::string_view mesh, std::string_view picture);

    /// How long `motion` takes, 0 when the model has none.
    [[nodiscard]] f32 motion_seconds(const BirdModel& model, BirdMotion motion);

    struct PlacedBird
    {
        const BirdModel* model = nullptr;
        BirdMotion motion = BirdMotion::Swim;
        units::Seconds time { }; ///< Into the motion
        u8 palette = 0;
        glm::mat4 world { 1.0f }; ///< Model to park
    };

    /// The mesh of `birds`, one batch for each picture.
    [[nodiscard]] object::ObjectMesh bird_mesh(std::span<const PlacedBird> birds);

} // namespace rct3::park
