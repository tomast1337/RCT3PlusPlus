// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/sandbox.hpp"

#include "core/maybe.hpp"
#include "simulation/systems.hpp"

#include <format>
#include <stdexcept>

namespace rct3::simulation
{

    Sandbox::Sandbox(random::Seed seed, unsigned threads)
        : workers_(threads > 0 ? std::make_unique<Workers>(threads) : nullptr)
    {
        world_.seed = seed;
    }

    NodeId Sandbox::node(glm::ivec2 tile, int level) const
    {
        const auto found = world_.paths.find({ tile.x, tile.y, level });
        if (!found)
            throw std::invalid_argument(std::format("no path at {} {} level {}", tile.x, tile.y, level));
        return *found;
    }

    Sandbox& Sandbox::path(glm::ivec2 from, glm::ivec2 to, int level)
    {
        return lay(from, to, level, 0);
    }

    Sandbox& Sandbox::queue(glm::ivec2 from, glm::ivec2 to, int level)
    {
        return lay(from, to, level, ++lines_);
    }

    Sandbox& Sandbox::lay(glm::ivec2 from, glm::ivec2 to, int level, std::uint64_t line)
    {
        if (from.x != to.x && from.y != to.y)
            throw std::invalid_argument("a sandbox path runs along a column or a row");
        const glm::ivec2 step { glm::sign(to.x - from.x), glm::sign(to.y - from.y) };
        for (glm::ivec2 at = from;; at += step)
        {
            world_.paths.add({ at.x, at.y, level }, line != 0, std::nullopt, line);
            if (at == to)
                break;
        }
        return *this;
    }

    entt::entity Sandbox::shop(std::string name, Product product, glm::ivec2 entrance, std::int64_t price, int level)
    {
        auto& r = world_.registry;
        const entt::entity e = r.create();
        r.emplace<Attraction>(e, Attraction { .kind = AttractionKind::Shop, .name = std::move(name), .entrance = node(entrance, level), .price = price });
        r.emplace<Shop>(e, product);
        return e;
    }

    entt::entity Sandbox::flat_ride(std::string name, glm::ivec2 entrance, std::int64_t price, const RideSetup& setup, int level)
    {
        auto& r = world_.registry;
        const entt::entity e = r.create();
        std::vector<NodeId> line;
        for (const glm::ivec2 tile : setup.line)
            line.push_back(node(tile, level));
        r.emplace<Attraction>(e, Attraction { .kind = AttractionKind::Ride, .name = std::move(name), .entrance = line.empty() ? node(entrance, level) : line.back(), .price = price });
        r.emplace<CycleRide>(e, CycleRide { .capacity = setup.capacity, .cycle_ticks = ticks(setup.cycle_seconds), .excitement = setup.excitement, .intensity = setup.intensity, .nausea = setup.nausea, .cover = setup.cover, .line = std::move(line), .exit = setup.exit ? node(*setup.exit, level) : kNoNode });
        return e;
    }

    entt::entity Sandbox::item(PathItemKind kind, glm::ivec2 at, float capacity, int level)
    {
        auto& r = world_.registry;
        const entt::entity e = r.create();
        r.emplace<PathItem>(e, PathItem { node(at, level), kind });
        if (kind == PathItemKind::Bin)
            r.emplace<Bin>(e, Bin { capacity, 0 });
        return e;
    }

    entt::entity Sandbox::staff(StaffRole role, glm::ivec2 at, int level)
    {
        const entt::entity e = world_.registry.create();
        world_.registry.emplace<Staff>(e, role);
        start_walking(world_, e, node(at, level));
        return e;
    }

    Sandbox& Sandbox::litter(glm::ivec2 at, int count, int level)
    {
        for (int i = 0; i < count; ++i)
            world_.registry.emplace<Litter>(world_.registry.create(), Litter { node(at, level) });
        return *this;
    }

    entt::entity Sandbox::guest(glm::ivec2 at, const GuestSetup& setup, int level)
    {
        auto& r = world_.registry;
        const NodeId n = node(at, level);
        const entt::entity e = r.create();
        r.emplace<Visitor>(e, AgeGroup::Adult, false, std::format("Guest {}", ++guests_), world_.tick);
        r.emplace<Pose>(e, world_.paths.centre(n), 0.0f);
        r.emplace<Needs>(e, setup.hunger, setup.thirst, setup.bladder);
        r.emplace<Mood>(e, setup.happiness, setup.energy, setup.nausea);
        r.emplace<Wallet>(e, setup.cash);
        start_walking(world_, e, n);
        return e;
    }

    entt::entity Sandbox::party(const std::vector<entt::entity>& members)
    {
        auto& r = world_.registry;
        const entt::entity e = r.create();
        for (const entt::entity m : members)
        {
            r.emplace<Member>(m, e);
            if (m != members.front())
                r.get<Errand>(m) = { ErrandKind::Following, entt::null, 0 };
        }
        r.emplace<Party>(e, members.front(), members);
        return e;
    }

    void Sandbox::run(std::uint32_t count)
    {
        for (std::uint32_t i = 0; i < count; ++i)
            step(world_, maybe(workers_.get()));
    }

    void Sandbox::run_seconds(float seconds)
    {
        run(ticks(seconds));
    }

    bool Sandbox::run_until(const std::function<bool()>& done, float seconds)
    {
        for (std::uint32_t i = 0, n = ticks(seconds); i < n; ++i)
        {
            if (done())
                return true;
            step(world_, maybe(workers_.get()));
        }
        return done();
    }

} // namespace rct3::simulation
