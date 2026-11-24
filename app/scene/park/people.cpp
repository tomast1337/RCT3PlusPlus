// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/people.hpp"

#include "core/types.hpp"
#include "logging/log.hpp"
#include "park/person/motions.hpp"
#include "scene/park/holding.hpp"
#include "scene/park/patrol.hpp"
#include "scene/park/ride_camera.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "simulation/actions/ids.hpp"
#include "simulation/animals.hpp"
#include "simulation/holding.hpp"
#include "simulation/import_park.hpp"
#include "simulation/name_cheats.hpp"
#include "simulation/park/finance.hpp"
#include "simulation/park/messages.hpp"
#include "simulation/staff/care.hpp"
#include "simulation/staff/patrol.hpp"
#include "simulation/systems.hpp"
#include "strings/unicode.hpp"

#include <algorithm>
#include <map>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

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

    void name_cheats(ParkScene& scene, const ui::InterfaceAssets& assets, const config::game::GameOptions& options, usize shown, std::u16string_view name)
    {
        const simulation::Cheats cheats = options.flag(config::game::GameOption::Cheats) ? simulation::Cheats::Allowed : simulation::Cheats::Off;
        const auto found = simulation::named_cheat(strings::to_utf8(name), cheats, [&](std::string_view key) { return strings::to_utf8(assets.strings->get(key)); });
        if (!found)
            return;
        using enum simulation::NameCheat;
        if (found->cheat != Nothing && found->cheat != NotYet)
            scene.park.cheats_used = true;
        switch (found->cheat)
        {
            case Money:
                simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, units::Money { -simulation::kCheatMoney.value }, simulation::park::LedgerLine::Other, simulation::park::Charge::Always);
                return;
            case GuestCamera:
                ride_person(scene, assets, shown, false);
                return;
            case NotYet:
                logging::logger("park").info("name cheat {} is not in rct3++ yet", found->key);
                break;
            default:
                break;
        }
        (void)simulation::set_off(scene.sim, found->cheat);
        // Those with nothing to see at once say they are on, and those not here yet say so.
        if (const std::string_view said = simulation::set_off_message(found->cheat); !said.empty())
            simulation::park::post(scene.park.messages, { .text_key = std::string(said) });
    }

    bool change_person(ParkScene& scene, usize shown, const ui::screens::info::InfoChange& change)
    {
        using enum ui::screens::info::PersonControl;
        using simulation::actions::StaffSetting;
        if (shown >= scene.shown_people.size())
            return false;
        const ShownPerson& p = scene.shown_people[shown];
        if (p.staff && p.saved < scene.people.staff.size())
        {
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
            // The rest changes the worker once the park takes it in, and the page shows it then.
            std::optional<StaffSetting> setting;
            for (const auto [control, set] :
                 { std::pair { Name, StaffSetting::Name }, std::pair { ToConsole, StaffSetting::ToConsole }, std::pair { Highlight, StaffSetting::Highlight }, std::pair { Warn, StaffSetting::Warn }, std::pair { Train, StaffSetting::Train }, std::pair { Wage, StaffSetting::Wage }, std::pair { Costume, StaffSetting::Costume }, std::pair { PatrolClear, StaffSetting::ClearPatrol }, std::pair { PatrolFill, StaffSetting::FillPatrol }, std::pair { PatrolZone, StaffSetting::PatrolZone } })
                if (change.id == ui::screens::info::person_control(control))
                    setting = set;
            const i32 task = change.id - ui::screens::info::person_control(Task);
            if (!setting && task >= 0)
                setting = StaffSetting::Task;
            if (!setting)
                return false;
            simulation::actions::ask(scene.actions, scene.sim, simulation::actions::ChangeStaff { .worker = scene.people.staff[p.saved].person.id, .setting = *setting, .value = *setting == StaffSetting::Task ? task : change.value, .text = strings::to_utf8(change.text) });
            // Pay, the tasks and the costume are shown by the controls that set them.
            scene.info_after_actions |= *setting != StaffSetting::Wage && *setting != StaffSetting::Task && *setting != StaffSetting::Costume;
            return *setting == StaffSetting::Name; // For its cheats
        }
        if (p.staff || p.saved >= scene.people.guests.size())
            return false;
        save::model::Person& person = scene.people.guests[p.saved].person;
        if (change.id == ui::screens::info::person_control(Name))
        {
            // A blank name keeps the old one.
            if (!change.text.empty())
                person.name = strings::to_utf8(change.text);
            return true;
        }
        if (change.id == ui::screens::info::person_control(ToConsole))
        {
            person.thoughts_to_console = !person.thoughts_to_console;
            return true;
        }
        if (change.id == ui::screens::info::person_control(Highlight))
        {
            person.highlight = !person.highlight;
            return true;
        }
        return false;
    }

    void paint_person(ParkScene& scene, usize shown, usize pot, usize colour)
    {
        const usize palette = ui::screens::info::kUpperPalette + pot;
        if (shown >= scene.shown_people.size() || palette >= rct3::park::person::PersonLook { }.colours.size())
            return;
        ShownPerson& p = scene.shown_people[shown];
        const auto c = static_cast<u8>(colour);
        if (p.staff)
        {
            simulation::actions::ask(scene.actions, scene.sim, simulation::actions::ChangeStaff { .worker = scene.people.staff.at(p.saved).person.id, .setting = simulation::actions::StaffSetting::Colour, .value = c, .pot = palette });
            return;
        }
        change_people(scene);
        p.pose.look.colours.at(palette) = c;
        scene.people.guests.at(p.saved).person.appearance.palettes.at(palette) = c;
    }

    void fire(ParkScene& scene, usize shown)
    {
        // Not the player's doing, but the park's own as it runs, so it is done at once rather than queued.
        simulation::actions::World world = action_world(scene);
        (void)simulation::actions::apply(world, simulation::actions::Action { simulation::actions::FireStaff { .worker = scene.people.staff.at(scene.shown_people.at(shown).saved).person.id } });
        show_changed_people(scene);
    }

    void show_changed_people(ParkScene& scene)
    {
        simulation::actions::Changed& changed = scene.actions.changed;
        if (changed.sacked_staff.empty() && changed.hired_staff.empty() && changed.dressed_staff.empty())
            return;
        change_people(scene);
        const auto shown_page = [&](usize page) { return std::ranges::find_if(scene.shown_people, [&](const ShownPerson& p) { return p.staff && p.saved == page; }); };
        // A worker's clothes and colours as saved; a part the wardrobe does not have stays as it was.
        const auto dress = [&](ShownPerson& shown, const save::model::Appearance& a) {
            rct3::park::person::PersonLook& look = shown.pose.look;
            for (const auto [part, ref] : { std::pair { &look.head, a.head }, std::pair { &look.body, a.body }, std::pair { &look.legs, a.legs } })
                if (const auto worn = std::ranges::find(scene.wardrobe, ref.id, &Outfit::id); worn != scene.wardrobe.end())
                    *part = worn->set;
            for (usize i = 0; i < look.colours.size(); ++i)
                if (a.palettes[i] < kColourCells) // Not one the save leaves to pick
                    look.colours[i] = a.palettes[i];
        };
        for (const usize page : changed.sacked_staff)
        {
            if (const auto gone = shown_page(page); gone != scene.shown_people.end())
            {
                const auto shown = static_cast<usize>(gone - scene.shown_people.begin());
                if (auto info = scene.desktop->root().find_as<ui::screens::info::InfoPanel>(kInfoName); info && scene.shown_person == shown)
                    info->hide();
                scene.shown_people.erase(gone);
                for (std::optional<usize>* followed : { &scene.shown_person, &scene.riding_person })
                {
                    if (*followed == shown)
                        followed->reset();
                    else if (*followed && **followed > shown)
                        --**followed;
                }
            }
            for (ShownPerson& p : scene.shown_people)
                if (p.staff && p.saved > page)
                    --p.saved;
        }
        for (const entt::entity e : changed.hired_staff)
        {
            const auto walker = std::ranges::find(scene.staff_walkers, e);
            if (walker == scene.staff_walkers.end())
                continue; // Sacked since
            const auto page = static_cast<usize>(walker - scene.staff_walkers.begin());
            const save::model::Person& kept = scene.people.staff[page].person;
            ShownPerson& shown = scene.shown_people.emplace_back(ShownPerson { .walking = true, .staff = true, .saved = page, .guest = e });
            dress(shown, kept.appearance);
            shown.pose.motion = rct3::park::person::motion::kWalk;
            shown.pose.world = glm::translate(glm::mat4(1.0f), glm::vec3 { kept.position.x, kept.position.y, kept.position.z });
            scene.known_guests.insert(e);
        }
        // The last worker hired is handed to the pointer at once, to be put down where the player likes, or given
        // back to go to the park's entrance; whoever was held before goes back first.
        if (!changed.hired_staff.empty())
            if (const auto hired = std::ranges::find(scene.shown_people, changed.hired_staff.back(), &ShownPerson::guest); hired != scene.shown_people.end())
            {
                (void)hold_person(scene, { }, { }, Hold::GiveBack);
                if (pick_up_person(scene, static_cast<usize>(hired - scene.shown_people.begin())))
                    scene.holding->from = simulation::entrance_drop(scene.sim).value_or(scene.holding->from);
            }
        for (const save::model::PeopleObjectId id : changed.dressed_staff)
            if (const auto worker = simulation::actions::staff_of(scene.people, id))
                if (const auto shown = shown_page(static_cast<usize>(&*worker - scene.people.staff.data())); shown != scene.shown_people.end())
                    dress(*shown, worker->person.appearance);
        changed.sacked_staff.clear();
        changed.hired_staff.clear();
        changed.dressed_staff.clear();
    }

} // namespace rct3::scene::park
