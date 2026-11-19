// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ride/placement.hpp"

#include "core/types.hpp"
#include "park/boundary_fence.hpp"
#include "park/water/fill.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::park::ride
{

    namespace
    {
        bool has_path(std::span<const save::model::PathTile> paths, i32 col, i32 row)
        {
            return std::ranges::any_of(paths, [&](const save::model::PathTile& p) { return p.col == col && p.row == row; });
        }

        SiteProblem tile_problem(i32 col, i32 row, i32 height, const save::model::TerrainGrid& grid, std::span<const save::model::PathTile> paths, const path::SolidTiles& solids)
        {
            if (!grid.contains(col, row) || !owned_outright(grid.at(col, row)))
                return SiteProblem::NotOwned;
            if (has_path(paths, col, row))
                return SiteProblem::Path;
            // shortcut: anything solid reaching over the base blocks, however high; give rides a height to
            // check against once their models' heights are read.
            if (const auto spans = solids.tiles.find(static_cast<u32>(row) << 16 | static_cast<u32>(col)); spans != solids.tiles.end())
            {
                if (std::ranges::any_of(spans->second, [&](const path::SolidSpan& s) { return s.high > static_cast<f32>(height); }))
                    return SiteProblem::Blocked;
            }
            return SiteProblem::None;
        }

        template<typename Attraction> std::string lowest_free_name(std::span<const Attraction> attractions, std::string_view symbol)
        {
            constexpr std::string_view kNumbered = "%ME% ";
            std::vector<i32> taken;
            for (const Attraction& a : attractions)
                if (strings::equals_ignoring_case(a.base.symbol, symbol) && a.base.name.starts_with(kNumbered))
                    taken.push_back(std::atoi(a.base.name.c_str() + kNumbered.size()));
            i32 number = 1;
            while (std::ranges::contains(taken, number))
                ++number;
            return std::format("{}{}", kNumbered, number);
        }
    } // namespace

    std::string numbered_name(std::span<const save::model::Ride> rides, std::string_view symbol)
    {
        return lowest_free_name(rides, symbol);
    }

    std::string numbered_name(std::span<const save::model::Stall> stalls, std::string_view symbol)
    {
        return lowest_free_name(stalls, symbol);
    }

    void copy_menu(std::span<save::model::Stall> stalls, usize from)
    {
        if (from >= stalls.size())
            return;
        const save::model::Stall& source = stalls[from];
        for (usize i = 0; i < stalls.size(); ++i)
        {
            save::model::Stall& to = stalls[i];
            if (i == from || to.base.symbol != source.base.symbol || to.base.overlay_path != source.base.overlay_path)
                continue;
            std::vector<save::model::StallItem> items;
            for (const save::model::StallItem& item : source.items)
            {
                const auto kept = std::ranges::find(to.items, item.kind_symbol, &save::model::StallItem::kind_symbol);
                save::model::StallItem& copy = items.emplace_back(kept != to.items.end() ? *kept : save::model::StallItem { .item_asset = item.item_asset, .kind_symbol = item.kind_symbol });
                copy.price = item.price;
                copy.name = item.name;
                copy.colours = item.colours;
                copy.extras = item.extras;
            }
            to.items = std::move(items);
            for (save::model::StallExtra& extra : to.extras)
                if (const auto same = std::ranges::find(source.extras, extra.kind_symbol, &save::model::StallExtra::kind_symbol); same != source.extras.end())
                    extra.level = same->level;
        }
    }

    void offer_extras(save::model::Stall& stall, std::span<const CarriedKind> items)
    {
        for (const save::model::StallItem& item : stall.items)
        {
            const auto kind = std::ranges::find_if(items, [&](const CarriedKind& k) { return strings::equals_ignoring_case(k.symbol, item.kind_symbol); });
            if (kind == items.end())
                continue;
            for (const std::string& extra : kind->extras)
                if (std::ranges::none_of(stall.extras, [&](const save::model::StallExtra& e) { return strings::equals_ignoring_case(e.kind_symbol, extra); }))
                    stall.extras.push_back({ .kind_symbol = extra, .level = save::model::StallExtra::kSome });
        }
    }

    void move_item(save::model::Stall& stall, usize from, usize to)
    {
        if (from >= stall.items.size())
            return;
        to = std::min(to, stall.items.size() - 1);
        const auto at = stall.items.begin();
        if (from < to)
            std::rotate(at + static_cast<std::ptrdiff_t>(from), at + static_cast<std::ptrdiff_t>(from) + 1, at + static_cast<std::ptrdiff_t>(to) + 1);
        else
            std::rotate(at + static_cast<std::ptrdiff_t>(to), at + static_cast<std::ptrdiff_t>(from), at + static_cast<std::ptrdiff_t>(from) + 1);
    }

    namespace
    {
        /// Lower case, without "anim" and the zeros that lead a number.
        std::string plain_name(std::string_view text)
        {
            std::string out;
            for (usize i = 0; i < text.size(); ++i)
            {
                const char c = strings::ascii_lower(text[i]);
                if (c == '0' && (out.empty() || !std::isdigit(static_cast<unsigned char>(out.back()))) && i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])))
                    continue;
                out.push_back(c);
            }
            for (usize at; (at = out.find("anim")) != std::string::npos;)
                out.erase(at, 4);
            return out;
        }
    } // namespace

    std::optional<usize> trick_animation(std::string_view trick, std::span<const std::string> animations)
    {
        const usize show = trick.find("Show");
        const std::string tail = plain_name(show == std::string_view::npos ? trick : trick.substr(show + 4));
        for (usize i = 0; i < animations.size(); ++i)
            if (plain_name(animations[i]).ends_with(tail))
                return i;
        return std::nullopt;
    }

    std::vector<save::model::ShowTrick> show_tricks(std::string_view kind, std::span<const std::string> names, std::span<const std::string> animations, std::span<const f32> seconds)
    {
        const auto lasting = [&](std::string_view trick) -> std::optional<f32> {
            const auto a = trick_animation(trick, animations);
            return a && *a < seconds.size() ? std::optional(seconds[*a]) : std::nullopt;
        };
        std::vector<std::string> known;
        for (const std::string& name : names)
            if (const auto show = name.find("Show"); show != std::string::npos && plain_name(kind).ends_with(plain_name(std::string_view(name).substr(0, show + 4))))
                known.push_back(name);
        std::ranges::sort(known);
        std::stable_partition(known.begin(), known.end(), [&](const std::string& n) { return plain_name(n).contains("idle"); });
        std::vector<save::model::ShowTrick> tricks;
        for (const std::string& name : known)
        {
            auto lasts = lasting(name);
            if (!lasts && tricks.empty())
                lasts = lasting("Loop");
            tricks.push_back({ .name = name, .seconds = lasts.value_or(0), .available = true, .invented = true });
        }
        return tricks;
    }

    std::optional<ShowMoment> show_moment(const save::model::RideShow& show, f32 into)
    {
        f32 start = 0;
        for (const u32 t : show.order)
        {
            if (t >= show.tricks.size())
                continue;
            if (into < start + show.tricks[t].seconds)
                return ShowMoment { .trick = t, .into = into - start };
            start += show.tricks[t].seconds;
        }
        return std::nullopt;
    }

    std::optional<MotionAt> show_motion(const save::model::RideShow& show, std::span<const std::string> animations, std::span<const f32> seconds, std::optional<f32> into, f32 idle)
    {
        if (into)
        {
            if (const auto moment = show_moment(show, *into))
                if (const auto a = trick_animation(show.tricks[moment->trick].name, animations))
                    return MotionAt { *a, moment->into };
            // Past its last trick the idle runs on from where the show ended.
            f32 length = 0;
            for (const u32 t : show.order)
                length += t < show.tricks.size() ? show.tricks[t].seconds : 0.0f;
            idle = std::max(*into - length, 0.0f);
        }
        if (show.tricks.empty())
            return std::nullopt;
        auto a = trick_animation(show.tricks.front().name, animations);
        if (!a)
            a = trick_animation("Loop", animations);
        if (!a)
            return std::nullopt;
        const f32 length = *a < seconds.size() ? seconds[*a] : 0.0f;
        return MotionAt { *a, length > 0 ? std::fmod(idle, length) : 0.0f };
    }

    RideSite ride_site(save::model::Tile tile, std::array<i32, 2> squares, ModelTurn turn, const save::model::TerrainGrid& grid)
    {
        const auto [col, row] = tile;
        RideSite site;
        site.turn = ModelTurn::from(turn.value);
        site.span = site.turn.value % 2 == 0 ? squares : std::array { squares[1], squares[0] };
        site.col = col - (site.span[0] - 1) / 2;
        site.row = row - (site.span[1] - 1) / 2;
        f32 sum = 0;
        i32 corners = 0;
        for (i32 r = site.row; r < site.row + site.span[1]; ++r)
        {
            for (i32 c = site.col; c < site.col + site.span[0]; ++c)
            {
                if (!grid.contains(c, r))
                    continue;
                const auto& at = grid.at(c, r).corners;
                sum += std::accumulate(at.begin(), at.end(), 0.0f);
                corners += 4;
            }
        }
        if (corners > 0)
            site.height = static_cast<i32>(std::floor(sum / static_cast<f32>(corners) + 0.5f));
        return site;
    }

    SiteProblem site_problem(const RideSite& site, const save::model::TerrainGrid& grid, std::span<const save::model::PathTile> paths, const path::SolidTiles& solids)
    {
        for (i32 r = site.row; r < site.row + site.span[1]; ++r)
        {
            for (i32 c = site.col; c < site.col + site.span[0]; ++c)
            {
                if (const SiteProblem p = tile_problem(c, r, site.height, grid, paths, solids); p != SiteProblem::None)
                    return p;
            }
        }
        return SiteProblem::None;
    }

    SiteProblem water_problem(const RideSite& site, bool water_only, std::span<const save::model::WaterBody> bodies)
    {
        for (i32 r = site.row; r < site.row + site.span[1]; ++r)
        {
            for (i32 c = site.col; c < site.col + site.span[0]; ++c)
            {
                const auto lake = std::ranges::find_if(bodies, [&](const save::model::WaterBody& b) { return water::holds(b, { c, r }); });
                if (water_only && lake == bodies.end())
                    return SiteProblem::OffWater;
                if (!water_only && lake != bodies.end() && lake->level > static_cast<f32>(site.height))
                    return SiteProblem::UnderWater;
            }
        }
        return SiteProblem::None;
    }

    SiteProblem ground_problem(const RideSite& site, const save::model::TerrainGrid& grid)
    {
        constexpr f32 kSlack = 0.05f; // Ground at the base is under it
        for (i32 r = site.row; r < site.row + site.span[1]; ++r)
        {
            for (i32 c = site.col; c < site.col + site.span[0]; ++c)
            {
                if (grid.contains(c, r) && grid.at(c, r).max_height() > static_cast<f32>(site.height) + kSlack)
                    return SiteProblem::IntoGround;
            }
        }
        return SiteProblem::None;
    }

    i32 clear_height(const RideSite& site, const save::model::TerrainGrid& grid)
    {
        constexpr f32 kSlack = 0.01f; // Heights read back as floats sit a hair off whole units
        f32 top = static_cast<f32>(site.height);
        for (i32 r = site.row; r < site.row + site.span[1]; ++r)
            for (i32 c = site.col; c < site.col + site.span[0]; ++c)
                if (grid.contains(c, r))
                    top = std::max(top, grid.at(c, r).max_height());
        return static_cast<i32>(std::ceil(top - kSlack));
    }

    ground::GroundCuts levelled_ground(const RideSite& site, Flatten flatten, const save::model::TerrainGrid& grid)
    {
        ground::GroundCuts out;
        if (flatten == Flatten::Off)
            return out;
        const auto height = static_cast<f32>(site.height);
        const i32 reach = flatten == Flatten::Smooth ? 1 : 0;
        for (i32 r = site.row - reach; r < site.row + site.span[1] + reach; ++r)
        {
            for (i32 c = site.col - reach; c < site.col + site.span[0] + reach; ++c)
            {
                if (!grid.contains(c, r))
                    continue;
                const bool inside = c >= site.col && c < site.col + site.span[0] && r >= site.row && r < site.row + site.span[1];
                ground::GroundCut cut { { c, r }, grid.at(c, r).corners };
                // Corners go (col,row), (col+1,row), (col,row+1), (col+1,row+1); a tile round the footprint
                // moves those on its edge.
                for (usize k = 0; k < 4; ++k)
                {
                    const i32 x = c + static_cast<i32>(k & 1);
                    const i32 z = r + static_cast<i32>(k >> 1);
                    if (inside || (x >= site.col && x <= site.col + site.span[0] && z >= site.row && z <= site.row + site.span[1]))
                        cut.corners[k] = height;
                }
                if (cut.corners != grid.at(c, r).corners)
                    (inside ? out.footprint : out.rim).push_back(cut);
            }
        }
        return out;
    }

    void level_ground(save::model::TerrainGrid& grid, const RideSite& site)
    {
        const auto height = static_cast<f32>(site.height);
        // Every corner point of the footprint, in each of the up to four tiles that share it.
        for (i32 r = site.row; r <= site.row + site.span[1]; ++r)
        {
            for (i32 c = site.col; c <= site.col + site.span[0]; ++c)
            {
                for (i32 dr = 0; dr < 2; ++dr)
                {
                    for (i32 dc = 0; dc < 2; ++dc)
                    {
                        // The tile whose corner (1 - dc, 1 - dr) is this point; corners go (col,row),
                        // (col+1,row), (col,row+1), (col+1,row+1).
                        const i32 tc = c - 1 + dc;
                        const i32 tr = r - 1 + dr;
                        if (grid.contains(tc, tr))
                            grid.at(tc, tr).corners[static_cast<usize>((1 - dc) + 2 * (1 - dr))] = height;
                    }
                }
            }
        }
    }

    i32 shaped_height(const save::model::TerrainCell& cell, bool up)
    {
        constexpr f32 kSlack = 0.01f; // Heights read back as floats sit a hair off whole units
        if (up)
        {
            const auto top = static_cast<i32>(std::ceil(cell.max_height() - kSlack));
            return cell.min_height() >= static_cast<f32>(top) - kSlack ? top + 1 : top;
        }
        const auto bottom = static_cast<i32>(std::floor(cell.min_height() + kSlack));
        return cell.max_height() <= static_cast<f32>(bottom) + kSlack ? bottom - 1 : bottom;
    }

    DoorSpot door_spot(const RideSite& site, save::model::Tile tile)
    {
        const auto [col, row] = tile;
        const i32 last_col = site.col + site.span[0] - 1;
        const i32 last_row = site.row + site.span[1] - 1;
        const i32 c = std::clamp(col, site.col, last_col);
        const i32 r = std::clamp(row, site.row, last_row);
        // How far the tile is past (or, inside, short of) each side; the nearest side takes the door, on the tile
        // across it from the rim, facing back.
        const std::array<std::pair<i32, Side>, 4> past { {
            { site.col - col, Side::kColBefore },
            { row - last_row, Side::kNextRow },
            { col - last_col, Side::kNextCol },
            { site.row - row, Side::kRowBefore },
        } };
        const Side side = std::ranges::max_element(past, { }, &std::pair<i32, Side>::first)->second;
        const save::model::Tile rim = side == Side::kColBefore ? save::model::Tile { site.col, r } : side == Side::kNextRow ? save::model::Tile { c, last_row } : side == Side::kNextCol ? save::model::Tile { last_col, r } : save::model::Tile { c, site.row };
        const save::model::Tile at = side.beyond(rim);
        return { at.col, at.row, object::turn_facing(side.opposite()) };
    }

    namespace
    {
        /// The two door spots beside station piece `p`, across the sides its track does not run through, each
        /// facing the piece: the one towards lower rows or columns first.
        std::array<DoorSpot, 2> beside_station(const save::model::TrackPiece& p)
        {
            // A track turned 0 or 2 runs along x, so its sides are across z; turned 1 or 3 the other way.
            const bool along_x = p.direction % 2 == 0;
            std::array<DoorSpot, 2> spots;
            for (usize i = 0; i < 2; ++i)
            {
                const Side side = along_x ? std::array { Side::kRowBefore, Side::kNextRow }[i] : std::array { Side::kColBefore, Side::kNextCol }[i];
                const save::model::Tile at = side.beyond({ p.tile_x, p.tile_z });
                spots[i] = { at.col, at.row, object::turn_facing(side.opposite()), p.height };
            }
            return spots;
        }
    } // namespace

    std::optional<DoorSpot> station_door_spot(std::span<const save::model::TrackPiece> stations, save::model::Tile tile)
    {
        std::optional<DoorSpot> best;
        i64 nearest = 0;
        for (const save::model::TrackPiece& p : stations)
        {
            for (const DoorSpot& spot : beside_station(p))
            {
                const i64 dc = spot.col - tile.col, dr = spot.row - tile.row;
                if (!best || dc * dc + dr * dr < nearest)
                {
                    best = spot;
                    nearest = dc * dc + dr * dr;
                }
            }
        }
        return best;
    }

    usize door_station(std::span<const save::model::TrackPiece> stations, std::span<const save::model::Station> saved, const DoorSpot& door)
    {
        // A piece goes on from the one before when it stands on the next tile along that one's track.
        const auto follows = [](const save::model::TrackPiece& a, const save::model::TrackPiece& b) {
            const bool along_x = a.direction % 2 == 0;
            return a.height == b.height && std::abs(b.tile_x - a.tile_x) == (along_x ? 1 : 0) && std::abs(b.tile_z - a.tile_z) == (along_x ? 0 : 1);
        };
        const auto run_of = [&](i32 col, i32 row, ModelTurn turn) -> std::optional<usize> {
            usize run = 0;
            std::optional<usize> found;
            for (usize k = 0; k < stations.size(); ++k)
            {
                if (k > 0 && !follows(stations[k - 1], stations[k]))
                    ++run;
                for (const DoorSpot& spot : beside_station(stations[k]))
                    if (!found && col == spot.col && row == spot.row && turn == spot.turn)
                        found = run;
            }
            if (found && run > 0 && *found == run && follows(stations.back(), stations.front()))
                return 0;
            return found;
        };
        const auto run = run_of(door.col, door.row, door.turn);
        const auto at_run = [&](const std::optional<save::model::PlacementSpot>& spot) { return spot && run_of(spot->col, spot->row, spot_turn(*spot)) == run; };
        for (usize k = 0; k < saved.size(); ++k)
            if (at_run(saved[k].entrance_spot) || at_run(saved[k].exit_spot))
                return k;
        return static_cast<usize>(std::ranges::find_if(saved, [](const save::model::Station& s) { return !s.entrance_spot && !s.exit_spot; }) - saved.begin());
    }

    std::optional<std::string_view> platform_symbol(std::string_view type, const save::model::TrackPiece& piece, const PlatformPlace& place, std::span<const DoorSpot> doors, std::span<const std::string> names)
    {
        bool left = false, right = false;
        for (const DoorSpot& d : doors)
        {
            if (d.height != piece.height || std::abs(d.col - piece.tile_x) + std::abs(d.row - piece.tile_z) != 1)
                continue;
            // A door faces the piece, so the side it stands on is where it faces from.
            const ModelTurn turn = d.turn.turned(-piece.direction);
            left = left || turn == ModelTurn { 3 };
            right = right || turn == ModelTurn { 1 };
        }
        const std::string_view stairs = left && right ? "2stair" : left ? "1stairLeft" : right ? "1stairRight" : "";
        const bool before = place.station_before, after = place.station_after;
        const std::string_view run = !place.run.empty() ? place.run : before && after ? "middle" : after ? "start" : before ? "end" : "startEnd";
        const std::string_view track = !place.track_before && !place.track_after ? "trackStartEnd" : !place.track_before ? "trackStart" : !place.track_after ? "trackEnd" : "";
        const std::string_view side = place.side == 1 ? "_L" : place.side == 2 ? "_R" : "";
        for (const std::string_view r : { run, std::string_view("middle") })
            for (const auto& [with_run, with_track] : { std::pair { true, true }, { true, false }, { false, true }, { false, false } })
            {
                const std::string want = std::format("{}{}{}{}{}", type, with_run ? r : "", stairs, with_track ? track : "", side);
                if (const auto found = std::ranges::find_if(names, [&](const std::string& n) { return strings::equals_ignoring_case(n, want); }); found != names.end())
                    return *found;
            }
        return std::nullopt;
    }

    SiteProblem door_problem(const RideSite& site, const DoorSpot& door, std::optional<DoorSpot> taken, const save::model::TerrainGrid& grid, std::span<const save::model::PathTile> paths, const path::SolidTiles& solids)
    {
        if (taken && taken->col == door.col && taken->row == door.row)
            return SiteProblem::Blocked;
        return tile_problem(door.col, door.row, site.height, grid, paths, solids);
    }

} // namespace rct3::park::ride
