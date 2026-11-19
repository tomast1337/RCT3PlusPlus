// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds small parks by hand, to set up a situation and watch what the simulation does with it:
//
//   Sandbox park;
//   park.path({ 0, 0 }, { 9, 0 });
//   const auto stand = park.shop("Drinks", simulation::Product::Drink, { 9, 0 }, 150);
//   const auto guest = park.guest({ 0, 0 }, { .thirst = 0.9f });
//   park.run_until([&] { return park.world().registry.get<Attraction>(stand).customers == 1; }, 60);
//
// Tiles are columns and rows on level 0 unless given a level. Everything runs headless.
#pragma once

#include "core/types.hpp"
#include "simulation/components.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/stalls.hpp"
#include "simulation/workers.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rct3::simulation
{

    struct GuestSetup
    {
        f32 thirst = 0;
        f32 hunger = 0;
        f32 bladder = 0;
        f32 swim = 0;
        f32 happiness = 0.5f;
        f32 energy = 1;
        f32 nausea = 0;
        units::Money cash { 5000 };
        units::Money bank { 10000 }; ///< What cash machines can give them
    };

    struct RideSetup
    {
        u32 capacity = 8;
        f32 cycle_seconds = 30;
        f32 excitement = 0.5f;
        f32 intensity = 0.3f;
        f32 nausea = 0.3f;
        f32 cover = 0;
        std::vector<glm::ivec2> line { };   ///< Queue tiles from the entrance back to the paths; none to walk straight in
        std::optional<glm::ivec2> exit { }; ///< The path tile riders get off onto; the entrance when none
    };

    class Sandbox
    {
    public:
        explicit Sandbox(random::Seed seed = random::Seed { 1 }, u32 threads = 0);

        /// A straight path of tiles from `from` to `to` (same column or same row), both ends included.
        Sandbox& path(glm::ivec2 from, glm::ivec2 to, i32 level = 0);
        /// The same, as one queue line.
        Sandbox& queue(glm::ivec2 from, glm::ivec2 to, i32 level = 0);

        entt::entity shop(std::string name, Product product, glm::ivec2 entrance, i64 price, i32 level = 0);
        entt::entity flat_ride(std::string name, glm::ivec2 entrance, i64 price, const RideSetup& setup = { }, i32 level = 0);
        /// A guest standing on the path tile `at`, wandering.
        entt::entity guest(glm::ivec2 at, const GuestSetup& setup = { }, i32 level = 0);
        /// A bench, sign or bin standing on the path tile `at`; a bin holds `capacity` pieces of rubbish.
        entt::entity item(PathItemKind kind, glm::ivec2 at, f32 capacity = 40, i32 level = 0);
        /// A member of staff on the path tile `at`, walking the paths at their work.
        entt::entity staff(StaffRole role, glm::ivec2 at, i32 level = 0);
        /// `count` pieces of litter lying on the path tile `at`.
        Sandbox& litter(glm::ivec2 at, i32 count = 1, i32 level = 0);
        /// Makes `members` a party led by the first; the others follow.
        entt::entity party(const std::vector<entt::entity>& members);

        void run(u32 ticks);
        void run_seconds(f32 seconds);
        /// Steps until `done` holds or `seconds` have passed; whether it held.
        bool run_until(const std::function<bool()>& done, f32 seconds);

        [[nodiscard]] SimWorld& world()
        {
            return world_;
        }

    private:
        NodeId node(glm::ivec2 tile, i32 level) const;
        Sandbox& lay(glm::ivec2 from, glm::ivec2 to, i32 level, u64 line);

        SimWorld world_;
        std::unique_ptr<Workers> workers_;
        u32 guests_ = 0;
        u64 lines_ = 0;
    };

} // namespace rct3::simulation
