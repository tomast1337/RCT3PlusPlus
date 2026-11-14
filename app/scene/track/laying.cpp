// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/track/laying.hpp"

#include "logging/log.hpp"
#include "ovl/overlay.hpp"
#include "ovl/tracked_ride.hpp"
#include "park/demolish_refund.hpp"
#include "park/design_placement.hpp"
#include "park/flexi_colours.hpp"
#include "park/object/models.hpp"
#include "park/object/placement.hpp"
#include "park/pools/pools.hpp"
#include "park/ride/placement.hpp"
#include "park/support/columns.hpp"
#include "park/track/completion.hpp"
#include "park/track/edit.hpp"
#include "park/track/ground.hpp"
#include "park/track/rules.hpp"
#include "park/track/split.hpp"
#include "park/water/fill.hpp"
#include "profiling/profiler.hpp"
#include "scene/ghosts.hpp"
#include "scene/objects.hpp"
#include "scene/park/footpaths.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/supports.hpp"
#include "scene/park/view.hpp"
#include "scene/ride_placing.hpp"
#include "scene/track/trains.hpp"
#include "simulation/components.hpp"
#include "ui/interface_assets.hpp"
#include "ui/screens/info/attraction.hpp"
#include "ui/screens/info/panel.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <limits>
#include <span>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace rct3::scene::track
{

    namespace
    {
        /// How near a piece's end must come to where the first piece starts to close the track, in park units.
        constexpr float kCloses = 0.1f;
        constexpr std::string_view kSectionTag = ":tks";
    } // namespace

    const rct3::park::track::PieceCatalogue* catalogue(const TrackBuilding& b)
    {
        const auto found = b.catalogues.find(b.overlay);
        return found != b.catalogues.end() ? &found->second : nullptr;
    }

    std::optional<const rct3::park::track::PieceCatalogue&> load_catalogue(TrackBuilding& b, const std::string& overlay, const ui::InterfaceAssets& assets)
    {
        if (const auto found = b.catalogues.find(overlay); found != b.catalogues.end())
            return found->second;
        const auto file = assets.install ? assets.install->find(std::format("{}.common.ovl", slashes(overlay))) : std::nullopt;
        if (!file)
            return std::nullopt;
        auto loaded = rct3::park::track::load_piece_catalogue(*file, overlay);
        if (!loaded)
        {
            logging::logger("park").warning("track pieces of {}: {}", overlay, loaded.error());
            return std::nullopt;
        }
        return b.catalogues.emplace(overlay, std::move(*loaded)).first->second;
    }

    std::unordered_map<std::uint64_t, std::size_t> object_indices(const park::ParkScene& scene)
    {
        std::unordered_map<std::uint64_t, std::size_t> out;
        for (std::size_t i = 0; i < scene.objects.size(); ++i)
            out.emplace(scene.objects[i].id, i);
        return out;
    }

    bool opening(const save::model::Ride& ride)
    {
        return ride.track && (ride.track->pieces.empty() || (!ride.track->segments.empty() && ride.track->segments.back().piece_count == 0));
    }

    namespace
    {

        /// The first piece of the stretch being built, where laying round to it closes it.
        const save::model::TrackPiece& stretch_start(const save::model::Track& track)
        {
            return track.pieces.at(track.segments.empty() ? 0 : track.segments.back().first_piece);
        }

    } // namespace

    std::optional<rct3::park::track::TrackTip> track_tip(const park::ParkScene& scene, const TrackBuilding& b)
    {
        const rct3::park::track::PieceCatalogue* c = catalogue(b);
        if (c == nullptr || !b.ride)
            return std::nullopt;
        const save::model::Ride& ride = scene.rides.rides.at(*b.ride);
        if (!ride.track || ride.track->pieces.empty() || ride.track->circuit)
            return std::nullopt;
        const save::model::TrackPiece& last = ride.track->pieces.back();
        const auto objects = object_indices(scene);
        const auto object = objects.find(last.scenery);
        auto shape = rct3::park::track::shape_named(*c, last.symbol);
        if (object == objects.end() || !shape)
            return std::nullopt;
        return rct3::park::track::tip_of(scene.objects[object->second], *shape, last.reversed, scene.grid);
    }

    namespace
    {

        /// The catalogue's piece the ride's track ends with, laid as it is.
        std::optional<rct3::park::track::LaidPiece> last_piece(const park::ParkScene& scene, const TrackBuilding& b)
        {
            const rct3::park::track::PieceCatalogue* c = catalogue(b);
            const save::model::Ride& ride = scene.rides.rides.at(*b.ride);
            if (c == nullptr || !ride.track || ride.track->pieces.empty())
                return std::nullopt;
            const save::model::TrackPiece& last = ride.track->pieces.back();
            const auto shape = rct3::park::track::shape_named(*c, last.symbol);
            if (!shape)
                return std::nullopt;
            return rct3::park::track::LaidPiece { static_cast<std::size_t>(&*shape - c->pieces.data()), last.reversed };
        }

        /// The colours a new track starts in, as its kind's file sets them.
        save::model::RideColours starting_colours(const park::ParkScene& scene, const TrackBuilding& b)
        {
            const rct3::park::track::PieceCatalogue* c = catalogue(b);
            const rct3::park::track::TrackRules rules = *b.ride < scene.track_rules.size() ? scene.track_rules[*b.ride] : rct3::park::track::TrackRules { };
            return c != nullptr ? rct3::park::track::starting_colours(rules, c->pieces) : save::model::RideColours { };
        }

    } // namespace

    save::model::RideColours next_colours(const park::ParkScene& scene, const TrackBuilding& b)
    {
        const save::model::Ride& ride = scene.rides.rides.at(*b.ride);
        return b.colours.value_or(ride.track && !ride.track->pieces.empty() ? ride.track->pieces.back().colours : starting_colours(scene, b));
    }

    bool has_platform(const rct3::park::track::PieceShape& shape)
    {
        return (shape.record.flags & rct3::park::track::kPieceStation) != 0 || shape.record.role == rct3::park::track::kRoleStation;
    }

    std::optional<Platforms> ride_platforms(TrackBuilding& b, const save::model::Ride& ride, const ui::InterfaceAssets& assets)
    {
        if (ride.platform_type.empty() || !assets.install)
            return std::nullopt;
        Platforms p { .overlay = std::format("Tracks/Platforms/{}/{}", ride.platform_style, ride.platform_type), .names = { }, .doors = { } };
        auto found = b.platform_names.find(p.overlay);
        if (found == b.platform_names.end())
        {
            const auto file = assets.install->find(std::format("{}.common.ovl", p.overlay));
            if (!file)
                return std::nullopt;
            const auto loaded = ovl::Overlay::load(*file);
            if (!loaded)
                return std::nullopt;
            // By name in any case: types are not spelt as their pieces.
            std::vector<std::string> names;
            for (const auto part : { core::maybe(&loaded->common()), loaded->unique() })
                for (const auto& symbol : part ? part->symbols : std::vector<ovl::Symbol> { })
                    if (const ovl::SymbolName n = ovl::split_symbol_name(symbol.name); n.tag == "sid")
                        names.emplace_back(n.name);
            found = b.platform_names.emplace(p.overlay, std::move(names)).first;
        }
        p.names = found->second;
        for (const save::model::Station& s : ride.stations)
            for (const auto& spot : { s.entrance_spot, s.exit_spot })
                if (spot)
                    p.doors.push_back(rct3::park::ride::DoorSpot::of(*spot));
        return p;
    }

    rct3::park::ride::PlatformPlace platform_place(const rct3::park::track::PieceShape& shape, const rct3::park::ride::PlatformPlace& neighbours)
    {
        // A piece whose platform stands alone takes no part in a longer station.
        const bool alone = (shape.record.flags & rct3::park::track::kPiecePlatformAlone) != 0;
        return { .station_before = !alone && neighbours.station_before, .station_after = !alone && neighbours.station_after, .track_before = neighbours.track_before, .track_after = neighbours.track_after, .run = shape.record.platform_run, .side = shape.record.side };
    }

    std::optional<rct3::park::track::LaidPiece> laid_piece(const rct3::park::track::PieceCatalogue& c, const save::model::TrackPiece& piece)
    {
        const auto shape = rct3::park::track::shape_named(c, piece.symbol);
        return shape ? std::optional(rct3::park::track::LaidPiece { static_cast<std::size_t>(&*shape - c.pieces.data()), piece.reversed }) : std::nullopt;
    }

    namespace
    {

        /// A track piece as built: its object, of the scene's, and the catalogue's piece it is.
        struct BuiltPiece
        {
            std::size_t object = 0;
            rct3::park::track::LaidPiece laid;
        };

        std::optional<BuiltPiece> built_piece(const park::ParkScene& scene, const rct3::park::track::PieceCatalogue& c, const save::model::TrackPiece& piece)
        {
            const auto object = std::ranges::find(scene.objects, piece.scenery, &save::model::PlacedObject::id);
            const auto laid = laid_piece(c, piece);
            if (object == scene.objects.end() || !laid)
                return std::nullopt;
            return BuiltPiece { static_cast<std::size_t>(object - scene.objects.begin()), *laid };
        }

        /// Adds to ride `ride`'s running cost what building `cost` of its track adds (taking it away when below
        /// nought), on the ride and on the attraction the park runs for it.
        void add_running_cost(park::ParkScene& scene, save::model::Ride& ride, units::Money cost)
        {
            ride.base.running_cost = std::max<std::int64_t>(ride.base.running_cost + simulation::park::running_cost(cost, true).value, 0);
            for (const auto [e, a] : scene.sim.registry.view<simulation::Attraction>().each())
                if (a.saved == ride.base.id)
                    a.running_cost.value = ride.base.running_cost;
        }

        /// Pays back what piece `piece` of ride `ride`, standing as object `object`, cost to build, as pulling the
        /// whole ride down would, floating it up from where the piece starts. The ride costs that much less to run.
        void refund_piece(park::ParkScene& scene, const rct3::park::track::PieceCatalogue& c, save::model::Ride& ride, const save::model::TrackPiece& piece, const save::model::PlacedObject& object)
        {
            const auto shape = rct3::park::track::shape_named(c, piece.symbol);
            if (!shape)
                return;
            const units::Money cost { shape->record.cost };
            add_running_cost(scene, ride, -cost);
            const units::Money back = rct3::park::demolish_refund(cost, (ride.base.flags & save::model::AttractionCommon::kNeverOpened) == 0);
            simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, -back, simulation::park::LedgerLine::RideConstruction, simulation::park::Charge::IfAffordable);
            park::float_money(scene, rct3::park::track::start_of(object, *shape, piece.reversed, scene.grid).position, back);
        }

    } // namespace

    void hide_platform(park::ParkScene& scene, const save::model::TrackPiece& piece)
    {
        for (save::model::PlacedObject& o : scene.objects)
            if (piece.platform && o.owner_ref == piece.platform->id && o.id != piece.platform->id)
                o.hidden = true;
    }

    void use_split(park::ParkScene& scene, TrackBuilding& b, rct3::park::track::SplitState state)
    {
        b.split = state;
        const rct3::park::track::PieceCatalogue* c = catalogue(b);
        save::model::Ride& ride = scene.rides.rides.at(*b.ride);
        if (c == nullptr || !ride.track || state == rct3::park::track::SplitState::None)
            return;
        const auto& segments = ride.track->segments;
        for (std::size_t s = segments.size(); s-- > 0;)
        {
            const auto end = segments[s].closed || segments[s].piece_count == 0 ? std::nullopt : rct3::park::track::shape_named(*c, ride.track->pieces.at(segments[s].first_piece + segments[s].piece_count - 1).symbol);
            if (end && end->record.side == rct3::park::track::split_side(state))
            {
                rct3::park::track::make_last(*ride.track, s);
                return;
            }
        }
    }

    std::array<bool, 2> split_closed(const park::ParkScene& scene, const TrackBuilding& b)
    {
        std::array<bool, 2> closed { }, open { };
        const rct3::park::track::PieceCatalogue* c = catalogue(b);
        const save::model::Ride& ride = scene.rides.rides.at(*b.ride);
        for (const save::model::TrackSegment& s : c != nullptr && ride.track ? ride.track->segments : std::vector<save::model::TrackSegment> { })
        {
            const auto first = s.piece_count == 0 ? std::nullopt : rct3::park::track::shape_named(*c, ride.track->pieces.at(s.first_piece).symbol);
            if (first && (first->record.side == 1 || first->record.side == 2))
                (s.closed ? closed : open).at(first->record.side - 1) = true;
        }
        return { closed[0] && !open[0], closed[1] && !open[1] };
    }

    save::model::PlacedObject moved_door(save::model::PlacedObject o, const save::model::TrackPiece& from, const save::model::TrackPiece& to)
    {
        const int turns = (to.direction - from.direction + 4) % 4;
        std::int32_t across = o.col - from.tile_x;
        std::int32_t along = o.row - from.tile_z;
        for (int t = 0; t < turns; ++t)
            std::tie(across, along) = std::pair { along, -across };
        o.col = to.tile_x + across;
        o.row = to.tile_z + along;
        o.rotation = (o.rotation + turns) % 4;
        o.base_height = o.base_height.value_or(0) - from.height + to.height;
        o.force_absolute_height = true;
        return o;
    }

    std::optional<rct3::park::track::PiecePlacement> design_piece(const rct3::park::track::PieceCatalogue& c, std::span<const save::model::TrackPiece> design, std::size_t i, const save::model::PlacedObject& to, const save::model::TerrainGrid& grid)
    {
        const auto first = rct3::park::track::shape_named(c, design.front().symbol);
        const auto shape = rct3::park::track::shape_named(c, design[i].symbol);
        if (!first || !shape)
            return std::nullopt;
        const auto saved = [](const save::model::TrackPiece& p) { return save::model::PlacedObject { .col = p.tile_x, .row = p.tile_z, .base_height = p.height, .rotation = p.direction, .force_absolute_height = true }; };
        rct3::park::track::PiecePlacement placed { .object = rct3::park::object::moved_with(saved(design[i]), shape->record.squares, saved(design.front()), first->record.squares, to) };
        placed.object.symbol = shape->record.item;
        placed.object.overlay = to.overlay;
        placed.next = rct3::park::track::tip_of(placed.object, *shape, design[i].reversed, grid);
        return placed;
    }

    namespace
    {

        /// Whether `o`, a piece of the ride being built, goes only on water and has dry land under it. Pieces of
        /// other rides may run under water, as rides that splash through it need.
        bool off_water(park::ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets, save::model::PlacedObject o)
        {
            o.overlay = b.overlay;
            if (!load_object_model(scene, b.models, assets, o))
                return false;
            const auto model = scene.models.find(rct3::park::object::model_ref(o));
            if (model == scene.models.end() || !model->second.water_only)
                return false;
            const rct3::park::support::SupportFootprint shape { .x_squares = model->second.squares[0], .z_squares = model->second.squares[1] };
            return !std::ranges::all_of(rct3::park::support::footprint_tiles(o, shape), [&](const auto& t) { return rct3::park::water::holds_water(scene.footpaths.water.bodies, { t.first, t.second }); });
        }

        /// The room the park's solid items take, but the pieces a piece `b` builds would join: the track's ends, or
        /// the piece built over and those either side of it. Joined pieces may share a tile, as diagonals do.
        /// shortcut: the track's far end is left out even where the new piece does not join it, so a piece may cross
        /// it; leave it in once a piece's ends tell which pieces it meets.
        rct3::park::path::SolidTiles taken_room(const park::ParkScene& scene, const TrackBuilding& b)
        {
            const save::model::Ride& ride = scene.rides.rides.at(*b.ride);
            if (!ride.track || ride.track->pieces.empty())
                return rct3::park::path::solid_tiles(scene.objects, scene.models, scene.grid);
            const auto& pieces = ride.track->pieces;
            std::vector<std::uint64_t> joined { pieces.front().scenery, pieces.back().scenery };
            if (b.selected)
            {
                joined.push_back(pieces.at(*b.selected).scenery);
                if (const auto around = rct3::park::track::piece_neighbours(*ride.track, *b.selected))
                    joined.insert(joined.end(), { pieces[around->before].scenery, pieces[around->after].scenery });
            }
            std::vector<save::model::PlacedObject> others;
            std::ranges::copy_if(scene.objects, std::back_inserter(others), [&](const save::model::PlacedObject& o) { return !std::ranges::contains(joined, o.id); });
            return rct3::park::path::solid_tiles(others, scene.models, scene.grid);
        }

        /// Whether the ground `pieces` (their models loaded) would cut may not be cut.
        bool cut_refused(const park::ParkScene& scene, std::span<const save::model::PlacedObject> pieces)
        {
            const std::vector<rct3::park::ground::GroundCut> cuts = rct3::park::track::ground_cuts(pieces, scene.models, scene.grid, false).footprint;
            return !cuts.empty() && rct3::park::ground::cuts_refused(cuts, scene.grid, rct3::park::ground::ground_holds(scene.objects, scene.models, scene.footpaths.paths, scene.grid), scene.footpaths.water.bodies);
        }

    } // namespace

    bool clashes(const park::ParkScene& scene, const rct3::park::path::SolidTiles& taken, const save::model::PlacedObject& o)
    {
        return rct3::park::path::first_refused(std::span(&o, 1), scene.models, taken, scene.footpaths.paths, scene.grid).has_value() || cut_refused(scene, std::span(&o, 1));
    }

    void refresh(park::ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets)
    {
        const profiling::Scope zone("Ghost");
        b.test.reset(); // A test runs the track as it was
        const rct3::park::track::PieceCatalogue* c = catalogue(b);
        b.candidates.clear();
        b.picked.reset();
        b.ghost.reset();
        b.ghost_mesh = { };
        b.off_water = false;
        b.clashes = false;
        b.unowned = false;
        b.in_ground = false;
        b.ghost_doors = 0;
        b.ghost_platform.clear();
        const save::model::Ride& ride = scene.rides.rides.at(*b.ride);
        const bool starting = opening(ride);
        b.backward = false;
        const rct3::park::track::TrackRules rules = *b.ride < scene.track_rules.size() ? scene.track_rules[*b.ride] : rct3::park::track::TrackRules { };
        if (!starting)
            b.tip = track_tip(scene, b);
        // Over a piece picked between two others, what fits both, laid on from the end of the one before.
        std::optional<std::vector<rct3::park::track::LaidPiece>> between;
        if (const auto around = b.selected && c != nullptr && !starting ? rct3::park::track::piece_neighbours(*ride.track, *b.selected) : std::nullopt)
        {
            const auto before = built_piece(scene, *c, ride.track->pieces[around->before]);
            const auto after = built_piece(scene, *c, ride.track->pieces[around->after]);
            b.tip.reset();
            if (before && after)
            {
                b.tip = rct3::park::track::tip_of(scene.objects[before->object], c->pieces[before->laid.index], before->laid.reversed, scene.grid);
                const rct3::park::track::TrackTip goal = rct3::park::track::start_of(scene.objects[after->object], c->pieces[after->laid.index], after->laid.reversed, scene.grid);
                between = rct3::park::track::pieces_between(c->pieces, *b.tip, before->laid, goal, after->laid, scene.grid);
            }
        }
        else
            b.selected.reset();
        if (c == nullptr || (starting ? !b.aim : !b.tip) || (!starting && rules.one_piece))
            return;
        b.candidates = between ? std::move(*between) : starting ? rct3::park::track::starting_pieces(c->pieces) : rct3::park::track::pieces_after(c->pieces, b.tip->end, last_piece(scene, b));
        // A ride built back from its station goes on from where its first piece starts: pieces laid backwards
        // from there, then kept in the track's own direction.
        // shortcut: the way follows the ride's file, never the player, upgrade with a forward and backward button.
        if (!starting && rules.builds_back && !b.selected)
        {
            b.candidates.clear();
            b.backward = true;
            const save::model::TrackPiece& first = ride.track->pieces.front();
            const auto objects = object_indices(scene);
            const auto object = objects.find(first.scenery);
            const auto shape = rct3::park::track::shape_named(*c, first.symbol);
            if (object != objects.end() && shape)
            {
                // The first piece laid backwards, and its far end.
                const rct3::park::track::LaidPiece turned { static_cast<std::size_t>(&*shape - c->pieces.data()), !first.reversed };
                b.tip = rct3::park::track::tip_of(scene.objects[object->second], *shape, turned.reversed, scene.grid);
                b.candidates = rct3::park::track::pieces_after(c->pieces, b.tip->end, turned);
            }
        }
        // Only the pieces its kind lists, not others of the same track overlay.
        std::erase_if(b.candidates, [&](const rct3::park::track::LaidPiece& l) { return !rct3::park::track::lists_piece(rules, c->pieces[l.index].record.name); });
        // Only pieces that can be laid there, so a piece offered one way round is not hidden by the other.
        if (!starting)
            std::erase_if(b.candidates, [&](const rct3::park::track::LaidPiece& l) { return !rct3::park::track::place_after(*b.tip, c->pieces[l.index], l.reversed, scene.grid); });
        // Of side by side tracks, only joined pieces or only pieces alone, as the split button says.
        if (b.split != rct3::park::track::SplitState::None)
            std::erase_if(b.candidates, [&](const rct3::park::track::LaidPiece& l) {
                const rct3::park::track::PieceRecord& r = c->pieces[l.index].record;
                return rct3::park::track::split_joined(b.split) == r.partner.empty() || (starting && r.side != rct3::park::track::split_side(b.split));
            });
        // A track being begun starts on its station, as the panel picks the station before the player picks.
        if (std::exchange(b.station_first, false) && starting)
            b.choice = rct3::park::track::opening_choice(c->pieces, b.candidates);
        b.choice = rct3::park::track::settle_choice(c->pieces, b.candidates, b.choice);
        b.picked = rct3::park::track::chosen_piece(c->pieces, b.candidates, b.choice);
        if (starting && !b.design.empty())
        {
            // A design starts with its own first piece, wherever the choice stands.
            const auto first = rct3::park::track::shape_named(*c, b.design.front().symbol);
            b.picked = first ? std::optional(rct3::park::track::LaidPiece { static_cast<std::size_t>(&*first - c->pieces.data()), b.design.front().reversed }) : std::nullopt;
        }
        if (!b.picked)
            return;
        const rct3::park::track::PieceShape& shape = c->pieces[b.picked->index];
        if (starting)
        {
            // Laid from where it would start standing on the aimed tile.
            save::model::PlacedObject on;
            on.col = b.aim->col;
            on.row = b.aim->row;
            on.rotation = b.turn.value;
            on.symbol = shape.record.item;
            on.overlay = b.overlay;
            // A piece that goes only on water floats on it, not on the lake's floor.
            const auto surface = rct3::park::water::water_surface(scene.footpaths.water.bodies, *b.aim);
            if (const auto model = load_object_model(scene, b.models, assets, on) ? scene.models.find(rct3::park::object::model_ref(on)) : scene.models.end(); surface && model != scene.models.end() && model->second.water_only)
            {
                on.base_height = *surface;
                on.force_absolute_height = true;
            }
            // Raised with Shift, never under the ground.
            if (b.raised)
            {
                on.base_height = std::max({ *b.raised, on.base_height.value_or(0), rct3::park::ride::ride_site(*b.aim, shape.record.squares, b.turn, scene.grid).height });
                on.force_absolute_height = true;
            }
            // A design stands with its lowest piece where its first would, as it was built on the ground.
            if (!b.design.empty())
            {
                if (const std::int32_t lift = b.design.front().height - std::ranges::min(b.design, { }, &save::model::TrackPiece::height).height; lift > 0)
                {
                    on.base_height = on.base_height.value_or(rct3::park::ride::ride_site(*b.aim, shape.record.squares, b.turn, scene.grid).height) + lift;
                    on.force_absolute_height = true;
                }
            }
            b.tip = rct3::park::track::start_of(on, shape, b.picked->reversed, scene.grid);
        }
        b.ghost = rct3::park::track::place_after(*b.tip, shape, b.picked->reversed, scene.grid);
        if (!b.ghost)
            return;
        save::model::PlacedObject& o = b.ghost->object;
        o.overlay = b.overlay;
        const save::model::RideColours paint = next_colours(scene, b);
        o.colours = { paint[0], paint[1], paint[2] };
        if (!load_object_model(scene, b.models, assets, o))
            return;
        b.off_water = off_water(scene, b, assets, o);
        // A design shows whole with its doors, and may be laid only whole: any piece or door that may not stand,
        // or a piece that does not follow, refuses all of it.
        std::vector<save::model::PlacedObject> shown { o };
        bool whole = true;
        for (std::size_t i = 1; starting && i < b.design.size(); ++i)
        {
            auto placed = design_piece(*c, b.design, i, o, scene.grid);
            if (!placed || !load_object_model(scene, b.models, assets, placed->object))
            {
                whole = false;
                break;
            }
            b.off_water = b.off_water || off_water(scene, b, assets, placed->object);
            shown.push_back(std::move(placed->object));
        }
        const std::size_t pieces = shown.size();
        if (starting && !b.design.empty() && whole)
        {
            const save::model::TrackPiece to { .tile_x = o.col, .tile_z = o.row, .height = o.base_height.value_or(0), .direction = static_cast<std::uint8_t>(o.rotation) };
            for (const TrackBuilding::DesignDoor& door : b.design_doors)
            {
                save::model::PlacedObject d = moved_door(door.object, b.design.front(), to);
                whole = whole && load_object_model(scene, b.models, assets, d);
                shown.push_back(std::move(d));
            }
        }
        b.ghost_doors = shown.size() - pieces;
        const auto refused = rct3::park::path::first_refused(shown, scene.models, taken_room(scene, b), scene.footpaths.paths, scene.grid);
        // shortcut: the ground a ghost would cut is cut only once it is built, upgrade to show the cut under the
        // ghost if pieces into hills are hard to see.
        b.in_ground = whole && !refused && cut_refused(scene, std::span(shown).first(pieces));
        b.clashes = !whole || refused.has_value() || b.in_ground;
        b.unowned = refused && refused->unowned;
        b.refusal = rct3::park::DesignRefusal::None;
        b.pool_openings.clear();
        if (starting && !b.design.empty())
        {
            // A design is refused for the first of its reasons, in the order its panel tells them.
            const std::span<const save::model::PlacedObject> laid = std::span(shown).first(pieces);
            const auto ground = [&](const save::model::PlacedObject& p) { return static_cast<float>(rct3::park::ride::ride_site({ p.col, p.row }, { 1, 1 }, { }, scene.grid).height); };
            std::vector<float> grounds;
            std::vector<rct3::park::pools::PoolOpening>& openings = b.pool_openings;
            float lowest = std::numeric_limits<float>::max();
            // A piece whose own height is under the ground passes into it; one on the ground that only dips into
            // it, as a banked piece does, is cut round as a piece built by hand is.
            bool buried = false;
            for (std::size_t i = 0; i < laid.size(); ++i)
            {
                grounds.push_back(ground(laid[i]));
                const float base = laid[i].base_height ? static_cast<float>(*laid[i].base_height) : grounds.back();
                lowest = std::min(lowest, base);
                buried = buried || base < grounds.back();
                if (const auto piece = rct3::park::track::shape_named(*c, b.design[i].symbol))
                    std::ranges::copy(rct3::park::piece_pool_openings(laid[i], piece->record), std::back_inserter(openings));
            }
            using enum rct3::park::DesignRefusal;
            b.refusal = b.unowned ? NotOwned : whole && rct3::park::stands_too_high(lowest, grounds) ? TooHigh : !whole || refused ? Intersects : b.in_ground || (buried && !b.cut_ground) ? IntoGround : !rct3::park::meets_pools(scene.pools, openings) ? OffPoolEdge : None;
            b.in_ground = b.refusal == IntoGround;
            b.clashes = b.refusal != None;
        }
        // A station piece shows its platform, as it will stand once laid between its neighbours.
        // shortcut: a design's ghost shows none; upgrade by laying its platforms along it the same way.
        const auto kit = has_platform(shape) && b.design.empty() ? ride_platforms(b, ride, assets) : std::nullopt;
        if (kit)
        {
            const auto station = [&](std::size_t i) {
                const auto s = rct3::park::track::shape_named(*c, ride.track->pieces[i].symbol);
                return ride.track->pieces[i].platform.has_value() || (s && has_platform(*s));
            };
            const auto around = b.selected ? rct3::park::track::piece_neighbours(*ride.track, *b.selected) : std::nullopt;
            // Built on from the open end the piece follows the last piece, or goes before the first when the
            // track is built back from it.
            const std::optional<std::size_t> before = around ? std::optional(around->before) : starting ? std::nullopt : std::optional(b.backward ? 0 : ride.track->pieces.size() - 1);
            const std::optional<std::size_t> after = around ? std::optional(around->after) : std::nullopt;
            const rct3::park::ride::PlatformPlace place = platform_place(shape, { .station_before = before && station(*before), .station_after = after && station(*after), .track_before = before.has_value(), .track_after = after.has_value() });
            const save::model::TrackPiece laid { .tile_x = o.col, .tile_z = o.row, .height = o.base_height.value_or(0), .direction = static_cast<std::uint8_t>(o.rotation) };
            if (const auto name = rct3::park::ride::platform_symbol(ride.platform_type, laid, place, kit->doors, kit->names))
            {
                save::model::PlacedObject platform = o;
                platform.overlay = kit->overlay;
                platform.symbol = *name;
                if (load_object_model(scene, b.models, assets, platform))
                {
                    b.ghost_platform = platform.symbol;
                    shown.push_back(std::move(platform));
                }
            }
        }
        // Ghost track is held up as built track is, its supports ghosts too.
        if (const rct3::park::support::SupportSets* sets = park::support_sets(scene, assets))
        {
            for (save::model::PlacedObject& piece : park::supports_under(scene, shown, *sets))
            {
                if (load_object_model(scene, b.models, assets, piece))
                    shown.push_back(std::move(piece));
            }
        }
        b.ghost_mesh = rct3::park::object::object_mesh(shown, scene.models, scene.grid);
        for (rct3::park::GroundVertex& v : b.ghost_mesh.vertices)
            v.colour *= b.off_water || b.clashes ? kRefusedTint : kGhostTint;
    }

    void track_changed(park::ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets)
    {
        const profiling::Scope zone("TrackChanged");
        const rct3::park::track::PieceCatalogue* c = catalogue(b);
        save::model::Ride& ride = scene.rides.rides.at(*b.ride);
        if (c == nullptr || !ride.track)
            return;
        std::vector<rct3::park::track::LaidPiece> laid;
        for (const save::model::TrackPiece& p : ride.track->pieces)
            if (const auto shape = rct3::park::track::shape_named(*c, p.symbol))
                laid.push_back({ static_cast<std::size_t>(&*shape - c->pieces.data()), p.reversed });
        const bool patterned = laid.size() == ride.track->pieces.size() && *b.ride < scene.track_rules.size();
        const std::vector<rct3::park::track::LaidPiece> swapped = patterned ? rct3::park::track::apply_patterns(c->pieces, laid, scene.track_rules[*b.ride].patterns, ride.track->circuit) : laid;
        if (swapped != laid)
        {
            wait_for_build(scene);
            const auto objects = object_indices(scene);
            const auto object_of = [&](const save::model::TrackPiece& p) {
                const auto found = objects.find(p.scenery);
                return found != objects.end() ? core::maybe(&scene.objects[found->second]) : std::nullopt;
            };
            for (std::size_t i = 0; i < laid.size(); ++i)
            {
                save::model::TrackPiece& piece = ride.track->pieces[i];
                const auto object = object_of(piece);
                if (swapped[i] == laid[i] || !object)
                    continue;
                // Where the piece starts: past the piece before it, or where it starts itself.
                const auto before = i > 0 ? object_of(ride.track->pieces[i - 1]) : std::nullopt;
                const rct3::park::track::TrackTip start = before ? rct3::park::track::tip_of(*before, c->pieces[swapped[i - 1].index], swapped[i - 1].reversed, scene.grid) : rct3::park::track::start_of(*object, c->pieces[laid[i].index], laid[i].reversed, scene.grid);
                const rct3::park::track::PieceShape& shape = c->pieces[swapped[i].index];
                const auto placed = rct3::park::track::place_after(start, shape, swapped[i].reversed, scene.grid);
                if (!placed)
                    continue;
                object->symbol = placed->object.symbol;
                object->col = placed->object.col;
                object->row = placed->object.row;
                object->rotation = placed->object.rotation;
                object->base_height = placed->object.base_height;
                (void)load_object_model(scene, b.models, assets, *object);
                piece.symbol = shape.record.name + std::string(kSectionTag);
                piece.tile_x = object->col;
                piece.tile_z = object->row;
                piece.height = object->base_height.value_or(0);
                piece.direction = static_cast<std::uint8_t>(object->rotation);
                piece.reversed = swapped[i].reversed;
            }
            objects_changed(scene);
        }
        // shortcut: the simulation keeps running the trains on the course the park loaded with, upgrade when
        // a changed track is taken into the running park.
        if (*b.ride < scene.courses.size())
            scene.courses[*b.ride] = rct3::park::track::track_course(ride, scene.objects, scene.models, scene.grid, scene.fitted, [&](const save::model::TrackPiece& p, const save::model::PlacedObject&) { return rct3::park::track::shape_named(*c, p.symbol); });
        update_platforms(scene, *b.ride, assets);
    }

    void build(park::ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets, simulation::park::Charge charge)
    {
        const rct3::park::track::PieceCatalogue* c = catalogue(b);
        save::model::Ride& ride = scene.rides.rides.at(*b.ride);
        if (c == nullptr || !b.ghost || b.off_water || b.clashes || !b.picked || !ride.track)
            return;
        wait_for_build(scene);
        const auto over = b.selected ? built_piece(scene, *c, ride.track->pieces.at(*b.selected)) : std::nullopt;
        if (b.selected && !over)
            return;
        const auto step_on = [&] {
            const auto around = rct3::park::track::piece_neighbours(*ride.track, *b.selected);
            const std::size_t next = around ? (b.selected_back ? around->before : around->after) : 0;
            // Off the end of a stretch, building goes back to the open end.
            b.selected = around && rct3::park::track::piece_neighbours(*ride.track, next) ? std::optional(next) : std::nullopt;
            if (const auto laid = b.selected ? laid_piece(*c, ride.track->pieces[next]) : std::nullopt)
                b.choice = rct3::park::track::choice_of(c->pieces, *laid);
            if (!b.selected)
                use_split(scene, b, b.split);
            refresh(scene, b, assets);
        };
        if (over && over->laid == *b.picked)
        {
            step_on();
            return;
        }
        save::model::PlacedObject object = b.ghost->object;
        object.id = over ? scene.objects[over->object].id : last_id(scene) + 1;
        object.owner_ref = over ? scene.objects[over->object].owner_ref : object.id;
        const auto objects = object_indices(scene);
        save::model::TrackPiece piece;
        if (!ride.track->pieces.empty())
        {
            const save::model::TrackPiece& before = b.selected ? ride.track->pieces[*b.selected] : ride.track->pieces.back();
            piece.colours = before.colours;
            piece.segment = ride.track->segments.empty() || b.selected ? before.segment : static_cast<std::uint32_t>(ride.track->segments.size() - 1);
            piece.asset = before.asset;
            if (const auto found = objects.find(before.scenery); found != objects.end())
                object.colours = scene.objects[found->second].colours;
        }
        else
        {
            piece.colours = starting_colours(scene, b);
            object.colours = { piece.colours[0], piece.colours[1], piece.colours[2] };
        }
        if (b.colours)
        {
            piece.colours = *b.colours;
            object.colours = { (*b.colours)[0], (*b.colours)[1], (*b.colours)[2] };
        }
        const rct3::park::track::PieceShape& shape = c->pieces[b.picked->index];
        const units::Money cost { shape.record.cost };
        if (!simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, cost, simulation::park::LedgerLine::RideConstruction, charge))
            return;
        add_running_cost(scene, ride, cost);
        ++scene.built;
        // The ground it passes into is cut down round it, sharply or smoothly, the scenery standing there seated
        // again; the ground is built again for that, or for the tunnel it lies in.
        // shortcut: the cut costs nothing, upgrade when the original's price for moving ground is matched.
        const std::span<const save::model::PlacedObject> ghost(&b.ghost->object, 1);
        if (!rct3::park::track::buried_hollows(ghost, scene.models, scene.grid).empty())
            scene.footpaths.pavings_stale = true;
        if (rct3::park::ground::GroundCuts cuts = rct3::park::track::ground_cuts(ghost, scene.models, scene.grid, b.smooth_cuts); !cuts.footprint.empty())
        {
            const rct3::park::ground::GroundHolds holds = rct3::park::ground::ground_holds(scene.objects, scene.models, scene.footpaths.paths, scene.grid);
            (void)rct3::park::ground::allow_cuts(cuts, scene.grid, holds, scene.footpaths.water.bodies);
            save::model::Tile low = cuts.footprint.front().tile;
            save::model::Tile high = low;
            for (const std::vector<rct3::park::ground::GroundCut>* part : { &cuts.footprint, &cuts.rim })
            {
                for (const rct3::park::ground::GroundCut& cut : *part)
                {
                    low = { std::min(low.col, cut.tile.col), std::min(low.row, cut.tile.row) };
                    high = { std::max(high.col, cut.tile.col), std::max(high.row, cut.tile.row) };
                }
            }
            rct3::park::ground::make_cuts(scene.grid, cuts);
            if (rct3::park::ground::seat_followers(scene.objects, holds, scene.models, scene.grid, low, high))
                objects_changed(scene);
            scene.footpaths.pavings_stale = true;
        }
        const bool closed_before = ride.track->circuit;
        // Its end meeting where the first piece starts closes the track.
        if (!opening(ride) && !b.backward && !b.selected)
        {
            const save::model::TrackPiece& first = stretch_start(*ride.track);
            const auto first_object = objects.find(first.scenery);
            const auto first_shape = rct3::park::track::shape_named(*c, first.symbol);
            if (first_object != objects.end() && first_shape)
            {
                const rct3::park::track::TrackTip start = rct3::park::track::start_of(scene.objects[first_object->second], *first_shape, first.reversed, scene.grid);
                const bool closes = glm::length(b.ghost->next.position - start.position) < kCloses && b.ghost->next.end == start.end;
                if (!ride.track->segments.empty())
                    ride.track->segments.back().closed = closes;
                ride.track->circuit = closes && std::ranges::all_of(ride.track->segments, &save::model::TrackSegment::closed);
            }
        }
        piece.id = b.selected ? ride.track->pieces[*b.selected].id : object.id;
        piece.symbol = shape.record.name + std::string(kSectionTag);
        piece.scenery = object.id;
        piece.tile_x = object.col;
        piece.tile_z = object.row;
        piece.height = object.base_height.value_or(0);
        piece.direction = static_cast<std::uint8_t>(object.rotation);
        piece.reversed = b.picked->reversed != b.backward;
        // Brake speeds are stored in whole metres a second, as the files do; none until set, so the ride's.
        if (shape.record.role == rct3::park::track::kRoleBrakes || shape.record.role == rct3::park::track::kRoleBlockBrake)
            piece.brake_speed = b.brake_speed > 0 ? std::optional(b.brake_speed) : std::nullopt;
        if (shape.record.role == kHoldingRole)
            piece.holding_time = b.holding_time;
        piece.car_turn = static_cast<float>(b.car_turn);
        piece.facing = b.facing;
        if (over)
        {
            save::model::TrackPiece& old = ride.track->pieces[*b.selected];
            refund_piece(scene, *c, ride, old, scene.objects[over->object]);
            hide_platform(scene, old); // A station's platform is built again if the new piece is one
            old = piece;
            scene.objects[over->object] = std::move(object);
            objects_changed(scene);
            track_changed(scene, b, assets);
            step_on();
            return;
        }
        ride.track->pieces.insert(b.backward ? ride.track->pieces.begin() : ride.track->pieces.end(), piece);
        if (!ride.track->segments.empty())
            ++ride.track->segments.back().piece_count;
        scene.attractions[object.id] = { rct3::park::AttractionRef::Kind::Ride, *b.ride };
        scene.objects.push_back(std::move(object));
        // A joined piece is laid with its other half on the other track: on after that track's open end where
        // it starts, else as a stretch of its own.
        // shortcut: demolishing or building over one half leaves the other as it is, upgrade by finding the
        // other half on the same tiles.
        if (const auto other = b.split != rct3::park::track::SplitState::None && !shape.record.partner.empty() ? rct3::park::track::shape_named(*c, shape.record.partner) : std::nullopt)
        {
            save::model::PlacedObject half = scene.objects.back();
            half.id = last_id(scene) + 1;
            half.owner_ref = half.id;
            half.symbol = other->record.item;
            save::model::TrackPiece joined = piece;
            joined.id = joined.scenery = half.id;
            joined.symbol = other->record.name + std::string(kSectionTag);
            const rct3::park::track::TrackTip start = rct3::park::track::start_of(half, *other, joined.reversed, scene.grid);
            const rct3::park::track::TrackTip end = rct3::park::track::tip_of(half, *other, joined.reversed, scene.grid);
            const auto meets = [](const rct3::park::track::TrackTip& a, const rct3::park::track::TrackTip& b) { return glm::length(a.position - b.position) < kCloses && a.end == b.end; };
            std::optional<std::size_t> onto;
            const auto& segments = ride.track->segments;
            for (std::size_t s = 0; !onto && s + 1 < segments.size(); ++s)
            {
                const auto last = segments[s].closed || segments[s].piece_count == 0 ? std::nullopt : built_piece(scene, *c, ride.track->pieces.at(segments[s].first_piece + segments[s].piece_count - 1));
                if (last && c->pieces[last->laid.index].record.side == other->record.side && meets(rct3::park::track::tip_of(scene.objects[last->object], c->pieces[last->laid.index], last->laid.reversed, scene.grid), start))
                    onto = s;
            }
            const std::size_t s = rct3::park::track::add_piece(*ride.track, onto, std::move(joined));
            // Its end meeting where its stretch starts closes that stretch.
            if (const auto first = segments[s].piece_count > 1 ? built_piece(scene, *c, ride.track->pieces.at(segments[s].first_piece)) : std::nullopt)
                ride.track->segments[s].closed = meets(end, rct3::park::track::start_of(scene.objects[first->object], c->pieces[first->laid.index], first->laid.reversed, scene.grid));
            ride.track->circuit = std::ranges::all_of(ride.track->segments, &save::model::TrackSegment::closed);
            (void)load_object_model(scene, b.models, assets, half);
            scene.attractions[half.id] = { rct3::park::AttractionRef::Kind::Ride, *b.ride };
            scene.objects.push_back(std::move(half));
        }
        // A pool ride has no doors: its stations join a pool complex's paving.
        if (ride.track->circuit && !closed_before && !(*b.ride < scene.track_rules.size() && scene.track_rules[*b.ride].pool_ride))
            scene.doors_requested = *b.ride;
        // Its end meeting where another open stretch starts, as across a gap left mid-track, joins that one on.
        auto& segments = ride.track->segments;
        for (std::size_t s = 0; !b.backward && segments.size() > 1 && !segments.back().closed && s + 1 < segments.size(); ++s)
        {
            const auto start = segments[s].closed || segments[s].piece_count == 0 ? std::nullopt : built_piece(scene, *c, ride.track->pieces.at(segments[s].first_piece));
            const auto at = start ? std::optional(rct3::park::track::start_of(scene.objects[start->object], c->pieces[start->laid.index], start->laid.reversed, scene.grid)) : std::nullopt;
            if (at && c->pieces[start->laid.index].record.side == shape.record.side && glm::length(b.ghost->next.position - at->position) < kCloses && b.ghost->next.end == at->end)
            {
                rct3::park::track::join_stretch(*ride.track, s);
                break;
            }
        }
        objects_changed(scene);
        track_changed(scene, b, assets);
        b.tip = track_tip(scene, b);
        refresh(scene, b, assets);
    }

    void show_way(park::ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets, std::span<const rct3::park::track::LaidPlacement> way)
    {
        std::vector<save::model::PlacedObject> shown;
        for (const rct3::park::track::LaidPlacement& p : way)
        {
            save::model::PlacedObject o = p.object;
            o.overlay = b.overlay;
            if (load_object_model(scene, b.models, assets, o))
                shown.push_back(std::move(o));
        }
        b.ghost_mesh = rct3::park::object::object_mesh(shown, scene.models, scene.grid);
        for (rct3::park::GroundVertex& v : b.ghost_mesh.vertices)
            v.colour *= kGhostTint;
    }

    void complete(park::ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets)
    {
        const rct3::park::track::PieceCatalogue* c = catalogue(b);
        const save::model::Ride& ride = scene.rides.rides.at(*b.ride);
        const auto last = last_piece(scene, b);
        if (b.completing)
        {
            b.completing.reset();
            return;
        }
        if (c == nullptr || !b.tip || !last || !ride.track || b.backward || b.selected)
            return;
        if (b.proposed.empty())
        {
            const save::model::TrackPiece& first = stretch_start(*ride.track);
            const auto objects = object_indices(scene);
            const auto object = objects.find(first.scenery);
            const auto shape = rct3::park::track::shape_named(*c, first.symbol);
            if (object == objects.end() || !shape)
                return;
            const rct3::park::track::LaidPiece start { static_cast<std::size_t>(&*shape - c->pieces.data()), first.reversed };
            // shortcut: the way found may cross itself, as only what stands already is in its way; keep the pieces
            // laid in thought in its way too if a found way does.
            const rct3::park::track::TrackRules rules = *b.ride < scene.track_rules.size() ? scene.track_rules[*b.ride] : rct3::park::track::TrackRules { };
            b.completing = rct3::park::track::start_completion(c->pieces, *b.tip, *last, rct3::park::track::start_of(scene.objects[object->second], *shape, first.reversed, scene.grid), start, scene.grid, [&scene, &b, &assets, c, rules, taken = taken_room(scene, b)](save::model::PlacedObject o) {
                o.overlay = b.overlay;
                const bool listed = std::ranges::any_of(c->pieces, [&](const rct3::park::track::PieceShape& p) { return p.record.item == o.symbol && rct3::park::track::lists_piece(rules, p.record.name); });
                return listed && !off_water(scene, b, assets, o) && !clashes(scene, taken, o);
            });
            return;
        }
        const std::vector<rct3::park::track::LaidPlacement> way = std::exchange(b.proposed, { });
        units::Money cost;
        for (const rct3::park::track::LaidPlacement& p : way)
            cost += units::Money { c->pieces[p.piece.index].record.cost };
        if (scene.park.cash && *scene.park.cash < cost)
        {
            b.complaint = "RideAutoCompleteNotEnoughCash";
            return;
        }
        logging::logger("park").info("track finished with {} pieces", way.size());
        for (const rct3::park::track::LaidPlacement& p : way)
        {
            const rct3::park::track::PieceShape& laid = c->pieces[p.piece.index];
            save::model::PlacedObject o = p.object;
            o.overlay = b.overlay;
            if (!load_object_model(scene, b.models, assets, o))
                return;
            b.picked = p.piece;
            b.ghost = rct3::park::track::PiecePlacement { o, rct3::park::track::tip_of(o, laid, p.piece.reversed, scene.grid) };
            b.off_water = false; // The way was found on water
            b.clashes = false;   // and clear of what stands
            build(scene, b, assets);
        }
    }

    void demolish(park::ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets)
    {
        save::model::Ride& ride = scene.rides.rides.at(*b.ride);
        if (!ride.track || ride.track->pieces.empty())
            return;
        // A stretch begun but not yet laid is given up instead.
        if (opening(ride))
        {
            ride.track->segments.pop_back();
            b.tip = track_tip(scene, b);
            refresh(scene, b, assets);
            return;
        }
        wait_for_build(scene);
        save::model::TrackPiece last;
        const rct3::park::track::PieceCatalogue* c = catalogue(b);
        if (b.selected)
        {
            last = rct3::park::track::take_out_piece(*ride.track, *std::exchange(b.selected, std::nullopt));
            if (const auto laid = c != nullptr ? laid_piece(*c, last) : std::nullopt)
                b.choice = rct3::park::track::choice_of(c->pieces, *laid);
            use_split(scene, b, b.split);
        }
        else
        {
            // Built backwards, the first piece is the one laid last.
            const bool front = b.backward && ride.track->pieces.size() > 1;
            last = front ? ride.track->pieces.front() : ride.track->pieces.back();
            ride.track->pieces.erase(front ? ride.track->pieces.begin() : ride.track->pieces.end() - 1);
            ride.track->circuit = false;
            if (!ride.track->segments.empty() && ride.track->segments.back().piece_count > 0)
                --ride.track->segments.back().piece_count;
            // A stretch emptied gives way to the one before, open at its end again.
            if (ride.track->segments.size() > 1 && ride.track->segments.back().piece_count == 0)
                ride.track->segments.pop_back();
            if (!ride.track->segments.empty())
                ride.track->segments.back().closed = false;
        }
        const auto objects = object_indices(scene);
        if (const auto found = objects.find(last.scenery); found != objects.end())
        {
            if (c != nullptr)
                refund_piece(scene, *c, ride, last, scene.objects[found->second]);
            // Its tunnel closes; the ground it cut stays cut.
            if (!rct3::park::track::buried_hollows(std::span(&scene.objects[found->second], 1), scene.models, scene.grid).empty())
                scene.footpaths.pavings_stale = true;
            scene.objects[found->second].hidden = true;
        }
        hide_platform(scene, last);
        scene.attractions.erase(last.scenery);
        objects_changed(scene);
        track_changed(scene, b, assets);
        b.tip = track_tip(scene, b);
        refresh(scene, b, assets);
    }

} // namespace rct3::scene::track
