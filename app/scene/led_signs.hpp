// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's LED signs (park/led_signs.hpp): scrolling lamp boards over the rides' queues and on park
// entrances.
//
// Each line of a ride shown with signs gets its theme's queue sign over its head tile; a ride of no theme, or one
// whose theme has none, takes the plain theme's. Park entrances get their own board written, with the park's
// name or that the park is closed; only the board is drawn again, over the entrance's own. Signs are put up
// again whenever paths, rides, objects or the choice of signs change, never saved with the park's objects.
//
// A board shows its own picture: lamps lit by the words, written in the game's large interface font, moved on as
// the text scrolls. It is drawn unlit, as the game's data asks for the board.
#pragma once

#include "park/led_signs.hpp"
#include "park/object_mesh.hpp"
#include "render/device.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "scene/track_building.hpp"
#include "ui/font.hpp"
#include "ui/interface_assets.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace rct3::scene
{

    struct ParkScene;

    struct LedSign
    {
        std::optional<std::size_t> ride; ///< Of the scene's rides; none for a park entrance
        save::PlacedObject object;
        park::ObjectMesh mesh;                      ///< Its board's batch painted with `board`
        std::string board;                          ///< Key of the board's picture in LedSigns::textures
        std::uint32_t width = 0;                    ///< Of the board's picture, whole lamps
        park::LampColour colour = park::kSignWhite; ///< Of its lamps
        std::u16string shown;
        ovl::RgbaImage loop;
        std::uint32_t shift = UINT32_MAX; ///< Of the loop the board shows; UINT32_MAX before it shows any
    };

    struct LedSigns
    {
        std::vector<LedSign> signs;
        std::set<save::ObjectId> hidden; ///< Rides whose queues have no signs, by id
        std::map<std::string, TrackModels, std::less<>> models;
        std::map<std::string, std::string, std::less<>> sign_of; ///< A theme's queue sign, by its overlay
        std::optional<ui::Font> font;
        std::vector<ovl::RgbaImage> font_pages; ///< Of `font`, by its glyphs' page index
        bool font_tried = false;
        ui::UiTextures textures; ///< The boards' pictures
        bool stale = true;       ///< Put the signs up again at the next update
        /// The counts of paths, rides and objects the signs were put up for.
        /// shortcut: a path raised or changed in place keeps the counts, so its sign stays; upgrade to a change
        /// counter on the paths if signs float.
        std::array<std::size_t, 3> counted { };
    };

    /// Puts the signs up again from the paths and rides as they are now, when they changed; sets each board's
    /// words from its ride. Call once per update.
    void update_led_signs(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets);

    /// Moves the boards' words on. Call inside the screen pass; it ends the pass and begins it again keeping what
    /// was drawn when a board's picture moves.
    void roll_led_signs(render::Device& device, ParkScene& scene);

    void draw_led_signs(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light);

    void destroy_led_signs(render::Device& device, LedSigns& signs);

} // namespace rct3::scene
