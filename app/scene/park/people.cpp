// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/people.hpp"

#include "logging/log.hpp"
#include "park/person/motions.hpp"
#include "scene/park/patrol.hpp"
#include "scene/park/ride_camera.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "simulation/animals.hpp"
#include "simulation/import_park.hpp"
#include "simulation/staff/care.hpp"
#include "simulation/staff/patrol.hpp"
#include "simulation/systems.hpp"
#include "strings/unicode.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace rct3::scene::park
{

    namespace
    {

        /// Lets the people's meshes be changed: the build beside the frame reads them, so it is dropped first,
        /// and all are built again after.
        void change_people(ParkScene& scene)
        {
            if (scene.people_build.valid())
                (void)scene.people_build.get();
            if (scene.moving_build.valid())
                (void)scene.moving_build.get();
            if (scene.riders_build.valid())
                (void)scene.riders_build.get();
            scene.people_levels.clear();
            scene.since_people_levels = kObjectRebuildInterval;
        }

    } // namespace

    void copy_mood(const simulation::Staff& staff, save::model::Staff& saved)
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

    std::optional<std::size_t> hire(ParkScene& scene, save::model::StaffKind kind)
    {
        using enum save::model::StaffKind;
        if (!scene.simulate || kind == Vendor || kind == LifeGuard || kind == ParkInspector)
            return std::nullopt;
        auto& r = scene.sim.registry;
        if (kind == AnimalKeeper)
        {
            const auto houses = r.view<const simulation::AnimalHouse>();
            std::vector<entt::entity> homes(houses.begin(), houses.end());
            if (!homes.empty())
            {
                const entt::entity home = homes[random::below(scene.looks, homes.size())];
                (void)simulation::add_keeper(scene.sim, home, r.get<const simulation::AnimalHouse>(home).troughs);
            }
            return std::nullopt;
        }
        const auto type = kind == Handyman ? save::model::PersonType::Handyman : kind == Mechanic ? save::model::PersonType::Mechanic : kind == SecurityGuard ? save::model::PersonType::SecurityGuard : save::model::PersonType::Entertainer;
        const auto role = simulation::staff::staff_role(kind);
        // Each entertainer the next costume in turn, as the game gives them out.
        std::vector<std::uint8_t> costumes;
        for (const Outfit& o : scene.wardrobe)
            if (o.type == type && !std::ranges::contains(costumes, o.costume))
                costumes.push_back(o.costume);
        std::ranges::sort(costumes);
        const std::uint8_t costume = costumes.empty() ? 0 : costumes[scene.costumes_given++ % costumes.size()];
        std::array<const Outfit*, 3> worn { };
        for (const Outfit& o : scene.wardrobe)
            if (o.type == type && o.costume == costume && !o.swim && o.section < worn.size() && worn[o.section] == nullptr)
                worn[o.section] = &o;
        // On a walkway picked at random, as the game puts them on one of the park's paths.
        std::vector<simulation::NodeId> walkways;
        for (simulation::NodeId n = 0; n < scene.sim.paths.nodes().size(); ++n)
            if (const simulation::PathNode& node = scene.sim.paths.nodes()[n]; !node.removed && !node.queue && !node.staff_kept_off)
                walkways.push_back(n);
        if (std::ranges::contains(worn, nullptr) || walkways.empty())
            return std::nullopt;
        const entt::entity e = simulation::add_staff(scene.sim, role, walkways[random::below(scene.looks, walkways.size())]);
        r.get<simulation::Staff>(e).training = simulation::staff::hired_training(scene.park.cash.has_value());
        scene.known_guests.insert(e);
        change_people(scene);
        std::int32_t number = 0;
        for (const save::model::Staff& s : scene.people.staff)
            if (s.kind == kind)
                number = std::max(number, s.type_id);
        const simulation::Staff& worker = r.get<const simulation::Staff>(e);
        const glm::vec3 at = r.get<const simulation::Pose>(e).position;
        save::model::Staff& kept = scene.people.staff.emplace_back();
        kept.kind = kind;
        kept.type_id = number + 1;
        kept.wage_level = worker.wage_level;
        kept.training = worker.training;
        kept.happiness = worker.happiness.value;
        kept.task_flags = worker.tasks;
        kept.person.type = type;
        kept.person.sub_type = costume;
        kept.person.name = std::format("%ME% {}", kept.type_id); // As the game names them, after their job
        kept.person.start_time = scene.park.time;                // Hired today, and their work counted from nothing
        if (kind == save::model::StaffKind::Handyman)
            kept.work = save::model::HandymanWork { };
        else if (kind == save::model::StaffKind::Mechanic)
            kept.work = save::model::MechanicWork { };
        kept.person.position = { at.x, at.y, at.z };
        save::model::Appearance& a = kept.person.appearance;
        a.head = { worn[0]->id };
        a.body = { worn[1]->id };
        a.legs = { worn[2]->id };
        ShownPerson& shown = scene.shown_people.emplace_back(ShownPerson { .walking = true, .staff = true, .saved = scene.people.staff.size() - 1, .guest = e });
        shown.pose.look.head = worn[0]->set;
        shown.pose.look.body = worn[1]->set;
        shown.pose.look.legs = worn[2]->set;
        for (std::size_t i = 0; i < shown.pose.look.colours.size(); ++i)
            shown.pose.look.colours[i] = a.palettes[i] = static_cast<std::uint8_t>(random::below(scene.looks, kColourCells));
        shown.pose.motion = rct3::park::person::motion::kWalk;
        shown.pose.world = glm::translate(glm::mat4(1.0f), at);
        return scene.shown_people.size() - 1;
    }

    void name_cheats(ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t shown, std::u16string_view name)
    {
        if (strings::equals_ignoring_case(strings::to_utf8(name), strings::to_utf8(assets.strings->get("CheatPeepName_PeepCam"))))
            ride_person(scene, assets, shown, false);
    }

    bool change_person(ParkScene& scene, std::size_t shown, const ui::screens::info::InfoChange& change)
    {
        using enum ui::screens::info::PersonControl;
        if (shown >= scene.shown_people.size())
            return false;
        const ShownPerson& p = scene.shown_people[shown];
        save::model::Staff* staff = p.staff && p.saved < scene.people.staff.size() ? &scene.people.staff[p.saved] : nullptr;
        save::model::Guest* guest = !p.staff && p.saved < scene.people.guests.size() ? &scene.people.guests[p.saved] : nullptr;
        save::model::Person* person = staff != nullptr ? &staff->person : guest != nullptr ? &guest->person : nullptr;
        if (person == nullptr)
            return false;
        if (change.id == ui::screens::info::person_control(Name))
        {
            // A blank name keeps the old one.
            if (change.text.empty())
                return true;
            person->name = strings::to_utf8(change.text);
            return true;
        }
        if (change.id == ui::screens::info::person_control(ToConsole))
        {
            person->thoughts_to_console = !person->thoughts_to_console;
            return true;
        }
        if (change.id == ui::screens::info::person_control(Highlight))
        {
            person->highlight = !person->highlight;
            return true;
        }
        if (staff == nullptr)
            return false;
        // The details page shows the tools for painting while it goes on.
        if (change.id == ui::screens::info::person_control(PatrolPaint))
        {
            scene.painting_patrol = change.value != 0;
            return true;
        }
        if (change.id == ui::screens::info::person_control(PatrolEdit))
        {
            scene.painting_patrol = !scene.painting_patrol;
            return true;
        }
        if (const entt::entity e = shown_staff(scene); e != entt::null && change.id == ui::screens::info::person_control(PatrolClear))
        {
            simulation::staff::clear_patrol(scene.sim.registry, e);
            return true;
        }
        else if (e != entt::null && change.id == ui::screens::info::person_control(PatrolFill))
        {
            simulation::staff::fill_patrol(scene.sim.registry, e);
            return true;
        }
        else if (e != entt::null && change.id == ui::screens::info::person_control(PatrolZone))
        {
            simulation::staff::join_zone(scene.sim.registry, e, static_cast<std::uint32_t>(std::max(change.value, 0)));
            return true;
        }
        if (change.id == ui::screens::info::person_control(PatrolBrush))
        {
            scene.patrol_brush = change.value;
            return false;
        }
        if (change.id == ui::screens::info::person_control(PatrolCopy))
        {
            copy_patrol(scene);
            return true;
        }
        // Pay, training and warnings change the walking worker, and the saved one shows it; without the
        // simulation, the saved one alone.
        const entt::entity worker = shown_staff(scene);
        simulation::Staff loose { .wage_level = staff->wage_level, .training = staff->training, .warnings = staff->warnings, .happiness = staff->happiness, .laziness = staff->laziness };
        simulation::Staff& care = worker != entt::null ? scene.sim.registry.get<simulation::Staff>(worker) : loose;
        const auto show = [&] {
            copy_mood(care, *staff);
            return true;
        };
        if (change.id == ui::screens::info::person_control(Warn))
        {
            if (care.warnings < ui::screens::info::kMostWarnings)
                simulation::staff::warn(care);
            return show();
        }
        if (change.id == ui::screens::info::person_control(Train))
        {
            // Paid for at once, free in a park without money; none when the park cannot afford it.
            if (!simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, simulation::staff::training_cost(care), simulation::park::LedgerLine::StaffTraining, false))
                return false;
            simulation::staff::train(care);
            return show();
        }
        if (change.id == ui::screens::info::person_control(Wage))
        {
            simulation::staff::set_wage_level(care, change.value);
            (void)show();
        }
        else if (change.id == ui::screens::info::person_control(Costume))
        {
            person->sub_type = static_cast<std::uint8_t>(change.value);
            // They change into it, where the install has it.
            std::array<const Outfit*, 3> worn { };
            for (const Outfit& o : scene.wardrobe)
                if (o.type == save::model::PersonType::Entertainer && o.costume == person->sub_type && o.section < worn.size())
                    worn[o.section] = &o;
            if (!std::ranges::contains(worn, nullptr))
            {
                change_people(scene);
                rct3::park::person::PersonLook& look = scene.shown_people[shown].pose.look;
                std::tie(look.head, look.body, look.legs) = std::tuple(worn[0]->set, worn[1]->set, worn[2]->set);
                std::tie(person->appearance.head, person->appearance.body, person->appearance.legs) = std::tuple(save::model::PeopleRef { worn[0]->id }, save::model::PeopleRef { worn[1]->id }, save::model::PeopleRef { worn[2]->id });
            }
        }
        else if (change.id >= ui::screens::info::person_control(Task))
        {
            staff->task_flags ^= 1 << (change.id - ui::screens::info::person_control(Task));
            care.tasks = staff->task_flags;
        }
        return false;
    }

    void paint_person(ParkScene& scene, std::size_t shown, std::size_t pot, std::size_t colour)
    {
        if (shown >= scene.shown_people.size() || ui::screens::info::kUpperPalette + pot >= rct3::park::person::PersonLook { }.colours.size())
            return;
        change_people(scene);
        ShownPerson& p = scene.shown_people[shown];
        const auto c = static_cast<std::uint8_t>(colour);
        p.pose.look.colours.at(ui::screens::info::kUpperPalette + pot) = c;
        save::model::Person* person = p.staff ? &scene.people.staff.at(p.saved).person : &scene.people.guests.at(p.saved).person;
        person->appearance.palettes.at(ui::screens::info::kUpperPalette + pot) = c;
    }

    void fire(ParkScene& scene, std::size_t shown)
    {
        change_people(scene);
        const std::size_t saved = scene.shown_people.at(shown).saved;
        if (const entt::entity walker = scene.shown_people[shown].guest; walker != entt::null)
            scene.sim.registry.destroy(walker);
        scene.shown_people.erase(scene.shown_people.begin() + static_cast<std::ptrdiff_t>(shown));
        scene.people.staff.erase(scene.people.staff.begin() + static_cast<std::ptrdiff_t>(saved));
        for (ShownPerson& p : scene.shown_people)
        {
            if (p.staff && p.saved > saved)
                --p.saved;
        }
        // Those the view and the window follow keep to them; one gone takes them away.
        for (std::optional<std::size_t>* followed : { &scene.shown_person, &scene.riding_person })
        {
            if (*followed == shown)
                followed->reset();
            else if (*followed && **followed > shown)
                --**followed;
        }
    }

} // namespace rct3::scene::park
