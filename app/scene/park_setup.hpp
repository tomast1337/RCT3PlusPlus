// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loading a park into its scene (scene/park_scene.hpp), and what the scene's other files share from it: the
// names of the windows it puts up, and uploading meshes and pictures.
#pragma once

#include "park/object_mesh.hpp"
#include "render/device.hpp"
#include "ui/ui_textures.hpp"

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::scene
{

    struct ParkScene;

    /// Names of the windows the scene puts up.
    inline constexpr std::string_view kGuideName = "tutorial";
    inline constexpr std::string_view kBarName = "bar";
    inline constexpr std::string_view kToolsName = "tools";
    inline constexpr std::string_view kMessagesName = "messages";
    inline constexpr std::string_view kHeightsName = "heights";
    inline constexpr std::string_view kMoneyName = "money";
    inline constexpr std::string_view kInfoName = "info";
    inline constexpr std::string_view kPlacingName = "placing"; ///< The round panel of the scenery being placed

    template<typename T> std::expected<render::BufferHandle, std::string> upload(render::Device& device, render::BufferUsage usage, const std::vector<T>& items, std::string_view label)
    {
        return device.create_buffer({ usage, std::as_bytes(std::span(items)), label });
    }

    /// The relief map uploaded beside picture `name` (park/relief_maps.hpp), or none.
    render::TextureHandle relief_of(const ui::UiTextures& textures, std::string_view name);

    /// Paints and uploads the pictures of `batches` the scene does not have yet.
    void add_textures(render::Device& device, ParkScene& scene, std::span<const park::ObjectBatch> batches);

} // namespace rct3::scene
