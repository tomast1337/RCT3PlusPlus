// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pictures the ground is painted with. The game's terrain files list terrain types, each naming a
// texture and carrying its index: land surfaces and cliff faces are numbered separately, and only the
// type's own name tells them apart (cliff faces' start with "Cliff_"). The pictures are kept by a key made
// from what they paint and their index, which is how the terrain mesh's batches name them.
//
// Each path style keeps its two ground pictures in an overlay of its own, named after the style, and so
// does each queue style its one recolourable ground picture.
#pragma once

#include "ovl/texture_image.hpp"
#include "park/ground_queues.hpp"
#include "park/terrain_mesh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::park
{

    using GroundImages = std::map<std::string, ovl::RgbaImage, std::less<>>;

    [[nodiscard]] GroundPaint ground_paint(std::string_view terrain_type);
    [[nodiscard]] std::string ground_key(GroundPaint paint, std::uint8_t index);

    /// Adds the pictures of every terrain type in the overlay at `path` to `images`, replacing any with the
    /// same key. Returns how many types had no usable picture.
    [[nodiscard]] std::expected<std::size_t, std::string> add_ground_images(GroundImages& images, const std::filesystem::path& path);

    /// The game's overlays of terrain types, relative to its folder.
    inline constexpr std::array<std::string_view, 2> kGroundFiles = {
        "terrain/RCT3/Terrain_RCT3.common.ovl",
        "terrain/CT/Terrain_CT.common.ovl",
    };

    /// A land surface the ground can be painted with. Each terrain type names the interface picture that
    /// shows it and the string that names it.
    struct GroundSurface
    {
        std::uint8_t index = 0; ///< As the terrain cells store it
        std::string icon;
        std::string help; ///< Strings key
    };

    /// The land surfaces (not cliff faces) of the overlay at `path`, in file order.
    [[nodiscard]] std::expected<std::vector<GroundSurface>, std::string> ground_surfaces(const std::filesystem::path& path);

    /// Where a path style's ground pictures are, relative to the game's folder.
    [[nodiscard]] std::string path_texture_file(std::string_view style);

    /// Adds the two ground pictures of path style `style`, from the overlay at `path`, as the pictures of the
    /// park's style number `index`. Returns how many of the two were missing.
    [[nodiscard]] std::expected<std::size_t, std::string> add_path_images(GroundImages& images, const std::filesystem::path& path, std::string_view style, std::uint8_t index);

    /// Where the path tool's own pictures are, relative to the game's folder, and the arrow it lays over the
    /// ground: drawn pointing towards the picture's bottom edge.
    inline constexpr std::string_view kPathToolFile = "Path/Path.common.ovl";
    inline constexpr std::string_view kPathArrowPicture = "Path_ArrowFreeform";

    /// Adds the picture named `name` from the overlay at `path` to `images` as `key`. Returns false when the
    /// overlay has no such picture.
    [[nodiscard]] std::expected<bool, std::string> add_named_image(GroundImages& images, const std::filesystem::path& path, std::string_view name, std::string_view key);

    /// A part of a picture, in shares of its width and height.
    struct PictureRegion
    {
        float left = 0;
        float top = 0;
        float right = 1;
        float bottom = 1;
    };

    /// A tunnel style of the game's terrain surfaces, and where on its one picture, which the style's portal
    /// pieces share, a tunnel's walls and ceiling are cut out from. Our own picks of plain runs that repeat.
    struct TunnelStyle
    {
        std::string_view name;
        PictureRegion wall;
        PictureRegion ceiling;
    };
    inline constexpr std::array<TunnelStyle, 4> kTunnelStyles { {
        { "Rock", { }, { } },
        { "Concrete", { 0.63f, 0.02f, 0.99f, 0.98f }, { 0.63f, 0.02f, 0.99f, 0.98f } },
        { "Metal", { 0.41f, 0.31f, 0.76f, 0.97f }, { 0.01f, 0.31f, 0.37f, 0.97f } },
        { "Wooden", { 0.17f, 0.0f, 0.47f, 0.6f }, { 0.6f, 0.63f, 1.0f, 1.0f } },
    } };

    /// Where a tunnel style's picture is, relative to the game's folder.
    [[nodiscard]] std::string tunnel_texture_file(std::string_view style);

    struct TunnelPictures
    {
        ovl::RgbaImage wall;
        ovl::RgbaImage ceiling;
    };

    /// The wall and ceiling pictures of `style`, cut out of the overlay at `path`. Nothing when it has no picture.
    [[nodiscard]] std::expected<std::optional<TunnelPictures>, std::string> load_tunnel_pictures(const std::filesystem::path& path, const TunnelStyle& style);

    /// Gives each cliff face in `images` the tunnel pictures of the style among `styles` nearest it in mean
    /// colour, so a tunnel is lined like the ground it goes through.
    void add_tunnel_images(GroundImages& images, std::span<const TunnelPictures> styles);

    /// Where a queue style's ground picture is, relative to the game's folder.
    [[nodiscard]] std::string queue_texture_file(std::string_view style);

    /// Which of the pictures named `names`, from a queue style's overlay, is its ground: each style names it its
    /// own way, and keeps beside it a see-through mask whose name ends in ".alpha". The one named "Ground" and
    /// the style is taken first, else the first that is not a mask. Nothing when all are masks.
    [[nodiscard]] std::optional<std::size_t> queue_picture(std::span<const std::string_view> names, std::string_view style);

    /// Adds the ground picture of `painting`'s queue style, from the overlay at `path`, painted in its colours,
    /// as the picture of the park's painting number `index`. Returns false when the overlay has no such picture.
    [[nodiscard]] std::expected<bool, std::string> add_queue_image(GroundImages& images, const std::filesystem::path& path, const QueuePainting& painting, std::uint8_t index);

} // namespace rct3::park
