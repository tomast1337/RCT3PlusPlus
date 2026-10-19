// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game as scripts see it (rct3pp --script FILE.js). A script plays the game as a player would: it moves
// the pointer, presses and lets go, clicks widgets by name, then looks at what happened in the park. Each
// frame the host moves the script on and hands its pointer to the game in place of the real one.
//
// What scripts get, besides the engine's own frames() and log() (scripting/script_engine.hpp); positions are
// shares of the game's view, 0 to 1 from the top-left corner:
//
//   until(check, limit, what)  waits frames until check() holds; throws after `limit` frames (300)
//   pointer.move(x, y), pointer.press(), pointer.release(), pointer.click(x, y), pointer.drag(from, to, frames)
//   ui.find(path)       the middle of the widget at `path` ("tools/Land"), with `on` for a switch, or undefined
//                       when it is not shown
//   ui.click(path)      moves there, presses and lets go, a frame each
//   ui.toggle(path, on) clicks the switch at `path` when it is not already `on`
//   screenshot(name)    saved to the screenshots folder, or as NN_name.png in the report folder when there is
//                       one; resolves once it is written
//   quit(code)          leaves the game with `code` (also when the script ends: 0, or 1 when it failed)
//   park.open(path)     opens the park at `path` in the game's folder and waits until it shows
//   park.loaded()       whether a park is open; park.ready() waits until one is
//   park.cash()         hundredths of the currency, or undefined in a park without money
//   camera.look(at, from)  looks at the map point `at` from `from`, or as now when `from` is left out; map
//                       points are { col, row, up }: tiles, with fractions between, and world units above the
//                       ground
//   map.size()          { width, height } in tiles
//   map.tile(col, row)  { corners, surface, owned }, or undefined off the map
//   map.screen(col, row) the middle of a tile on the view, or undefined when it is not in sight
//   map.paths()         [{ col, row, raised, height }] for each path tile; height only when raised
//   map.water()         [{ level, triangles }] for each lake
//
// With a report folder (rct3pp --script-report DIR), the script's screenshots and log, with how it ended, go
// there for review; ./nob script gathers them into a report.
//
// types/rct3pp.d.ts describes them for TypeScript.
#pragma once

#include "render/device.hpp"
#include "scripting/script_engine.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

namespace rct3::scene
{

    struct ParkScene;

    class ScriptHost
    {
    public:
        /// Reads and starts the script at `file`, reporting into the folder `report` when given; empty with the
        /// reason logged when it cannot be read.
        [[nodiscard]] static std::unique_ptr<ScriptHost> start(const std::filesystem::path& file, std::optional<std::filesystem::path> report);

        /// Moves the script on a frame, seeing `park` (none while no park is open) drawn by `device`.
        void tick(const render::Device& device, ParkScene* park);

        /// The pointer the script holds, in the game view's pixels, and whether its button is down; none
        /// until the script first moves it.
        struct Pointer
        {
            float x = 0;
            float y = 0;
            bool down = false;
        };
        [[nodiscard]] std::optional<Pointer> pointer(const render::Device& device) const;
        /// The park the script asked for, once: a path in the game's folder.
        [[nodiscard]] std::optional<std::string> take_park();
        /// Stops the script, as failed with `why`.
        void fail(const std::string& why);
        /// Once for each screenshot asked for: the file to write it to, or an empty path for the user's
        /// screenshots folder.
        [[nodiscard]] std::optional<std::filesystem::path> take_screenshot();
        /// The exit code once the script asked to quit, finished or failed.
        [[nodiscard]] std::optional<int> quit() const
        {
            return quit_;
        }

    private:
        ScriptHost() = default;
        void define();
        /// To the log, and to the report's when there is one.
        void write(const std::string& line, bool error);

        scripting::ScriptEngine engine_;
        std::optional<glm::vec2> at_; ///< Share of the view
        bool down_ = false;
        std::optional<std::filesystem::path> screenshot_;
        std::optional<std::filesystem::path> report_;
        std::ofstream report_log_;
        int shots_ = 0;
        std::optional<std::string> open_;
        std::optional<int> quit_;
        const render::Device* device_ = nullptr; ///< While a tick runs
        ParkScene* park_ = nullptr;
    };

} // namespace rct3::scene
