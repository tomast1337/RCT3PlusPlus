// SPDX-License-Identifier: GPL-3.0-or-later
//
// The scenery menu's structure tools (park/structure_placement.hpp), in a smaller menu of their own.
//
// Picking out: a drag over the ground marks the tiles it spans, or unmarks them when it starts on a marked
// tile, so several drags make any shape. The panel over the tool row clears the marks or saves the scenery on
// them as a structure, through a save window on the structures folder that asks for its name.
//
// Placing: a load window lists the structures, ours and the original game's. The one picked follows the
// pointer as a see-through ghost, centred on the tile pointed at, on the ground there; the turn keys and the
// panel's button turn it a quarter, and Shift raises and sinks it. A click builds every item if it fits and the
// park can pay what they cost together, and the tool stays out for another. A right click puts it away.
#pragma once

#include "park/object_mesh.hpp"
#include "park/path_clearance.hpp"
#include "park/structure_placement.hpp"
#include "save/model/structure.hpp"
#include "scene/save_browser.hpp"
#include "scene/track_building.hpp"
#include "ui/key_map.hpp"
#include "ui/screens/file_dialog.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::scene
{

    struct ParkScene;

    struct StructureTools
    {
        enum class Tool : std::uint8_t
        {
            None,
            Grab,
            Place,
        };
        Tool tool = Tool::None;
        // Picking out
        std::vector<std::uint8_t> marks;  ///< Of every tile of the park, row after row: 1 where marked
        std::optional<save::Tile> corner; ///< Of the drag going on, where it began
        std::optional<save::Tile> other;  ///< And where it is now
        bool unmarking = false;           ///< The drag began on a marked tile
        bool marks_changed = false;       ///< The marks shown are not the marks
        // Placing
        std::optional<save::Structure> structure; ///< Picked out to save, or being placed
        std::int32_t turns = 0;
        std::optional<save::Tile> at; ///< The tile pointed at
        std::int32_t ground = 0;      ///< The ground's height there
        std::int32_t height = 0;      ///< The lowest item's, the ground's unless raised with Shift
        std::optional<glm::vec3> lift_at;
        std::int32_t lift_height = 0;
        std::vector<save::PlacedObject> placed; ///< The ghost's items
        park::StructureProblem problem = park::StructureProblem::None;
        std::int64_t cost = 0; ///< Of all the items
        bool affordable = true;
        park::ObjectMesh ghost_mesh;
        std::map<std::string, TrackModels, std::less<>> models;
        park::SolidTiles solids;
        std::size_t solids_of = static_cast<std::size_t>(-1);
        // The windows that save and load them
        std::optional<ui::FileDialogMode> asked; ///< A window to open, until the owner opens it
        SaveBrowser browser;
        std::size_t windows_opened = 0;
    };

    /// Takes out the tool, or puts it away when it is out.
    void toggle_structure_tool(ParkScene& scene, StructureTools::Tool tool, const ui::InterfaceAssets& assets);
    /// Puts the tools away.
    void stop_structure_tools(ParkScene& scene);
    /// Starts placing the structure in `file`, ours or the original game's; false when it does not read.
    bool start_structure_placing(ParkScene& scene, const std::filesystem::path& file, const ui::InterfaceAssets& assets);

    /// Each frame, after the area delete tool: the pointer at `at` (of a view `size`, `over` the interface or
    /// not), the left button `down`, `shift` and the game's `commands`, and the panels.
    void update_structure_tools(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over, bool shift, std::span<const ui::PlayCommand> commands);

    /// A click on the park: builds the structure placed. False when no structure is being placed.
    bool structure_tools_click(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets);

    void draw_structure_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light);

} // namespace rct3::scene
