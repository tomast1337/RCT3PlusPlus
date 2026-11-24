// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/actions/staff.hpp"

#include "park/person/mesh.hpp"
#include "park/person/outfit.hpp"
#include "random/generator.hpp"
#include "simulation/actions/ids.hpp"
#include "simulation/animals.hpp"
#include "simulation/import_park.hpp"
#include "simulation/park/finance.hpp"
#include "simulation/staff/care.hpp"
#include "simulation/staff/patrol.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <vector>

namespace rct3::simulation::actions
{

    namespace
    {

        using Uniform = std::array<const rct3::park::person::Outfit*, 3>; ///< Head, body and legs

        /// The type a worker of `kind` is saved as.
        save::model::PersonType person_type(save::model::StaffKind kind)
        {
            using enum save::model::StaffKind;
            return kind == Handyman ? save::model::PersonType::Handyman : kind == Mechanic ? save::model::PersonType::Mechanic : kind == SecurityGuard ? save::model::PersonType::SecurityGuard : save::model::PersonType::Entertainer;
        }

        /// The costume the next worker of `kind` hired wears: each entertainer the next in turn, as the game gives
        /// them out.
        u8 next_costume(const World& world, save::model::StaffKind kind)
        {
            std::vector<u8> costumes;
            for (const rct3::park::person::Outfit& o : world.wardrobe)
                if (o.type == person_type(kind) && !std::ranges::contains(costumes, o.costume))
                    costumes.push_back(o.costume);
            std::ranges::sort(costumes);
            return costumes.empty() ? 0 : costumes[world.costumes_given % costumes.size()];
        }

        /// What a worker of `kind` in `costume` wears, missing a part the install does not have.
        Uniform uniform(const World& world, save::model::StaffKind kind, u8 costume)
        {
            Uniform worn { };
            for (const rct3::park::person::Outfit& o : world.wardrobe)
                if (o.type == person_type(kind) && o.costume == costume && !o.swim && o.section < worn.size() && worn[o.section] == nullptr)
                    worn[o.section] = &o;
            return worn;
        }

        /// The walkways a worker hired may start on: not queues, nor where staff are kept off.
        std::vector<NodeId> walkways(const SimWorld& sim)
        {
            std::vector<NodeId> out;
            for (u32 n = 0; n < sim.paths.nodes().size(); ++n)
                if (const PathNode& node = sim.paths.nodes()[n]; !node.removed && !node.queue && !node.staff_kept_off)
                    out.push_back(NodeId { n });
            return out;
        }

        /// The worker `id` names and their walker, null for one not walked.
        struct Worker
        {
            save::model::Staff& saved;
            usize page = 0; ///< Their place in the park's staff
            entt::entity walker = entt::null;
        };

        std::optional<Worker> worker_of(const World& world, save::model::PeopleObjectId id)
        {
            const std::optional<save::model::Staff&> saved = staff_of(world.people, id);
            if (!saved)
                return std::nullopt;
            const auto page = static_cast<usize>(&*saved - world.people.staff.data());
            const entt::entity walker = page < world.staff_walkers.size() && world.sim.registry.valid(world.staff_walkers[page]) ? world.staff_walkers[page] : entt::null;
            return Worker { .saved = *saved, .page = page, .walker = walker };
        }

    } // namespace

    void copy_mood(const Staff& staff, save::model::Staff& saved)
    {
        saved.wage_level = staff.wage_level;
        saved.training = staff.training;
        saved.warnings = staff.warnings;
        saved.happiness = staff.happiness.value;
        saved.laziness = staff.laziness.value;
        if (auto* w = std::get_if<save::model::HandymanWork>(&saved.work))
        {
            w->paths_swept = staff.swept;
            w->bins_emptied = staff.emptied;
            w->gardens_watered = staff.watered;
        }
        else if (auto* m = std::get_if<save::model::MechanicWork>(&saved.work))
        {
            m->objects_fixed = staff.fixed;
            m->objects_inspected = staff.inspected;
        }
        else if (auto* k = std::get_if<save::model::KeeperWork>(&saved.work))
        {
            k->dung_cleared = staff.cleared;
            k->troughs_filled = staff.filled;
        }
    }

    Outcome check(const World& world, const HireStaff& action)
    {
        using enum save::model::StaffKind;
        if (!world.simulating || action.kind == Vendor || action.kind == LifeGuard || action.kind == ParkInspector)
            return std::unexpected(Refusal::Forbidden);
        if (action.kind == AnimalKeeper)
            return world.sim.registry.view<const AnimalHouse>().empty() ? Outcome(std::unexpected(Refusal::NothingToDo)) : Checked { };
        if (std::ranges::contains(uniform(world, action.kind, next_costume(world, action.kind)), nullptr) || walkways(world.sim).empty())
            return std::unexpected(Refusal::NothingToDo);
        return Checked { };
    }

    void apply(World& world, const HireStaff& action)
    {
        auto& r = world.sim.registry;
        if (action.kind == save::model::StaffKind::AnimalKeeper)
        {
            // The house with the fewest keepers, any of those. Their page comes with the keepers' clothes, as the
            // keepers that come with a house are dressed.
            std::map<entt::entity, usize> kept;
            for (const entt::entity h : r.view<const AnimalHouse>())
                kept[h] = 0;
            for (const auto [k, duty] : r.view<const KeeperDuty>().each())
                if (const auto at = kept.find(duty.house); at != kept.end())
                    ++at->second;
            const usize fewest = std::ranges::min(kept | std::views::values);
            std::vector<entt::entity> homes;
            for (const auto& [h, n] : kept)
                if (n == fewest)
                    homes.push_back(h);
            const entt::entity home = homes[random::below(world.sim.gameplay, homes.size())];
            (void)add_keeper(world.sim, home, r.get<const AnimalHouse>(home).troughs);
            return;
        }
        const u8 costume = next_costume(world, action.kind);
        ++world.costumes_given;
        const Uniform worn = uniform(world, action.kind, costume);
        const std::vector<NodeId> ways = walkways(world.sim);
        const entt::entity e = add_staff(world.sim, staff::staff_role(action.kind), ways[random::below(world.sim.gameplay, ways.size())]);
        Staff& worker = r.get<Staff>(e);
        worker.training = staff::hired_training(world.park.cash.has_value());
        i32 number = 0;
        for (const save::model::Staff& s : world.people.staff)
            if (s.kind == action.kind)
                number = std::max(number, s.type_id);
        const glm::vec3 at = r.get<const Pose>(e).position;
        const save::model::PeopleObjectId id = new_person_id(world.people);
        save::model::Staff& kept = world.people.staff.emplace_back();
        world.staff_walkers.push_back(e);
        kept.person.id = id;
        kept.kind = action.kind;
        kept.type_id = number + 1;
        kept.wage_level = worker.wage_level;
        kept.training = worker.training;
        kept.happiness = worker.happiness.value;
        kept.task_flags = worker.tasks;
        kept.person.type = person_type(action.kind);
        kept.person.sub_type = costume;
        kept.person.name = std::format("%ME% {}", kept.type_id); // As the game names them, after their job
        kept.person.start_time = world.park.time;                // Hired today, and their work counted from nothing
        if (action.kind == save::model::StaffKind::Handyman)
            kept.work = save::model::HandymanWork { };
        else if (action.kind == save::model::StaffKind::Mechanic)
            kept.work = save::model::MechanicWork { };
        kept.person.position = { at.x, at.y, at.z };
        save::model::Appearance& a = kept.person.appearance;
        a.head = { worn[0]->id };
        a.body = { worn[1]->id };
        a.legs = { worn[2]->id };
        // The palettes a person is drawn in, skin then clothes; the rest stay to be picked.
        for (usize i = 0; i < rct3::park::person::PersonLook { }.colours.size(); ++i)
            a.palettes[i] = static_cast<u8>(random::below(world.sim.gameplay, rct3::park::person::kColourCells));
        world.changed.hired_staff.push_back(e);
    }

    Outcome check(const World& world, const FireStaff& action)
    {
        return staff_of(world.people, action.worker) ? Outcome(Checked { }) : std::unexpected(Refusal::Missing);
    }

    void apply(World& world, const FireStaff& action)
    {
        const Worker w = *worker_of(world, action.worker);
        if (w.walker != entt::null)
            world.sim.registry.destroy(w.walker);
        world.people.staff.erase(world.people.staff.begin() + static_cast<std::ptrdiff_t>(w.page));
        if (w.page < world.staff_walkers.size())
            world.staff_walkers.erase(world.staff_walkers.begin() + static_cast<std::ptrdiff_t>(w.page));
        world.changed.sacked_staff.push_back(w.page);
    }

    Outcome check(const World& world, const ChangeStaff& action)
    {
        const std::optional<Worker> w = worker_of(world, action.worker);
        if (!w)
            return std::unexpected(Refusal::Missing);
        using enum StaffSetting;
        switch (action.setting)
        {
            case Name:
                return action.text.empty() ? Outcome(std::unexpected(Refusal::NothingToDo)) : Checked { };
            case Warn:
                return w->saved.warnings >= staff::kMostWarnings ? Outcome(std::unexpected(Refusal::NothingToDo)) : Checked { };
            case ClearPatrol:
            case FillPatrol:
            case PatrolZone:
                return w->walker == entt::null ? Outcome(std::unexpected(Refusal::Missing)) : Checked { };
            case Colour:
                return action.pot < w->saved.person.appearance.palettes.size() ? Outcome(Checked { }) : std::unexpected(Refusal::Missing);
            // Training one the park cannot afford is refused as it is booked, which tells the player.
            default:
                return Checked { };
        }
    }

    void apply(World& world, const ChangeStaff& action)
    {
        const Worker w = *worker_of(world, action.worker);
        save::model::Person& person = w.saved.person;
        // Pay, training and warnings change the walking worker, and the saved one shows it; without a walker, the
        // saved one alone.
        Staff loose { .wage_level = w.saved.wage_level, .training = w.saved.training, .warnings = w.saved.warnings, .happiness = { w.saved.happiness }, .laziness = { w.saved.laziness } };
        Staff& care = w.walker != entt::null ? world.sim.registry.get<Staff>(w.walker) : loose;
        using enum StaffSetting;
        switch (action.setting)
        {
            case Name:
                person.name = action.text;
                return;
            case ToConsole:
                person.thoughts_to_console = !person.thoughts_to_console;
                return;
            case Highlight:
                person.highlight = !person.highlight;
                return;
            case Warn:
                staff::warn(care);
                break;
            case Train:
                if (!park::book(world.park, world.model.finance, world.ledger_month, staff::training_cost(care), park::LedgerLine::StaffTraining, park::Charge::IfAffordable))
                    return;
                staff::train(care);
                break;
            case Wage:
                staff::set_wage_level(care, action.value);
                break;
            case Costume:
            {
                person.sub_type = static_cast<u8>(action.value);
                // They change into it, where the install has it.
                Uniform worn { };
                for (const rct3::park::person::Outfit& o : world.wardrobe)
                    if (o.type == save::model::PersonType::Entertainer && o.costume == person.sub_type && o.section < worn.size())
                        worn[o.section] = &o;
                if (std::ranges::contains(worn, nullptr))
                    return;
                person.appearance.head = { worn[0]->id };
                person.appearance.body = { worn[1]->id };
                person.appearance.legs = { worn[2]->id };
                world.changed.dressed_staff.push_back(action.worker);
                return;
            }
            case Task:
                w.saved.task_flags ^= 1 << action.value;
                care.tasks = w.saved.task_flags;
                return;
            case ClearPatrol:
                staff::clear_patrol(world.sim.registry, w.walker);
                return;
            case FillPatrol:
                staff::fill_patrol(world.sim.registry, w.walker);
                return;
            case PatrolZone:
                staff::join_zone(world.sim.registry, w.walker, static_cast<u32>(std::max(action.value, 0)));
                return;
            case Colour:
                person.appearance.palettes[action.pot] = static_cast<u8>(action.value);
                world.changed.dressed_staff.push_back(action.worker);
                return;
        }
        copy_mood(care, w.saved);
    }

} // namespace rct3::simulation::actions
