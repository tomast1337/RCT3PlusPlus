// SPDX-License-Identifier: GPL-3.0-or-later
#include "parkfile/saved_park.hpp"

#include "parkfile/binary.hpp"
#include "simulation/systems.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <unordered_set>

namespace rct3::parkfile
{

    using namespace detail;

    namespace
    {
        /// Every chunk, by tag and the member that holds it; all at version 1.
        template<typename F> void each_chunk(auto& park, F&& f)
        {
            f("BASE", park.base);
            f("SUMM", park.summary);
            f("TERR", park.grid);
            f("PATH", park.paths);
            f("OBJS", park.objects);
            f("BINS", park.bins);
            f("SIGN", park.signs);
            f("RIDE", park.rides);
            f("PARK", park.progress);
            f("VIEW", park.camera);
        }
        constexpr std::uint32_t kChunks = 10;
    } // namespace

    std::vector<save::PlacedObject> kept_objects(std::span<const save::PlacedObject> objects, std::size_t own, std::span<const save::PathTile> paths, const save::RideModel& rides)
    {
        std::unordered_set<std::uint64_t> built;
        for (const save::PathTile& p : paths)
        {
            if (p.elevation)
                built.insert(p.elevation->scenery_ref);
        }
        for (const save::Ride& ride : rides.rides)
        {
            if (ride.track)
                for (const save::TrackPiece& piece : ride.track->pieces)
                    built.insert(piece.scenery);
            for (const auto& spot : { ride.base.spot, ride.stations.empty() ? std::nullopt : ride.stations.front().entrance_spot, ride.stations.empty() ? std::nullopt : ride.stations.front().exit_spot })
            {
                if (spot)
                    built.insert(spot->object);
            }
        }
        std::vector<save::PlacedObject> kept;
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            if (i < own || built.contains(objects[i].id))
                kept.push_back(objects[i]);
        }
        return kept;
    }

    SavedPeople keep_people(const simulation::SimWorld& world, save::PeopleModel model, const std::unordered_map<entt::entity, std::int32_t>& looks)
    {
        using namespace simulation;
        const entt::registry& r = world.registry;
        SavedPeople out;
        const auto look = [&](entt::entity e, std::size_t count) {
            const auto found = looks.find(e);
            return found != looks.end() && found->second >= 0 && static_cast<std::size_t>(found->second) < count ? found->second : -1;
        };
        std::unordered_map<entt::entity, std::int32_t> parties;
        std::vector<bool> in_park(model.guests.size());
        // shortcut: queueing, riding and errands are not kept, guests walk on from where they stand; keep
        // them once the save can name rides and path nodes that last across loading.
        for (const auto [e, visitor, pose, needs, mood, wallet] : r.view<const Visitor, const Pose, const Needs, const Mood, const Wallet>(entt::exclude<Left>).each())
        {
            // Those off the paths (on a ride) where they were going on them, so they walk again on loading.
            const auto* stride = r.try_get<Stride>(e);
            const bool off = !world.paths.nearest(pose.position) && stride != nullptr && stride->to < world.paths.nodes().size();
            SavedGuest& g = out.guests.emplace_back(SavedGuest { .look = look(e, model.guests.size()), .visitor = visitor, .position = off ? world.paths.centre(stride->to) : pose.position, .heading = pose.heading, .needs = needs, .mood = mood, .cash = wallet.cash });
            if (const auto* taste = r.try_get<Taste>(e))
                g.taste = *taste;
            if (const auto* member = r.try_get<Member>(e); member != nullptr && r.valid(member->party))
            {
                g.party = parties.try_emplace(member->party, static_cast<std::int32_t>(parties.size())).first->second;
                g.leader = r.get<Party>(member->party).leader == e;
            }
            if (g.look >= 0)
                in_park[static_cast<std::size_t>(g.look)] = true;
        }
        for (std::size_t i = 0; i < model.guests.size(); ++i)
            model.guests[i].in_park = in_park[i];
        for (const auto [e, staff, pose] : r.view<const Staff, const Pose>().each())
        {
            SavedWorker& w = out.staff.emplace_back(SavedWorker { .look = look(e, model.staff.size()), .role = staff.role, .wage = staff.wage, .wage_level = staff.wage_level, .training = staff.training, .warnings = staff.warnings, .happiness = staff.happiness, .laziness = staff.laziness, .position = pose.position, .heading = pose.heading });
            if (const auto* patrol = r.try_get<Patrol>(e))
                w.patrol = *patrol;
        }
        out.model = std::move(model);
        return out;
    }

    std::vector<entt::entity> place_guests(simulation::SimWorld& world, const SavedPeople& people)
    {
        using namespace simulation;
        entt::registry& r = world.registry;
        std::vector<entt::entity> out;
        for (const SavedGuest& g : people.guests)
        {
            const entt::entity e = out.emplace_back(r.create());
            r.emplace<Visitor>(e, g.visitor);
            r.emplace<Pose>(e, g.position, g.heading, g.position, g.heading);
            r.emplace<Needs>(e, g.needs);
            r.emplace<Mood>(e, g.mood);
            r.emplace<Wallet>(e, g.cash);
            if (g.taste)
                r.emplace<Taste>(e, *g.taste);
        }
        return out;
    }

    void place_parties(simulation::SimWorld& world, const SavedPeople& people, std::span<const entt::entity> guests)
    {
        using namespace simulation;
        entt::registry& r = world.registry;
        std::map<std::int32_t, Party> parties; // In order, so the entities come out the same each load
        for (std::size_t i = 0; i < people.guests.size() && i < guests.size(); ++i)
        {
            const SavedGuest& g = people.guests[i];
            if (g.party < 0 || !r.all_of<Errand>(guests[i]) || r.all_of<Member>(guests[i]))
                continue;
            Party& party = parties[g.party];
            party.members.push_back(guests[i]);
            if (g.leader)
                party.leader = guests[i];
        }
        for (auto& [_, party] : parties)
        {
            if (party.members.size() < 2)
                continue;
            if (party.leader == entt::null)
                party.leader = party.members.front();
            const entt::entity e = r.create();
            for (const entt::entity m : party.members)
            {
                r.emplace<Member>(m, e);
                if (auto& errand = r.get<Errand>(m); m != party.leader && errand.kind == ErrandKind::Wandering)
                    errand = { ErrandKind::Following, entt::null, 0 };
            }
            r.emplace<Party>(e, std::move(party));
        }
    }

    std::vector<entt::entity> place_staff(simulation::SimWorld& world, const SavedPeople& people)
    {
        using namespace simulation;
        entt::registry& r = world.registry;
        std::vector<entt::entity> out(people.staff.size(), entt::null);
        for (std::size_t i = 0; i < people.staff.size(); ++i)
        {
            const SavedWorker& w = people.staff[i];
            const auto at = world.paths.nearest(w.position);
            if (w.role != StaffRole::Vendor && (!at || world.paths.nodes()[*at].queue))
                continue;
            const entt::entity e = r.create();
            r.emplace<Staff>(e, Staff { .role = w.role, .wage = w.wage, .wage_level = w.wage_level, .training = w.training, .warnings = w.warnings, .happiness = w.happiness, .laziness = w.laziness });
            r.emplace<Pose>(e, w.position, w.heading, w.position, w.heading);
            if (w.patrol)
                r.emplace<Patrol>(e, *w.patrol);
            if (w.role != StaffRole::Vendor) // A vendor stays in their booth
                start_walking(world, e, *at);
            out[i] = e;
        }
        return out;
    }

    std::vector<std::byte> write_saved_park(const SavedPark& park)
    {
        Writer w;
        const bool shows = park.shows && !park.shows->empty();
        write_header(w, kChunks + (park.people ? 1 : 0) + (park.no_queue_signs.empty() ? 0 : 1) + (shows ? 1 : 0) + (park.goals ? 1 : 0));
        each_chunk(park, [&](std::string_view tag, const auto& value) { put_chunk(w, tag, 1, value); });
        if (park.people)
            put_chunk(w, "PEOP", 1, *park.people);
        if (!park.no_queue_signs.empty())
            put_chunk(w, "QSGN", 1, park.no_queue_signs);
        if (shows)
            put_chunk(w, "SHOW", 1, *park.shows);
        if (park.goals)
            put_chunk(w, "GOAL", 1, *park.goals);
        return w.take();
    }

    std::expected<SavedPark, std::string> read_saved_park(std::span<const std::byte> bytes)
    {
        SavedPark park;
        std::uint32_t found = 0;
        auto read = read_chunks(bytes, [&](std::string_view tag, std::uint32_t version, Reader& body) -> std::expected<void, std::string> {
            std::expected<void, std::string> result;
            if (tag == "PEOP")
            {
                if (park.people || version != 1)
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                return get_chunk(tag, body, park.people.emplace());
            }
            if (tag == "QSGN")
            {
                if (!park.no_queue_signs.empty() || version != 1)
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                return get_chunk(tag, body, park.no_queue_signs);
            }
            if (tag == "SHOW")
            {
                if (park.shows || version != 1)
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                auto read = get_chunk(tag, body, park.shows.emplace());
                // A place naming no trick is dropped, so the show's places can be looked up unchecked.
                for (save::RideShow& show : *park.shows)
                    std::erase_if(show.order, [&](std::uint32_t t) { return t >= show.tricks.size(); });
                return read;
            }
            if (tag == "GOAL")
            {
                if (park.goals || version != 1)
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                return get_chunk(tag, body, park.goals.emplace());
            }
            each_chunk(park, [&](std::string_view name, auto& value) {
                if (name != tag)
                    return;
                ++found;
                if (version != 1)
                    result = std::unexpected(std::format("{}: version {} is not one this build reads", tag, version));
                else
                    result = get_chunk(tag, body, value);
            });
            return result;
        });
        if (!read)
            return std::unexpected(read.error());
        if (found != kChunks)
            return std::unexpected("not a saved park: chunks are missing or repeated");
        return park;
    }

    bool is_park_file(std::span<const std::byte> bytes)
    {
        return bytes.size() >= kMagic.size() && std::ranges::equal(bytes.first(kMagic.size()), kMagic, [](std::byte b, char c) { return b == static_cast<std::byte>(c); });
    }

} // namespace rct3::parkfile
